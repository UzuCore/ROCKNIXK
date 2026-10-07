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
import xml.etree.ElementTree as ET
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

    def test_refresh_only_preserves_successful_bit(self):
        for slot in ('a', 'b'):
            for successful in (False, True):
                old = self.metadata(slot, tries=1, successful=successful)
                new = boot.confirmed_bytes(old, '_' + slot, mark_successful=False)
                index = 12 + 2 * (slot == 'b')
                self.assertEqual(new[index], 15 | (7 << 4) | (128 if successful else 0))

    def test_reboot_app_accepts_both_rg405_models(self):
        self.assertTrue(boot.supported_model(b'Anbernic RG405M'))
        self.assertTrue(boot.supported_model(b'Anbernic RG405V'))
        self.assertFalse(boot.supported_model(b'Unisoc UMS512'))
        script = (ROOT / 'filesystem/usr/bin/rg405m-reboot-android').read_text()
        self.assertIn('from boot_success import supported_model', script)
        self.assertIn('if not supported_model(model):', script)

    def test_boot_success_runs_from_delayed_timer_without_blocking_boot(self):
        systemd = ROOT / 'filesystem/usr/lib/systemd/system'
        service = (systemd / 'rg405m-mark-success.service').read_text()
        timer = (systemd / 'rg405m-mark-success.timer').read_text()
        enabled = systemd / 'timers.target.wants/rg405m-mark-success.timer'
        self.assertNotIn('ExecStartPre=/bin/sleep', service)
        self.assertNotIn('WantedBy=multi-user.target', service)
        self.assertIn('--commit --refresh-only', service)
        self.assertIn('OnBootSec=90', timer)
        self.assertTrue(enabled.is_symlink())


class ImageFormatTests(unittest.TestCase):
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
        self.assertEqual(files['rg405v/wcnmodem.bin']['size'], 947120)
        self.assertEqual(files['rg405v/wcnmodem.bin']['expected_header_hex'], 'a00a1a00')
        self.assertNotEqual(files['sprd/rg405v/wifi_board_config.ini']['sha256'],
                            files['sprd/wifi_board_config.ini']['sha256'])
        self.assertEqual(files['sprd/rg405v/bt_configure_pskey.ini']['size'], 5442)
        self.assertEqual(files['sprd/rg405v/bt_configure_rf.ini']['size'], 3114)
        dts = (ROOT / 'linux/dts/sprd/ums512-rg405v.dts').read_text()
        self.assertIn('/lib/firmware/rg405v/wcnmodem.bin', dts)
        self.assertIn('sprd/rg405v/wifi_board_config.ini', dts)
        self.assertIn('sprd/rg405v/bt_configure_pskey.ini', dts)
        integration = ROOT / 'packages/t618-kernel-integration/sources'
        sdio = (integration / 'drivers/net/wireless/unisoc/sdio.c').read_text()
        bluetooth = (integration / 'drivers/bluetooth/btsprd_hci.c').read_text()
        self.assertIn('const char *hw_param_file = "sprd/wifi_board_config.ini";', sdio)
        self.assertIn('sprd,wifi-board-config-file-name', sdio)
        self.assertIn('const char *pskey_file = BT_PSKEY_INI;', bluetooth)
        self.assertIn('const char *rf_file = BT_RF_INI;', bluetooth)
        self.assertIn('sprd,bt-pskey-file-name', bluetooth)
        self.assertIn('sprd,bt-rf-file-name', bluetooth)

    def test_both_rg405_models_are_kernel_dtb_build_targets(self):
        config = ET.parse(ROOT.parents[1] / 'config.xml').getroot()
        targets = {node.text for node in config.findall('./T618/file')}
        self.assertEqual(targets, {'ums512-rg405m', 'ums512-rg405v'})

    def test_wcn_request_respects_model_firmware_name(self):
        source = (ROOT / 'packages/t618-kernel-integration/sources/drivers/net/wireless/sprdwcn/platform/wcn_boot.c').read_text()
        self.assertIn('const char *firmware_name = "wcnmodem.bin";', source)
        self.assertIn('of_property_read_string(marlin_dev->dev->of_node, "firmware-name",', source)
        self.assertIn('request_firmware(&firmware, firmware_name, marlin_dev->dev)', source)
        self.assertIn('request_firmware_direct(&firmware, firmware_name, marlin_dev->dev)', source)
        v = (ROOT / 'linux/dts/sprd/ums512-rg405v.dts').read_text()
        m = (ROOT / 'linux/dts/sprd/ums512-rg405m.dts').read_text()
        self.assertIn('firmware-name = "rg405v/wcnmodem.bin";', v)
        self.assertNotIn('firmware-name = "rg405v/wcnmodem.bin";', m)

    def test_rg405v_headset_uses_vendor_button_binding(self):
        source = (ROOT / 'linux/dts/sprd/ums512-rg405v.dts').read_text().split('rg405v_headset: headset', 1)[1]
        self.assertNotIn('linux,code', source)
        for key in ('KEY_MEDIA', 'KEY_VOLUMEUP', 'KEY_VOLUMEDOWN'):
            self.assertIn('code = <' + key + '>', source)

    def test_rg405v_sdio_receive_pin_matches_stock(self):
        v = (ROOT / 'linux/dts/sprd/ums512-rg405v.dts').read_text()
        m = (ROOT / 'linux/dts/sprd/ums512-rg405m.dts').read_text()
        self.assertIn('m2-wakeup-ap-gpios = <&ap_gpio 33 GPIO_ACTIVE_LOW>;', v)
        self.assertIn('m2-wakeup-ap-gpios = <&ap_gpio 32 GPIO_ACTIVE_LOW>;', m)

    def test_rg405v_carrier_uses_available_console(self):
        recipe = (ROOT / 'packages/u-boot/package.mk').read_text()
        self.assertIn('if [ "${model}" = "RG405M" ]; then model_cmdline="rootwait quiet fbcon=rotate:1 panic=10";', recipe)
        self.assertIn('if [ "${model}" = "RG405V" ]; then model_cmdline="rootwait quiet fbcon=rotate:1 panic=10";', recipe)
        self.assertIn('model_cmdline="${EXTRA_CMDLINE}";', recipe)

    def test_rg405m_carrier_keeps_working_release_arguments(self):
        recipe = (ROOT / 'packages/u-boot/package.mk').read_text()
        self.assertIn('if [ "${model}" = "RG405M" ]; then model_cmdline="rootwait quiet fbcon=rotate:1 panic=10";', recipe)

    def test_headset_accepts_existing_rg405m_button_property(self):
        source = (ROOT / 'packages/t618-kernel-integration/sources/sound/soc/sprd/vendor/sprd-headset-sc2730.c').read_text()
        self.assertIn('buttons_np, "code", &buttons_data->code', source)
        self.assertIn('buttons_np, "linux,code", &buttons_data->code', source)

    def test_rg405m_retains_known_board_settings(self):
        source = (ROOT / 'linux/dts/sprd/ums512-rg405m.dts').read_text()
        self.assertIn('invert-absx;', source)
        self.assertIn('invert-absy;', source)
        self.assertIn('regulator-always-on;', source.split('&vddldo0 {', 1)[1])
        self.assertNotIn('stdout-path', source)

    def test_rg405m_analog_applies_board_axis_inversion(self):
        source = (ROOT / 'packages/t618-input/sources/rg405m-analog.c').read_text()
        self.assertIn('bool invert[RG405M_AXES];', source)
        self.assertIn('joy->invert[i] = device_property_read_bool(dev, inv[i]);', source)
        self.assertIn('(joy->invert[i] ? -1 : 1) *', source)
        for axis in ('absx', 'absy', 'absrx', 'absry'):
            self.assertIn('"invert-' + axis + '"', source)

    def test_rg405m_audio_keeps_headphone_routes_in_verb(self):
        source = (ROOT / 'filesystem/usr/share/alsa/ucm2/Unisoc/rg405m/HiFi.conf').read_text()
        verb, devices = source.split('SectionDevice.', 1)
        self.assertIn("S_NORMAL_AP01_P_CODEC SWITCH", verb)
        self.assertNotIn("S_NORMAL_AP01_P_CODEC SWITCH", devices)
        self.assertIn('PlaybackPCM "hw:${CardId},0"', devices)
        policy = (ROOT / 'filesystem/usr/share/wireplumber/wireplumber.conf.d/51-rg405m-audio.conf').read_text()
        self.assertIn('api.acp.disable-pro-audio = true', policy)

    def test_rg405m_input_map_keeps_measured_buttons_and_axes(self):
        source = (ROOT / 'filesystem/usr/share/inputplumber/capability_maps/rg405m.yaml').read_text()
        for capability in ('East', 'South', 'West', 'North', 'DPadUp', 'DPadDown',
                           'DPadLeft', 'DPadRight', 'LeftTrigger', 'RightTrigger',
                           'LeftStick', 'RightStick', 'Start', 'Select', 'Guide'):
            self.assertIn(capability, source)
        device = (ROOT / 'filesystem/usr/share/inputplumber/devices/50-rg405m.yaml').read_text()
        self.assertEqual(device.count('capability_map_id: rg405m_keys'), 2)
        self.assertNotIn('matches: []', device)

    def test_t618_retains_full_build_graphics_and_compatibility(self):
        options = (ROOT / 'options').read_text()
        self.assertIn('VULKAN_SUPPORT="yes"', options)
        self.assertIn('ENABLE_32BIT="true"', options)
        self.assertIn('arm)', options)
        self.assertIn('T618_TEST_BUILD', options)

    def test_rg405m_working_source_files_are_preserved(self):
        guard = json.loads((ROOT / 'tests/rg405m-working-baseline.json').read_text())
        repo = ROOT.parents[3]
        for row in guard['files']:
            with self.subTest(path=row['path']):
                path = repo / row['path']
                data = path.readlink().as_posix().encode() if path.is_symlink() else path.read_bytes()
                self.assertEqual(hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest(), row['sha256'])

    def test_rg405m_firmware_hashes_are_preserved(self):
        guard = json.loads((ROOT / 'tests/rg405m-working-baseline.json').read_text())
        current = {row['file']: row['sha256'] for row in json.loads((ROOT / 'firmware-manifest.json').read_text())['files']}
        for name, sha in guard['firmware'].items():
            with self.subTest(firmware=name):
                self.assertEqual(current[name], sha)

    def test_t618_source_lock_is_valid_and_uses_7_1_2(self):
        lock = json.loads((ROOT / 'source-lock.json').read_text())
        self.assertEqual(lock['kernel_version'], '7.1.2')

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
