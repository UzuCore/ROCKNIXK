# RG405M staged Android-v4 entry

## Implemented entry contract

The prepared path is:

`stock SPL/U-Boot -> pre-authorized inactive Android boot slot -> ARM64 kernel`
`with built-in initramfs -> microSD SYSTEM/STORAGE -> ROCKNIX userspace`.

The current preparation does not access the handheld or write eMMC. A future
initial installation requires an already unlocked loader, individual-device
backups and separate authorization for boot/vendor_boot/dtbo/vbmeta on the
inactive slot. The working Android slot, SPL, U-Boot, unique NV and userdata are
not installation targets. No automatic flashing is part of this package.

This is not an unchanged-stock, SD-insertion-only boot mechanism. Removing the
card is not an automatic rollback command. The older project checklist listed
that behavior as a proposed policy, but the implementation must not imply it
without a separate SD-first loader. Recovery uses the retained original slot,
with the explicit plan described in INSTALL.md, not guessed fallback behavior.

## Source and trace evidence

The pinned UMS512 reference at
`beebono/u-boot-ums512@d1f91089bcb2df300f1e8a92938c1ca84a183e8c` contains
`common/loader/loader_nvm.c` and `include/android_bootimg.h`:

- v4 generic and vendor ramdisks must both be nonempty;
- the page size is 4096, with aligned kernel/ramdisk/DT/table/bootconfig offsets;
- the vendor DT is an Android DT table selected by the board's `sprd,sc-id`;
- a gzip kernel is decompressed to the configured kernel address, bounded by
  the boot partition size; header load-address fields are not a license to use
  a different SoC's map;
- Android A/B suffix selection chooses matching boot/vendor_boot partitions.

The independently acquired RG405M U-Boot trace in
`01_rg405m/rg405m_uboot_gist/uboot_log.txt` shows the boot_a/vendor_boot_a/dtbo_a
and vbmeta_a verification chain and an unlocked device state. It does not prove
this new Linux kernel has booted or that stock supports SD-first discovery.

The existing GammaOS kernel is used only as a temporary format-test input.
The actual vendor v4 parsing functions were compiled into a temporary host
test harness and produced the same six component offsets as the packer.
This exercises original parsing code, not a fabricated claim of device boot.

## Preparation failures are terminal

Unknown kernel source hashes, incomplete private firmware, invalid Android/FDT
headers, invalid/missing ramdisk table entries, oversized/decompression-invalid
kernels and ambiguous microSD labels fail preparation. Final carrier metadata
records the parsed component offsets, intended kernel/DT hashes and deployment
policy. Temporary test images are removed rather than retained as release builds.

The existing development AVB key is not a manufacturer signature and is not
production verified-boot protection. Existing SPL/U-Boot/secure-monitor code is
not replaced, bypassed or executed by the source-preparation scripts.
