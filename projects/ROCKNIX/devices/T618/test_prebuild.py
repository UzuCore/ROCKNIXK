# SPDX-License-Identifier: GPL-2.0-or-later
"""Portable Linux source tests. No firmware, kernel build, or device is needed."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parent


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


boot = load('boot_success', 'filesystem/usr/lib/rg405m/boot_success.py')
pack = load('pack_boot', 'packages/u-boot/pack_boot.py')
firmware = load('private_firmware', 'packages/t618-firmware/import_firmware.py')


class BootControlTests(unittest.TestCase):
    def test_approval_path_is_model_specific(self):
        m = boot.approval_path_for_model(b'Anbernic RG405M').as_posix()
        v = boot.approval_path_for_model(b'Anbernic RG405V').as_posix()
        self.assertEqual(m, '/storage/.config/rg405m/allow-boot-control-write')
        self.assertEqual(v, '/storage/.config/rg405v/allow-boot-control-write')
        self.assertNotEqual(m, v)
        with self.assertRaises(ValueError):
            boot.approval_path_for_model(b'unknown')

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
    def test_extlinux_preserves_model_specific_display_args(self):
        package = Path(__file__).resolve().parent / 'packages/u-boot/package.mk'
        text = package.read_text()
        m_entry = text.split('LABEL ROCKNIXK-RG405M', 1)[1].split('LABEL ROCKNIXK-RG405V', 1)[0]
        v_entry = text.split('LABEL ROCKNIXK-RG405V', 1)[1].split('EOF', 1)[0]
        self.assertIn('fbcon=rotate:1 panic=10', m_entry)
        self.assertIn('console=ttyS1,115200n8 console=tty0', v_entry)
        self.assertNotIn('fbcon=rotate:1', v_entry)

    def test_noop_overlay_and_dt_table(self):
        overlay = pack.no_op_overlay()
        self.assertEqual(pack.fdt_properties(overlay)['sprd,sc-id'], pack.SC_ID.encode() + bytes(1))
        table = pack.dt_table(overlay)
        self.assertEqual(struct.unpack_from('>I', table)[0], 0xd7b7ab1e)
        self.assertEqual(table[64:], overlay)

    def test_model_dtbo_tables_preserve_firmware_selected_index(self):
        for model, index, expected_count in [('RG405M', 0, 1), ('RG405V', 1, 2)]:
            table = pack.dtbo_table_for_model(model)
            magic, total, header, stride, count, offset, page, version = struct.unpack_from('>8I', table)
            self.assertEqual((magic, header, stride, offset), (0xd7b7ab1e, 32, 32, 32))
            self.assertEqual(count, expected_count)
            self.assertLessEqual(total, len(table))
            self.assertEqual(pack.DTBO_INDEX[model], index)
            size, entry_offset = struct.unpack_from('>II', table, offset + index * stride)
            props = pack.fdt_properties(table[entry_offset:entry_offset+size])
            self.assertEqual(props['sprd,sc-id'], pack.SC_ID.encode() + b'\0')
            if model == 'RG405V':
                size0, offset0 = struct.unpack_from('>II', table, offset)
                props0 = pack.fdt_properties(table[offset0:offset0+size0])
                self.assertEqual(props0['sprd,sc-id'], pack.DTBO_FALLBACK_SC_ID.encode() + b'\0')

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
    def test_initramfs_firmware_preserves_lib_symlink(self):
        hook = (ROOT / 'linux/prepare-initramfs.sh').read_text()
        self.assertIn('--destination "${BUILD}/initramfs/usr/lib/firmware"', hook)
        self.assertNotIn('--destination "${BUILD}/initramfs/lib/firmware"', hook)
        self.assertIn('shadows the usr/lib symlink', hook)

    def test_rg405v_firmware_is_separate_from_rg405m(self):
        manifest = json.loads((ROOT / 'firmware-manifest.json').read_text())
        files = {row['file']: row for row in manifest['files']}
        self.assertFalse(manifest['redistribution_approved'])
        self.assertEqual(files['rg405v/wcnmodem.bin']['size'], 949978)
        self.assertNotEqual(files['sprd/rg405v/wifi_board_config.ini']['sha256'],
                            files['sprd/wifi_board_config.ini']['sha256'])
        self.assertEqual(files['sprd/rg405v/bt_configure_pskey.ini']['size'], 5597)
        self.assertEqual(files['sprd/rg405v/bt_configure_rf.ini']['size'], 3180)
        dts = (ROOT / 'linux/dts/sprd/ums512-rg405v.dts').read_text()
        self.assertIn('/lib/firmware/rg405v/wcnmodem.bin', dts)
        self.assertIn('sprd/rg405v/wifi_board_config.ini', dts)
        self.assertIn('sprd/rg405v/bt_configure_pskey.ini', dts)

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
