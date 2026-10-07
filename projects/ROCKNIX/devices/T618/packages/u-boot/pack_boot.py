#!/usr/bin/env python3
"""Pack ARM64 Image/DTB inputs for the RG405M Android-v4 loader.

No device access, flashing, unlocking, or compilation. The public development
key does not provide production verified-boot security.
"""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

LIMITS = {'boot': 64 << 20, 'vendor_boot': 100 << 20, 'dtbo': 8 << 20}
SC_ID = 'ums512 1h10 1000'
MODELS = {'RG405M': b'Anbernic RG405M\0', 'RG405V': b'Anbernic RG405V\0'}
# GammaOS RG405V passes androidboot.dtbo_idx=1; preserve that selected slot.
DTBO_INDEX = {'RG405M': 0, 'RG405V': 1}
DTBO_FALLBACK_SC_ID = 'ums512 1h10 go'

def align(value: int, boundary: int) -> int:
    return ((value + boundary - 1) // boundary) * boundary

def minimal_cpio() -> bytes:
    name = b'TRAILER!!!\0'
    fields = [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0]
    result = b'070701' + b''.join(f'{x:08x}'.encode() for x in fields) + name
    return result.ljust(align(len(result), 4), b'\0')

def fdt_properties(blob: bytes) -> dict[str, bytes]:
    if len(blob) < 40:
        raise ValueError('Truncated FDT')
    magic, total, ofs, offstrings, offrsv, version, compat, cpu, nstrings, nstruct = struct.unpack_from('>10I', blob)
    if (magic != 0xd00dfeed or not 40 <= total <= len(blob) or version < 17
            or ofs < 40 or ofs % 4 or nstruct < 16 or ofs + nstruct > total
            or offstrings < 40 or offstrings + nstrings > total
            or max(ofs, offstrings) < min(ofs+nstruct, offstrings+nstrings)):
        raise ValueError('Invalid FDT header/bounds')
    strings = blob[offstrings:offstrings+nstrings]
    pos, depth, props, root_seen = ofs, 0, {}, False
    while pos < ofs+nstruct:
        if pos + 4 > ofs+nstruct:
            raise ValueError('Truncated FDT token')
        token = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if token == 1:
            end = blob.index(b'\0', pos, ofs+nstruct)
            if depth == 0:
                if root_seen or end != pos:
                    raise ValueError('Invalid FDT root')
                root_seen = True
            pos = align(end+1, 4)
            depth += 1
        elif token == 2:
            depth -= 1
            if depth < 0:
                raise ValueError('Unbalanced FDT nodes')
        elif token == 3:
            if depth < 1 or pos+8 > ofs+nstruct:
                raise ValueError('Truncated or misplaced FDT property')
            length, nameoff = struct.unpack_from('>II', blob, pos)
            pos += 8
            if pos+length > ofs+nstruct or nameoff >= len(strings):
                raise ValueError('Invalid FDT property')
            end = strings.index(b'\0', nameoff)
            name = strings[nameoff:end].decode('ascii')
            if depth == 1:
                if name in props:
                    raise ValueError('Duplicate FDT root property')
                props[name] = blob[pos:pos+length]
            pos = align(pos+length, 4)
        elif token == 4:
            continue
        elif token == 9:
            if not root_seen or depth != 0:
                raise ValueError('Incomplete FDT tree')
            return props
        else:
            raise ValueError('Unknown FDT token')
    raise ValueError('Missing FDT end token')

def no_op_overlay(sc_id: str = SC_ID) -> bytes:
    strings = b'sprd,sc-id\0'
    value = sc_id.encode()+b'\0'
    body = struct.pack('>I', 1)+bytes(4)
    body += struct.pack('>III', 3, len(value), 0)+value.ljust(align(len(value), 4), b'\0')
    body += struct.pack('>II', 2, 9)
    offstruct = 56
    offstrings = offstruct+len(body)
    header = struct.pack('>10I', 0xd00dfeed, offstrings+len(strings), offstruct, offstrings, 40, 17, 16, 0, len(strings), len(body))
    return header+bytes(16)+body+strings

def dt_table(fdt: bytes | tuple[bytes, ...] | list[bytes]) -> bytes:
    """Build an Android DT table with stable entry indices."""
    entries = (fdt,) if isinstance(fdt, bytes) else tuple(fdt)
    if not entries:
        raise ValueError('DT table must contain at least one entry')
    for item in entries:
        if not fdt_properties(item).get('sprd,sc-id', b'').rstrip(b'\0'):
            raise ValueError('DT entry is missing the UMS512 loader board identifier')
    data_offset = 32 + 32 * len(entries)
    rows, payloads = [], []
    for item in entries:
        rows.append(struct.pack('>8I', len(item), data_offset, 0, 0, 0, 0, 0, 0))
        payloads.append(item)
        data_offset += len(item)
    return (struct.pack('>8I', 0xd7b7ab1e, data_offset, 32, 32, len(entries), 32, 2048, 0)
            + b''.join(rows) + b''.join(payloads))

def dtbo_table_for_model(device_model: str) -> bytes:
    if device_model == 'RG405M':
        return dt_table(no_op_overlay(SC_ID))
    if device_model == 'RG405V':
        return dt_table((no_op_overlay(DTBO_FALLBACK_SC_ID), no_op_overlay(SC_ID)))
    raise ValueError('Unsupported device model: ' + device_model)

def dhtb_vbmeta(data: bytes) -> bytes:
    payload_size = 20480
    if len(data) > payload_size:
        raise ValueError('Root vbmeta exceeds the vendor DHTB payload size')
    payload = data.ljust(payload_size, b'\0')
    header = bytearray(512)
    header[:8] = b'DHTB\x01\0\0\0'
    header[8:40] = hashlib.sha256(payload).digest()
    struct.pack_into('<I', header, 48, payload_size)
    struct.pack_into('<I', header, 60, payload_size)
    return payload.ljust((1 << 20)-512, b'\0')+header

def run(tool: Path, *args: str, cwd: Path | None = None) -> None:
    subprocess.run([sys.executable, str(tool), *map(str, args)], check=True, cwd=cwd)

def loader_layout(boot: bytes, vendor: bytes, overlay: bytes, device_model: str) -> dict:
    """Check offsets required by UMS512 loader_nvm.c's Android-v4 path."""
    if len(boot) < 4096 or boot[:8] != b'ANDROID!' or struct.unpack_from('<I', boot, 40)[0] != 4:
        raise ValueError('Expected Android boot header v4')
    if len(vendor) < 4096 or vendor[:8] != b'VNDRBOOT' or struct.unpack_from('<II', vendor, 8) != (4, 4096):
        raise ValueError('Expected vendor boot v4 with 4096-byte pages')
    if struct.unpack_from('<I', boot, 20)[0] != 1584 or struct.unpack_from('<I', vendor, 2096)[0] != 2128:
        raise ValueError('Unexpected v4 header size')
    ks, rs = struct.unpack_from('<II', boot, 8)
    vs = struct.unpack_from('<I', vendor, 24)[0]
    ds = struct.unpack_from('<I', vendor, 2100)[0]
    ts, count, entry_size, cs = struct.unpack_from('<4I', vendor, 2112)
    if not ks or not rs or not vs or not ds or not ts or not count or entry_size != 108 or count * entry_size != ts:
        raise ValueError('Missing kernel, ramdisk, DTB, or vendor ramdisk table')
    ro = 4096 + align(ks, 4096)
    do = 4096 + align(vs, 4096)
    to = do + align(ds, 4096)
    co = to + align(ts, 4096)
    if ro + rs > len(boot) or co + cs > len(vendor):
        raise ValueError('Boot component exceeds file bounds')
    for index in range(count):
        size, offset = struct.unpack_from('<II', vendor, to + entry_size * index)
        if not size or offset + size > vs:
            raise ValueError('Invalid vendor ramdisk fragment')
    kernel_stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
    image = kernel_stream.decompress(boot[4096:4096+ks], LIMITS['boot'] + 1)
    if not kernel_stream.eof or kernel_stream.unused_data or len(image) > LIMITS['boot'] or image[56:60] != b'ARM\x64':
        raise ValueError('Invalid or oversized ARM64 gzip payload')

    def table(data: bytes) -> list[bytes]:
        if len(data) < 64:
            raise ValueError('Truncated Android DT table')
        magic, total, header, stride, entries, entry_offset, page, version = struct.unpack_from('>8I', data)
        if (magic != 0xd7b7ab1e or not 64 <= total <= len(data) or header != 32
                or stride != 32 or entries < 1 or entry_offset != 32
                or entry_offset + entries * stride > total):
            raise ValueError('Invalid Android DT table header')
        blobs = []
        for index in range(entries):
            size, offset = struct.unpack_from('>II', data, entry_offset + index * stride)
            if not size or offset < entry_offset + entries * stride or offset + size > total:
                raise ValueError('DT entry exceeds table')
            blobs.append(data[offset:offset+size])
        return blobs

    dtbs = table(vendor[do:do+ds])
    if len(dtbs) != 1:
        raise ValueError('Expected exactly one selected vendor DTB')
    dtb = dtbs[0]
    if fdt_properties(dtb).get('sprd,sc-id') != SC_ID.encode()+b'\0':
        raise ValueError('Vendor DTB does not match UMS512 board identifier')
    if device_model not in MODELS or fdt_properties(dtb).get('model') != MODELS[device_model]:
        raise ValueError('Wrong base DT model')
    overlays = table(overlay)
    dtbo_index = DTBO_INDEX[device_model]
    if dtbo_index >= len(overlays):
        raise ValueError(f'DTBO is missing firmware-selected index {dtbo_index}')
    if fdt_properties(overlays[dtbo_index]).get('sprd,sc-id') != SC_ID.encode()+b'\0':
        raise ValueError(f'DTBO index {dtbo_index} does not match UMS512 board identifier')
    cmdline = boot[44:1580].split(b'\0', 1)[0].decode('ascii')
    words = cmdline.split()
    for prefix in ['boot=LABEL=', 'disk=LABEL=']:
        if len([word for word in words if word.startswith(prefix) and len(word) > len(prefix)]) != 1:
            raise ValueError('Missing or ambiguous microSD filesystem selector')
    return {'page_size': 4096, 'kernel_offset': 4096, 'ramdisk_offset': ro,
            'vendor_ramdisk_offset': 4096, 'dt_offset': do, 'vendor_ramdisk_table_offset': to,
            'vendor_bootconfig_offset': co, 'kernel_sha256': hashlib.sha256(image).hexdigest(),
            'dtb_sha256': hashlib.sha256(dtb).hexdigest(), 'cmdline': cmdline,
            'dtbo_index': dtbo_index, 'dtbo_entry_count': len(overlays),
            'entry_policy': 'pre-authorized inactive Android boot slot; SYSTEM/STORAGE on microSD',
            'unchanged_stock_sd_boot': False, 'sd_removal_auto_rollback': False}

def vbmeta_chain_check(avbtool: Path, template: Path, key: Path, work: Path) -> None:
    """The template must chain boot, vendor_boot and dtbo to the key that signs our footers."""
    pub = work/'dev_pubkey.bin'
    run(avbtool, 'extract_public_key', '--key', key, '--output', pub)
    want = hashlib.sha1(pub.read_bytes()).hexdigest()
    info = subprocess.run([sys.executable, str(avbtool), 'info_image', '--image', str(template)],
                          check=True, capture_output=True, text=True).stdout
    chains = dict(re.findall(r'Chain Partition descriptor:\s+Partition Name:\s+(\S+)\s+Rollback Index Location:\s+\d+\s+Public key \(sha1\):\s+([0-9a-f]{40})', info))
    for part in ('boot', 'vendor_boot', 'dtbo'):
        if chains.get(part) != want:
            raise ValueError(f'vbmeta template does not chain {part} to the development key')
    if 'Flags:                    0' not in info:
        raise ValueError('vbmeta template must have flags 0')

def pack(args: argparse.Namespace) -> None:
    kernel = args.kernel.read_bytes()
    if len(kernel) < 64 or kernel[56:60] != b'ARM\x64':
        raise ValueError('Input is not a raw ARM64 Linux Image')
    if len(kernel) > LIMITS['boot']:
        raise ValueError('Uncompressed Image exceeds the vendor 64 MiB decompression limit')
    dtb = args.dtb.read_bytes()
    if args.device_model not in MODELS or MODELS[args.device_model] != fdt_properties(dtb).get('model'):
        raise ValueError('Device model does not match input DTB')
    if len(args.cmdline.encode()) >= 1536 or not args.cmdline.isascii() or '\0' in args.cmdline or '\n' in args.cmdline:
        raise ValueError('Command line too long')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rg405m-carrier-', dir=args.output.parent) as temp:
        work = Path(temp)
        (work/'Image.gz').write_bytes(gzip.compress(kernel, compresslevel=9, mtime=0))
        (work/'empty.cpio.gz').write_bytes(gzip.compress(minimal_cpio(), mtime=0))
        (work/'dtb.table').write_bytes(dt_table(dtb))
        (work/'dtbo.img').write_bytes(dtbo_table_for_model(args.device_model))
        run(args.mkbootimg, '--header_version', '4', '--pagesize', '4096', '--base', '0',
            '--kernel', work/'Image.gz', '--ramdisk', work/'empty.cpio.gz',
            '--vendor_ramdisk', work/'empty.cpio.gz', '--dtb', work/'dtb.table',
            '--kernel_offset', '0x8000', '--ramdisk_offset', '0x05400000',
            '--tags_offset', '0x100', '--dtb_offset', '0x01f00000',
            '--cmdline', args.cmdline, '--vendor_cmdline', 'printk.devkmsg=on',
            '--os_version', '12.0.0', '--os_patch_level', '2022-07',
            '--output', work/'boot.img', '--vendor_boot', work/'vendor_boot.img')
        for name, limit in LIMITS.items():
            run(args.avbtool, 'add_hash_footer', '--image', work/(name+'.img'),
                '--partition_name', name, '--partition_size', str(limit),
                '--algorithm', 'SHA256_RSA4096', '--key', args.development_key,
                '--salt', hashlib.sha256(name.encode()+kernel).hexdigest())
        descriptors = []
        for name in LIMITS:
            descriptors += ['--include_descriptors_from_image', str(work/(name+'.img'))]
        # Stock UMS512 U-Boot rejects a hash-descriptor vbmeta with flags=3 (kernel never starts).
        # It accepts the GammaOS-style chain-partition vbmeta whose boot/vendor_boot/dtbo chains use
        # the same AOSP test key as our footers, so ship that template unchanged.
        vbmeta_chain_check(args.avbtool, args.vbmeta_template, args.development_key, work)
        template = args.vbmeta_template.read_bytes()
        if len(template) > 1 << 20:
            raise ValueError('vbmeta template larger than the partition')
        (work/'vbmeta.img').write_bytes(template.ljust(1 << 20, b'\0'))
        manifest = {'format': 'RG405 Android v4', 'device_model': args.device_model,
                    'requires_unlocked_bootloader': True,
                    'signing': 'AOSP test key footers + chain-partition vbmeta template (unlocked loader)',
                    'replaces_spl_or_uboot': False, 'kernel_sha256': hashlib.sha256(kernel).hexdigest(),
                    'dtb_sha256': hashlib.sha256(dtb).hexdigest(), 'files': []}
        for name in [*LIMITS, 'vbmeta']:
            file = work/(name+'.img')
            expected = LIMITS.get(name, 1 << 20)
            if file.stat().st_size != expected:
                raise ValueError('Carrier partition size mismatch: '+name)
            if name != 'vbmeta':   # chain vbmeta also references vbmeta_system/modem images not built here
                run(args.avbtool, 'verify_image', '--image', file, '--key', args.development_key, cwd=work)
            manifest['files'].append({'name': file.name, 'size': expected, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest()})
        layout = loader_layout((work/'boot.img').read_bytes(), (work/'vendor_boot.img').read_bytes(),
                               (work/'dtbo.img').read_bytes(), args.device_model)
        if layout['kernel_sha256'] != manifest['kernel_sha256'] or layout['dtb_sha256'] != manifest['dtb_sha256']:
            raise ValueError('Loader payload does not match the intended kernel/DTB')
        manifest['loader_layout'] = layout
        manifest['hardware_tested'] = False
        (work/'carrier-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        args.output.mkdir(exist_ok=True)
        for name in [*(n+'.img' for n in [*LIMITS, 'vbmeta']), 'carrier-manifest.json']:
            shutil.copyfile(work/name, args.output/name)

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['kernel', 'dtb', 'mkbootimg', 'avbtool', 'development-key', 'vbmeta-template', 'output']:
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cmdline', required=True)
    p.add_argument('--device-model', choices=sorted(MODELS), required=True)
    pack(p.parse_args())

if __name__ == '__main__':
    main()
