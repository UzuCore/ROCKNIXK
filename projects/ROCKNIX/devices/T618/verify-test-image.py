#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect emulator-free build artifacts without mounting or writing a device."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zlib


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(4 << 20), b''):
            value.update(chunk)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    checks = []

    def check(name, result, detail=None):
        checks.append({'name': name, 'passed': bool(result), 'detail': detail})
        print(('PASS ' if result else 'FAIL ') + name, flush=True)
        if not result:
            raise ValueError(name)

    def command(arguments):
        result = subprocess.run([str(x) for x in arguments], capture_output=True, timeout=180)
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors='replace'))
        return result.stdout

    images = list(args.output.glob('*test-no-emulators*.img.gz'))
    archives = list(args.output.glob('*test-no-emulators*.tar'))
    check('Exactly one test disk image and update archive', len(images) == len(archives) == 1)
    image, archive = images[0], archives[0]
    artifacts = []
    for path in [image, archive]:
        actual = digest(path)
        checksum = path.with_name(path.name + '.sha256')
        check('SHA256 ' + path.name, checksum.is_file() and checksum.read_text().split()[0] == actual)
        artifacts.append({'path': str(path), 'size': path.stat().st_size, 'sha256': actual})

    try:
        with tempfile.TemporaryDirectory(prefix='t618-artifact-qa-', dir=args.build_dir.parent) as directory:
            temp = Path(directory)
            with tarfile.open(archive) as package:
                for component in ['KERNEL', 'SYSTEM']:
                    entries = [entry for entry in package.getmembers() if entry.isfile() and entry.name.endswith('/target/' + component)]
                    check('Archive contains ' + component, len(entries) == 1)
                    with package.extractfile(entries[0]) as source, (temp / component).open('wb') as destination:
                        shutil.copyfileobj(source, destination, 1 << 20)
            with (temp / 'KERNEL').open('rb') as source:
                check('Raw ARM64 Linux kernel', source.read(64)[56:60] == b'ARM\x64')

            unsquashfs = args.build_dir / 'toolchain/bin/unsquashfs'
            if not unsquashfs.is_file():
                host_unsquashfs = shutil.which('unsquashfs')
                if not host_unsquashfs:
                    raise FileNotFoundError('unsquashfs is required for artifact QA')
                unsquashfs = Path(host_unsquashfs)
            listing = command([unsquashfs, '-ll', temp / 'SYSTEM']).decode(errors='replace')
            names = re.findall(r'squashfs-root/(.+?)(?: -> .*)?$', listing, re.M)
            host_links = [line for line in listing.splitlines() if ' -> /home/uzu/' in line
                          or ' -> /mnt/d/rg405m/' in line]
            check('Target symlinks do not point into the build host', not host_links, host_links)
            forbidden = [name for name in names if re.search(r'(^|/)(retroarch|.*_libretro\.so|libretro/[^/]+\.so)$', name)]
            emulator_bins = {'retroarch', 'duckstation', 'duckstation-nogui', 'flycast', 'ppsspp',
                             'PPSSPPSDL', 'mupen64plus', 'dolphin-emu', 'mednafen', 'scummvm',
                             'wine', 'wine64', 'box64', 'box86', 'yabasanshiro'}
            forbidden += [name for name in names if name.startswith(('usr/bin/', 'usr/libexec/'))
                          and Path(name).name in emulator_bins]
            emulator_catalog = set()
            repo = Path(__file__).resolve().parents[4]
            for recipe in (repo / 'projects/ROCKNIX/packages/emulators').rglob('package.mk'):
                match = re.search(r'^PKG_NAME="([^"]+)"', recipe.read_text(), re.M)
                if match:
                    emulator_catalog.add(match[1])
            check('Repository emulator catalog is available for exclusion audit', bool(emulator_catalog))
            for row in json.loads((args.build_dir / '.threads/plan.json').read_text()):
                name = row['name'].split(':')[0]
                if name in emulator_catalog or name in ['emulators', 'gamesupport', 'lib32', 'wine', 'box64', 'portmaster'] or name.endswith(('-lr', '-sa')) or name.startswith('retroarch'):
                    forbidden.append('build-plan:' + name)
            check('No emulator/core/32-bit payload in plan or SYSTEM', not forbidden, forbidden)
            required = ['usr/bin/emulationstation', 'usr/bin/inputplumber', 'usr/bin/t618-test-status',
                        'etc/t618-test-build', 'usr/share/alsa/ucm2/Unisoc/rg405m/HiFi.conf']
            check('Frontend, input and test utilities are included', all(name in names for name in required), required)
            gallium = [name for name in names if re.fullmatch(r'usr/lib/libgallium-[^/]+\.so', name)]
            check('Panfrost userspace driver is included', 'usr/lib/dri/panfrost_dri.so' in names
                  and 'usr/lib/libEGL_mesa.so.0' in names and len(gallium) == 1)
            gpu_elf = command([unsquashfs, '-cat', temp / 'SYSTEM', gallium[0]])[:64]
            check('Mesa Gallium library is AArch64', gpu_elf[:4] == b'\x7fELF' and gpu_elf[4:6] == b'\x02\x01'
                  and struct.unpack_from('<H', gpu_elf, 18)[0] == 183)
            modules = [name for name in names if name.endswith('/rg405m-analog.ko')]
            check('RG405M analog kernel module is included', len(modules) == 1)
            module_path = temp / 'rg405m-analog.ko'
            module_path.write_bytes(command([unsquashfs, '-cat', temp / 'SYSTEM', modules[0]]))
            module_info = command([args.build_dir / 'toolchain/bin/aarch64-rocknix-linux-gnu-readelf',
                                   '-p', '.modinfo', module_path]).decode()
            vermagic_match = re.search(r'vermagic=([^\n]+)', module_info)
            vermagic = vermagic_match[1].strip() if vermagic_match else ''
            module_parts = Path(modules[0]).parts
            release_name = module_parts[module_parts.index('modules') + 1]
            kernel_banner = re.search(br'Linux version ([^ \x00]+)', (temp / 'KERNEL').read_bytes())
            check('Kernel and analog module versions match', kernel_banner is not None
                  and kernel_banner[1].decode() == release_name and vermagic.startswith(release_name + ' '))
            firmware_manifest = json.loads((repo / 'projects/ROCKNIX/devices/T618/firmware-manifest.json').read_text())
            for row in firmware_manifest['files']:
                firmware = command([unsquashfs, '-cat', temp / 'SYSTEM',
                                    'usr/lib/kernel-overlays/base/lib/firmware/' + row['file']])
                check('Installed firmware ' + row['file'], len(firmware) == row['size']
                      and hashlib.sha256(firmware).hexdigest() == row['sha256'])
            release = command([unsquashfs, '-cat', temp / 'SYSTEM', 'etc/os-release']).decode()
            check('T618 branch and architecture are recorded', all(value in release for value in
                ['BUILD_BRANCH="t618"', 'HW_DEVICE="T618"', 'HW_ARCH="aarch64"']))
            systems = command([unsquashfs, '-cat', temp / 'SYSTEM', 'usr/config/emulationstation/es_systems.cfg']).decode()
            check('Frontend exposes Tools instead of unavailable emulators', '<name>tools</name>' in systems and '<name>psx</name>' not in systems)
            check('Hardware information menu entry exists', 'usr/config/modules/T618 Hardware Information.sh' in names)
            for name in ['usr/bin/emulationstation', 'usr/bin/inputplumber']:
                payload = command([unsquashfs, '-cat', temp / 'SYSTEM', name])
                elf = payload[:64]
                check('AArch64 ELF ' + name, elf[:4] == b'\x7fELF' and elf[4:6] == b'\x02\x01' and struct.unpack_from('<H', elf, 18)[0] == 183)
                executable = temp / Path(name).name
                executable.write_bytes(payload)
                dynamic = command([args.build_dir / 'toolchain/bin/aarch64-rocknix-linux-gnu-readelf',
                                   '-d', executable]).decode(errors='replace')
                needed = re.findall(r'Shared library: \[([^\]]+)\]', dynamic)
                missing = [library for library in needed if not any(directory + library in names
                           for directory in ['usr/lib/', 'lib/', 'usr/lib64/', 'lib64/'])]
                check('Direct shared-library dependencies ' + name, not missing,
                      {'needed': needed, 'missing': missing})
                runtime_paths = [line.strip() for line in dynamic.splitlines() if 'RPATH' in line or 'RUNPATH' in line]
                check('Runtime library paths do not reference the build host ' + name,
                      not any('/home/uzu/' in line or '/mnt/d/' in line for line in runtime_paths), runtime_paths)

            raw = temp / 'disk.img'
            zero = bytes(1 << 20)
            with gzip.open(image, 'rb') as source, raw.open('wb') as destination:
                while True:
                    chunk = source.read(len(zero))
                    if not chunk:
                        break
                    if chunk == zero:
                        destination.seek(len(chunk), 1)
                    else:
                        destination.write(chunk)
                destination.truncate()
            check('Compressed image CRC verified', raw.stat().st_size > 64 << 20)
            with raw.open('rb') as source:
                source.seek(512)
                header = bytearray(source.read(512))
                check('GPT partition table', header[:8] == b'EFI PART')
                length, expected = struct.unpack_from('<II', header, 12)
                struct.pack_into('<I', header, 16, 0)
                check('GPT header CRC', 92 <= length <= 512 and zlib.crc32(header[:length]) == expected)
                entry_lba, count, stride, expected = struct.unpack_from('<QIII', header, 72)
                check('GPT entry bounds', count <= 1024 and 128 <= stride <= 1024)
                source.seek(entry_lba * 512)
                entries = source.read(count * stride)
                check('GPT partition-entry CRC', zlib.crc32(entries) == expected)
                backup_lba = struct.unpack_from('<Q', header, 32)[0]
                check('Backup GPT is located inside the image', backup_lba * 512 + 512 == raw.stat().st_size)
                source.seek(backup_lba * 512)
                backup = bytearray(source.read(512))
                check('Backup GPT signature', backup[:8] == b'EFI PART')
                backup_size, backup_crc = struct.unpack_from('<II', backup, 12)
                struct.pack_into('<I', backup, 16, 0)
                check('Backup GPT header CRC', 92 <= backup_size <= 512 and zlib.crc32(backup[:backup_size]) == backup_crc)
                backup_entries_lba = struct.unpack_from('<Q', backup, 72)[0]
                source.seek(backup_entries_lba * 512)
                check('Primary and backup GPT entries match', source.read(count * stride) == entries)
                partitions = []
                for index in range(count):
                    entry = entries[index * stride:(index + 1) * stride]
                    if any(entry[:16]):
                        start, end = struct.unpack_from('<QQ', entry, 32)
                        partitions.append({'start': start, 'end': end, 'name': entry[56:128].decode('utf-16le').rstrip('\0')})
                check('Two non-overlapping partitions', len(partitions) == 2 and partitions[0]['end'] < partitions[1]['start'])
                source.seek(partitions[0]['start'] * 512)
                fat = source.read(512)
                label = fat[71:82] if fat[82:90] == b'FAT32   ' else fat[43:54]
                check('Boot filesystem label ROCKNIX', label.rstrip() == b'ROCKNIX')
                source.seek(partitions[1]['start'] * 512 + 1024)
                ext4 = source.read(1024)
                check('Storage ext4 label STORAGE', ext4[56:58] == b'\x53\xef' and ext4[120:136].rstrip(b'\0') == b'STORAGE')
            disk_spec = str(raw) + '@@' + str(partitions[0]['start'] * 512)
            mcopy = args.build_dir / 'toolchain/bin/mcopy'
            for component in ['KERNEL', 'SYSTEM']:
                copy = temp / ('fat-' + component)
                command([mcopy, '-i', disk_spec, '::/' + component, copy])
                check('Disk and archive ' + component + ' are identical', digest(copy) == digest(temp / component))
            carriers = temp / 'carriers'
            carriers.mkdir()
            manifest_path = carriers / 'carrier-manifest.json'
            command([mcopy, '-i', disk_spec, '::/extlinux/rg405m/carrier-manifest.json', manifest_path])
            manifest = json.loads(manifest_path.read_text())
            check('Boot carriers refer to the actual built kernel', manifest['kernel_sha256'] == digest(temp / 'KERNEL'))
            safety = carriers / 'SAFETY.md'
            install_plan = carriers / 'installation_plan.py'
            command([mcopy, '-i', disk_spec, '::/extlinux/rg405m/SAFETY.md', safety])
            command([mcopy, '-i', disk_spec, '::/extlinux/rg405m/installation_plan.py', install_plan])
            safety_text = safety.read_text(errors='replace')
            plan_text = install_plan.read_text(errors='replace')
            check('Boot partition includes pre-flash safety guidance',
                  'custom-SPL' in safety_text and 'BootROM/FDL' in safety_text)
            check('Boot installation plan enforces recovery readiness', all(name in plan_text for name in [
                'android_slot_boot_confirmed', 'fastboot_readonly_probe_passed',
                'bootrom_download_mode_observed', 'fdl_readonly_probe_passed',
                'recovery_host_assets_verified', 'device_backup_capture_verified']))
            dtb = temp / 'ums512-rg405m.dtb'
            command([mcopy, '-i', disk_spec, '::/device_trees/ums512-rg405m.dtb', dtb])
            check('Boot carriers and standalone DTB match', manifest['dtb_sha256'] == digest(dtb))
            check('Carrier installation policy is explicit', manifest['requires_unlocked_bootloader'] is True
                  and manifest['replaces_spl_or_uboot'] is False and manifest['hardware_tested'] is False)
            check('Four named boot carriers', {row['name'] for row in manifest['files']} ==
                  {'boot.img', 'vendor_boot.img', 'dtbo.img', 'vbmeta.img'} and len(manifest['files']) == 4)
            for row in manifest['files']:
                destination = carriers / row['name']
                command([mcopy, '-i', disk_spec, '::/extlinux/rg405m/' + row['name'], destination])
                check('Carrier size and SHA256 ' + row['name'], destination.stat().st_size == row['size']
                      and digest(destination) == row['sha256'])
            avbtool = args.build_dir / 'toolchain/t618-avbtool/avbtool.py'
            key = args.build_dir / 'toolchain/t618-avbtool/testkey_rsa4096.pem'
            verified = subprocess.run(['python3', str(avbtool), 'verify_image', '--image', str(carriers / 'vbmeta.img'),
                                       '--key', str(key)], cwd=carriers, capture_output=True, text=True, timeout=120)
            check('Final carrier AVB signatures and descriptors verify', verified.returncode == 0,
                  verified.stderr if verified.returncode else None)

        report = {'passed': True, 'checks': checks, 'artifacts': artifacts, 'hardware_tested': False,
                  'device_accessed': False, 'temporary_files_removed': True, 'os_release': release}
    except Exception as exc:
        report = {'passed': False, 'checks': checks, 'artifacts': artifacts, 'error': str(exc),
                  'hardware_tested': False, 'device_accessed': False}
        args.report.write_text(json.dumps(report, indent=2) + '\n')
        raise
    args.report.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
