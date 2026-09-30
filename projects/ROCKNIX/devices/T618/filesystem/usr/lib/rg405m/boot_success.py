#!/usr/bin/env python3
"""Opt-in UMS512 A/B update; see vendor boot_control_definition.h/android_ab.c."""
import argparse
import fcntl
import os
from pathlib import Path
import re
import stat
import struct
import zlib

OFFSET, SIZE = 2048, 32
APPROVAL = Path('/storage/.config/rg405m/allow-boot-control-write')

def decode(data: bytes) -> dict:
    if len(data) != SIZE:
        raise ValueError('Boot-control metadata must be exactly 32 bytes')
    if struct.unpack_from('<I', data, 4)[0] != 0x42414342 or data[8] != 1:
        raise ValueError('Unknown boot-control magic/version')
    if data[9] & 7 != 2:
        raise ValueError('Expected exactly two boot slots')
    if zlib.crc32(data[:28]) != struct.unpack_from('<I', data, 28)[0]:
        raise ValueError('Invalid boot-control CRC; no automatic repair')
    return {'suffix': data[:4].split(b'\0')[0].decode('ascii'), 'slots': [
        {'priority': data[12+2*i] & 15, 'tries': (data[12+2*i] >> 4) & 7,
         'successful': bool(data[12+2*i] & 128), 'verity_corrupted': bool(data[13+2*i] & 1)}
        for i in range(2)]}

def confirmed_bytes(data: bytes, suffix: str, mark_successful: bool = True) -> bytes:
    info = decode(data)
    if suffix not in ['_a', '_b'] or info['suffix'] != suffix:
        raise ValueError('Booted slot and stored suffix do not match')
    index = 0 if suffix == '_a' else 1
    slot = info['slots'][index]
    if slot['priority'] == 0 or slot['verity_corrupted']:
        raise ValueError('Refusing to revive an invalid slot')
    new = bytearray(data)
    # Vendor code rejects tries==0 even if successful_boot is true.
    # refresh-only keeps an unsuccessful slot unsuccessful, so U-Boot's tries countdown (and with it the
    # automatic fallback to the other slot after repeated failed boots) stays in force.
    flags = 128 if (mark_successful or slot['successful']) else 0
    new[12+2*index] = slot['priority'] | (7 << 4) | flags
    struct.pack_into('<I', new, 28, zlib.crc32(new[:28]))
    decode(bytes(new))
    return bytes(new)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit', action='store_true')
    parser.add_argument('--refresh-only', action='store_true',
                        help='reset tries to 7 but do not set successful_boot (keeps the auto-fallback)')
    args = parser.parse_args()
    if Path('/sys/firmware/devicetree/base/model').read_bytes().rstrip(b'\0') != b'Anbernic RG405M':
        raise SystemExit('Not an RG405M')
    suffixes = re.findall(r'(?:^|\s)androidboot.slot_suffix=(_[ab])(?:\s|$)', Path('/proc/cmdline').read_text())
    if len(set(suffixes)) != 1:
        raise SystemExit('Missing or ambiguous boot-slot suffix')
    suffix = suffixes[0]
    if args.commit and (os.geteuid() != 0 or not APPROVAL.is_file() or APPROVAL.read_text().strip() != suffix):
        raise SystemExit('Explicit slot-matching approval is required')
    device = Path('/dev/disk/by-partlabel/misc').resolve(strict=True)
    match = re.fullmatch(r'/dev/(mmcblk\d+)p\d+', str(device))
    if not match or not stat.S_ISBLK(device.stat().st_mode):
        raise SystemExit('misc is not an eMMC partition')
    parent = Path('/sys/class/block') / match[1]
    # mmcblk also names removable SD cards. A label named "misc" is not proof.
    if (parent / 'device/type').read_text().strip() != 'MMC' or (parent / 'removable').read_text().strip() != '0':
        raise SystemExit('misc does not belong to non-removable MMC storage')
    fd = os.open(device, (os.O_RDWR if args.commit else os.O_RDONLY) | os.O_CLOEXEC)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        old = os.pread(fd, SIZE, OFFSET)
        new = confirmed_bytes(old, suffix, mark_successful=not args.refresh_only)
        if args.commit and new != old:
            if os.pread(fd, SIZE, OFFSET) != old:
                raise RuntimeError('Metadata changed concurrently')
            if os.pwrite(fd, new, OFFSET) != SIZE:
                raise RuntimeError('Short boot-control write')
            os.fsync(fd)
            if os.pread(fd, SIZE, OFFSET) != new:
                raise RuntimeError('Boot-control readback mismatch')
        print(('Confirmed ' if args.commit else 'Validated only: ') + suffix)
    finally:
        os.close(fd)

if __name__ == '__main__':
    main()
