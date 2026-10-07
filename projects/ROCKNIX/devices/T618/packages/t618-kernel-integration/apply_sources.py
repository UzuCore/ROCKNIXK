#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Apply hash-verified board adaptations only within an explicit kernel build root."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile

PACKAGE = Path(__file__).resolve().parent


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plan(kernel: Path, build_root: Path) -> list[tuple[Path, Path, str]]:
    kernel, build_root = kernel.resolve(strict=True), build_root.resolve(strict=True)
    if kernel == build_root or not kernel.is_relative_to(build_root) or not kernel.name.startswith('linux-'):
        raise ValueError('Destination is not an isolated linux-* build directory')
    if not (kernel / 'Makefile').is_file():
        raise ValueError('Missing kernel build root')
    manifest = json.loads((PACKAGE / 'source-manifest.json').read_text())
    result = []
    seen = set()
    for row in manifest['files']:
        relative = PurePosixPath(row['path'])
        if relative.is_absolute() or '..' in relative.parts or not relative.parts or str(relative) in seen:
            raise ValueError('Unsafe or duplicate source path')
        seen.add(str(relative))
        source = PACKAGE / 'sources' / relative
        target = kernel / relative
        if not source.resolve(strict=True).is_relative_to(PACKAGE / 'sources') or digest(source) != row['sha256']:
            raise ValueError('Adaptation source hash mismatch: ' + str(relative))
        if target.is_symlink() or not target.resolve().is_relative_to(kernel):
            raise ValueError('Destination symlink escapes build root')
        current = digest(target) if target.is_file() else None
        if current not in (row['base_sha256'], row['sha256']):
            raise ValueError('Kernel source differs from the pinned input: ' + str(relative))
        result.append((source, target, row['sha256']))
    if not result:
        raise ValueError('Empty source manifest')
    return result


def apply(kernel: Path, build_root: Path, verify_only: bool = False) -> dict:
    files = plan(kernel, build_root)
    if not verify_only:
        for source, target, expected in files:
            if target.is_file() and digest(target) == expected:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temp = tempfile.mkstemp(prefix='.rg405m-', dir=target.parent)
            try:
                with os.fdopen(descriptor, 'wb') as output:
                    output.write(source.read_bytes())
                    output.flush()
                    os.fsync(output.fileno())
                os.chmod(temp, 0o644)
                os.replace(temp, target)
            finally:
                if os.path.exists(temp):
                    os.unlink(temp)
        if any(digest(target) != expected for _, target, expected in files):
            raise ValueError('Adaptation readback failed')
    return {'files': len(files), 'verified_only': verify_only, 'kernel_compiled': False,
            'destination': str(kernel.resolve())}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-tree', type=Path, required=True)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    print(json.dumps(apply(args.kernel_tree, args.build_root, args.verify_only), indent=2))


if __name__ == '__main__':
    main()
