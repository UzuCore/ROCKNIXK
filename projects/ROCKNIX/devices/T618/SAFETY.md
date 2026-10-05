# RG405M pre-flash safety

No T618 installation helper writes a device automatically. Hardware deployment
remains blocked until the device-specific recovery audit and backup contract pass.

The first mainline-kernel path uses only the inactive Android boot slot. The
known-good Android slot remains untouched until the four carrier images have
been verified and the slot switch is the final reviewed action. SD removal is
not an automatic rollback for this path.

The GarlicOS RG405 recovery bootstrap is a useful recovery reference, but it is
not a mainline ROCKNIX boot path: its init template keeps the stock recovery
kernel, and the RG405M stock kernel configuration disables both `CONFIG_KEXEC`
and `CONFIG_KEXEC_FILE`. Its recovery script also clears boot-record state in
`misc`/metadata before entering the SD userspace.

The RG Rotate custom-SPL path can make later kernel iterations SD-only. Its SPL
adds SD U-Boot loading and falls back to the eMMC U-Boot when no valid SD U-Boot
is present. Installing that SPL is a higher-risk one-time boot-region write and
must not be attempted until per-device boot0/boot1 readback and BootROM/FDL
recovery have been rehearsed and the fuse/signing state is known.

The stock V1.15 PAC is not a replacement for the per-device rescue backup. The
extracted PAC manifest has an `SPLLoader` entry without an embedded SPL payload,
and unique NV/calibration data is device-specific.
