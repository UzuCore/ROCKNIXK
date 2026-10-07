#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate a future device audit and print a plan. Never execute fastboot/adb."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex

PARTITIONS = ('boot', 'vendor_boot', 'dtbo', 'vbmeta')
LIMITS = dict(zip(PARTITIONS, (64 << 20, 100 << 20, 8 << 20, 1 << 20)))
RECOVERY_GATES = (
    'android_slot_boot_confirmed',
    'fastboot_readonly_probe_passed',
    'bootrom_download_mode_observed',
    'fdl_readonly_probe_passed',
    'recovery_host_assets_verified',
    'device_backup_capture_verified',
)

def validated_plan(audit: dict, carriers: Path, backups: Path) -> dict:
    if audit.get('format') != 'RG405M-device-audit-v2' or audit.get('evidence_chain_valid') is not True:
        raise ValueError('A validated RG405M v2 evidence chain is required')
    if audit.get('device_write_performed') is not False:
        raise ValueError('Audit does not prove a read-only preparation state')
    if audit.get('model') != 'RG405M' or audit.get('bootloader_unlocked') is not True:
        raise ValueError('An identified RG405M with an already unlocked bootloader is required')
    active = audit.get('current_slot')
    if active not in ('a', 'b'):
        raise ValueError('Missing current-slot readback')
    serial = audit.get('serial', '')
    if not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', serial):
        raise ValueError('Invalid device serial')
    missing_gates = [name for name in RECOVERY_GATES if audit.get(name) is not True]
    if missing_gates:
        raise ValueError('Recovery readiness is incomplete: ' + ', '.join(missing_gates))
    target = 'b' if active == 'a' else 'a'
    manifest = json.loads((carriers/'carrier-manifest.json').read_text())
    if (manifest.get('format') != 'RG405M Android v4'
            or manifest.get('requires_unlocked_bootloader') is not True
            or manifest.get('replaces_spl_or_uboot') is not False):
        raise ValueError('Unexpected boot-carrier format or policy')
    files = {row['name']: row for row in manifest['files']}
    if len(files) != len(manifest['files']):
        raise ValueError('Duplicate carrier entries')
    backup_manifest_path = backups/'device-backup.json'
    backup_manifest = json.loads(backup_manifest_path.read_text())
    if (backup_manifest.get('format') != 'RG405M-device-backup-v2'
            or backup_manifest.get('raw_partition_backups') is not True
            or backup_manifest.get('full_boot_regions') is not True):
        raise ValueError('Backup manifest is not a raw full-region v2 capture')
    if backup_manifest.get('serial') != serial:
        raise ValueError('Backup belongs to a different device')
    if backup_manifest.get('android_audit_id') != audit.get('android_audit_id'):
        raise ValueError('Backup belongs to a different Android audit chain')
    entries = {r['partition']: r for r in backup_manifest['files']}
    if len(entries) != len(backup_manifest['files']):
        raise ValueError('Duplicate backup entries')
    required = ['misc', 'miscdata', 'gpt_primary', 'gpt_backup', 'boot0', 'boot1',
                'uboot_a', 'uboot_b', 'prodnv', 'persist', 'l_runtimenv1', 'l_runtimenv2']
    required += [name+'_'+slot for name in ('l_fixnv1', 'l_fixnv2', 'l_deltanv', 'dtb', 'init_boot') for slot in ('a', 'b')]
    required += [name+'_'+slot for name in PARTITIONS for slot in ('a', 'b')]
    for name in required:
        row = entries.get(name)
        if not row:
            raise ValueError('Missing device backup: '+name)
        rel = Path(row['file'])
        if rel.is_absolute() or '..' in rel.parts:
            raise ValueError('Unsafe backup filename')
        path = (backups/rel).resolve(strict=True)
        if not path.is_relative_to(backups.resolve(strict=True)) or not path.is_file() or row['size'] <= 0:
            raise ValueError('Unsafe or empty backup: '+name)
        data = path.read_bytes()
        if len(data) != row['size'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Invalid backup: '+name)
    commands = []
    for name in PARTITIONS:
        row = files.get(name+'.img')
        path = (carriers/(name+'.img')).resolve(strict=True)
        if not path.is_relative_to(carriers.resolve(strict=True)) or not path.is_file():
            raise ValueError('Carrier escapes input directory')
        data = path.read_bytes()
        part = name+'_'+target
        if not row or len(data) != LIMITS[name] or row['size'] != len(data) or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Invalid carrier: '+name)
        if audit.get('partition_sizes', {}).get(part) != LIMITS[name]:
            raise ValueError('Device partition-size mismatch: '+part)
        commands.append(shlex.join(['fastboot', '-s', serial, 'flash', part, str(carriers/(name+'.img'))]))
    commands.append(shlex.join(['fastboot', '-s', serial, '--set-active='+target]))
    return {'execute': False, 'recovery_gate_passed': True,
            'serial': serial, 'android_audit_id': audit.get('android_audit_id'),
            'backup_manifest_sha256': hashlib.sha256(backup_manifest_path.read_bytes()).hexdigest(),
            'retained_android_slot': active, 'target_slot': target,
            'commands_for_manual_review': commands,
            'boot_success_approval_file': '/storage/.config/rg405m/allow-boot-control-write',
            'boot_success_approval_contents': '_'+target,
            'first_failure_action': shlex.join(['fastboot', '-s', serial, '--set-active='+active]),
            'never_flash': ['SPL', 'U-Boot', 'trustos', 'modems', 'NV', 'userdata']}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-audit', type=Path, required=True)
    parser.add_argument('--carriers', type=Path, required=True)
    parser.add_argument('--device-backups', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(validated_plan(json.loads(args.device_audit.read_text()), args.carriers, args.device_backups), indent=2))
