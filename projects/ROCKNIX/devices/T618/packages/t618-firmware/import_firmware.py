#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Import verified private RG405M inputs from already extracted firmware.

No download, device access or redistribution permission is implied.
All source and transformed hashes are checked before writing any output.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct


def safe_relative(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if not value or not path.parts or path.is_absolute() or '..' in path.parts or '\\' in value:
        raise ValueError('Unsafe relative path: ' + value)
    return path


def avb_payload(data: bytes) -> bytes:
    if len(data) < 64:
        raise ValueError('Missing AVB footer')
    magic, major, minor, original, offset, size = struct.unpack_from('>4sIIQQQ', data, len(data) - 64)
    if magic != b'AVBf' or major != 1 or minor != 0:
        raise ValueError('Unsupported AVB footer')
    if not (0 < original <= offset and 256 <= size and offset + size <= len(data) - 64):
        raise ValueError('Invalid AVB payload/metadata bounds')
    if data[offset:offset + 4] != b'AVB0':
        raise ValueError('Missing vbmeta header')
    return data[:original]


def prepare(manifest: dict, vendor: Path, stock: Path) -> dict[str, bytes]:
    roots = {'vendor': vendor.resolve(strict=True), 'stock': stock.resolve(strict=True)}
    result = {}
    for row in manifest['files']:
        target = str(safe_relative(row['file']))
        origin = safe_relative(row['source_file'])
        if target in result or row['source_group'] not in roots:
            raise ValueError('Duplicate target or unknown input group')
        root = roots[row['source_group']]
        path = root.joinpath(*origin.parts).resolve(strict=True)
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('Input escapes reference root')
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != row['source_sha256']:
            raise ValueError('Unexpected source bytes: ' + str(origin))
        if row.get('transform'):
            if row['transform'] != 'AVB footer original_image_size; tail metadata excluded':
                raise ValueError('Unknown firmware transform')
            data = avb_payload(data)
        if len(data) != row['size'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Unexpected output bytes: ' + target)
        result[target] = data
    if not result:
        raise ValueError('Empty firmware manifest')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--vendor-root', type=Path, required=True,
                        help='Extracted GammaOS Next vendor directory containing firmware/')
    parser.add_argument('--stock-partitions', type=Path, required=True,
                        help='Already extracted stock V1.15 partitions directory')
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    files = prepare(json.loads(args.manifest.read_text()), args.vendor_root, args.stock_partitions)
    if not args.verify_only:
        if args.destination is None:
            parser.error('--destination is required without --verify-only')
        root = args.destination.resolve()
        targets = [(root / name, data) for name, data in files.items()]
        for path, _ in targets:
            if not path.resolve().is_relative_to(root) or path.is_symlink():
                raise ValueError('Destination escapes output root')
        for path, data in targets:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(0o644)
    print(f'Verified {len(files)} private firmware outputs; redistribution is not authorized by this tool')


if __name__ == '__main__':
    main()
