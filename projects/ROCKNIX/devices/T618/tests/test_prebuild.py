# SPDX-License-Identifier: GPL-2.0-or-later
"""Portable Linux source tests. No firmware, kernel build, or device is needed."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


boot = load('boot_success', 'filesystem/usr/lib/rg405m/boot_success.py')
pack = load('pack_boot', 'packages/u-boot/pack_boot.py')
firmware = load('private_firmware', 'packages/t618-firmware/import_firmware.py')


class BootControlTests(unittest.TestCase):
    def metadata(self, slot, priority=15, tries=1, successful=False):
        raw = bytearray(range(32))
        raw[:4] = ('_' + slot).encode() + bytes(2)
        struct.pack_into('<I', raw, 4, 0x42414342)
        raw[8], raw[9] = 1, 2
        index = 12 + 2 * (slot == 'b')
        raw[index] = priority | (tries << 4) | (128 if successful else 0)
        raw[index+1] &= ~1
        struct.pack_into('<I', raw, 28, zlib.crc32(raw[:28]))
        return bytes(raw)

    def test_all_slot_states_preserve_inactive_bytes(self):
        for slot in ('a', 'b'):
            for priority in range(1, 16):
                for tries in range(8):
                    for successful in (False, True):
                        old = self.metadata(slot, priority, tries, successful)
                        new = boot.confirmed_bytes(old, '_' + slot)
                        index = 12 + 2 * (slot == 'b')
                        self.assertEqual(new[index], priority | 0xf0)
                        for offset in range(28):
                            if offset != index:
                                self.assertEqual(old[offset], new[offset])
                        self.assertEqual(boot.confirmed_bytes(new, '_' + slot), new)

    def test_refresh_only_keeps_successful_bit(self):
        for slot in ('a', 'b'):
            for priority in range(1, 16):
                for tries in range(8):
                    for successful in (False, True):
                        old = self.metadata(slot, priority, tries, successful)
                        new = boot.confirmed_bytes(old, '_' + slot, mark_successful=False)
                        index = 12 + 2 * (slot == 'b')
                        self.assertEqual(new[index], priority | 0x70 | (128 if successful else 0))
                        for offset in range(28):
                            if offset != index:
                                self.assertEqual(old[offset], new[offset])

    def test_every_single_bit_error_is_rejected(self):
        old = self.metadata('a')
        for bit in range(256):
            corrupt = bytearray(old)
            corrupt[bit // 8] ^= 1 << (bit % 8)
            with self.assertRaises(ValueError):
                boot.confirmed_bytes(bytes(corrupt), '_a')

    def test_wrong_slot_and_unbootable_priority(self):
        with self.assertRaises(ValueError):
            boot.confirmed_bytes(self.metadata('a'), '_b')
        with self.assertRaises(ValueError):
            boot.confirmed_bytes(self.metadata('a', priority=0), '_a')


class ImageFormatTests(unittest.TestCase):
    def test_noop_overlay_and_dt_table(self):
        overlay = pack.no_op_overlay()
        self.assertEqual(pack.fdt_properties(overlay)['sprd,sc-id'], pack.SC_ID.encode() + bytes(1))
        table = pack.dt_table(overlay)
        self.assertEqual(struct.unpack_from('>I', table)[0], 0xd7b7ab1e)
        self.assertEqual(table[64:], overlay)

    def test_truncated_fdt_rejected(self):
        data = pack.no_op_overlay()
        for length in range(len(data)):
            with self.assertRaises(ValueError):
                pack.fdt_properties(data[:length])

    def test_unbalanced_fdt_rejected(self):
        data = bytearray(pack.no_op_overlay())
        struct.pack_into('>I', data, 56, 2)
        with self.assertRaises(ValueError):
            pack.fdt_properties(bytes(data))

    def test_dhtb_payload_hash(self):
        image = pack.dhtb_vbmeta(b'non-deployable test payload')
        self.assertEqual(len(image), 1 << 20)
        self.assertEqual(image[-512:-504], b'DHTB\x01\0\0\0')
        self.assertEqual(image[-504:-472], hashlib.sha256(image[:20480]).digest())
        with self.assertRaises(ValueError):
            pack.dhtb_vbmeta(bytes(20481))

    def test_empty_cpio_is_well_formed(self):
        data = pack.minimal_cpio()
        self.assertEqual(data[:6], b'070701')
        self.assertIn(b'TRAILER!!!\0', data)
        self.assertEqual(len(data) % 4, 0)


class FirmwareTests(unittest.TestCase):
    def test_path_escape_and_empty_target_rejected(self):
        for value in ('', '.', './', '../outside', 'fw/../../outside', '/outside', 'fw\\outside'):
            with self.assertRaises(ValueError):
                firmware.safe_relative(value)

    def test_avb_payload_bounds(self):
        raw = bytearray(4096)
        raw[:64] = b'P' * 64
        raw[512:516] = b'AVB0'
        struct.pack_into('>4sIIQQQ', raw, len(raw)-64, b'AVBf', 1, 0, 64, 512, 256)
        self.assertEqual(firmware.avb_payload(bytes(raw)), b'P' * 64)
        struct.pack_into('>Q', raw, len(raw)-64+12, 513)
        with self.assertRaises(ValueError):
            firmware.avb_payload(bytes(raw))

    def test_manifest_hash_duplicate_and_source_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vendor, stock = root / 'vendor', root / 'stock'
            vendor.mkdir(); stock.mkdir()
            payload = b'non-deployable private firmware test'
            (vendor / 'firmware.bin').write_bytes(payload)
            sha = hashlib.sha256(payload).hexdigest()
            row = {'file': 'sprd/fw.bin', 'source_file': 'firmware.bin', 'source_group': 'vendor',
                   'source_sha256': sha, 'sha256': sha, 'size': len(payload)}
            manifest = {'files': [row]}
            self.assertEqual(firmware.prepare(manifest, vendor, stock), {'sprd/fw.bin': payload})
            with self.assertRaises(ValueError):
                firmware.prepare({'files': [row, row]}, vendor, stock)
            bad = copy.deepcopy(manifest)
            bad['files'][0]['sha256'] = '0' * 64
            with self.assertRaises(ValueError):
                firmware.prepare(bad, vendor, stock)
            (root / 'outside.bin').write_bytes(payload)
            (vendor / 'link.bin').symlink_to(root / 'outside.bin')
            bad = copy.deepcopy(manifest)
            bad['files'][0]['source_file'] = 'link.bin'
            with self.assertRaises(ValueError):
                firmware.prepare(bad, vendor, stock)


if __name__ == '__main__':
    unittest.main()
