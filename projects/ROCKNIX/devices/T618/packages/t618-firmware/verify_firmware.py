#!/usr/bin/env python3
"""Verify every private input before copying any file into a build directory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil

def verify(manifest: Path, source: Path) -> list[tuple[Path, PurePosixPath]]:
    root = source.resolve(strict=True)
    rows = json.loads(manifest.read_text())['files']
    checked, names = [], set()
    for row in rows:
        name = PurePosixPath(row['file'])
        if name.is_absolute() or '..' in name.parts or str(name) in names:
            raise ValueError('Unsafe or duplicate firmware path: ' + str(name))
        names.add(str(name))
        file = root.joinpath(*name.parts).resolve(strict=True)
        if not file.is_relative_to(root) or not file.is_file():
            raise ValueError('Firmware escapes input directory: ' + str(name))
        data = file.read_bytes()
        if len(data) != row['size'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Firmware hash/size mismatch: ' + str(name))
        if row.get('expected_header_hex') and not data.startswith(bytes.fromhex(row['expected_header_hex'])):
            raise ValueError('Firmware header mismatch: ' + str(name))
        checked.append((file, name))
    if not checked:
        raise ValueError('Empty firmware manifest')
    return checked

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    files = verify(args.manifest, args.source)
    if not args.verify_only:
        if args.destination is None:
            parser.error('--destination is required unless --verify-only is set')
        for source, name in files:
            dest = args.destination.joinpath(*name.parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            dest.chmod(0o644)
    print(f'Verified {len(files)} RG405 firmware files')

if __name__ == '__main__':
    main()
