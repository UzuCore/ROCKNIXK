# RG405M developer boot carriers

These are Android-v4 kernel/DT carriers, not replacement SPL/U-Boot binaries.
The installed vendor boot chain loads the kernel from an Android boot slot;
the built-in ROCKNIX initramfs then loads SYSTEM and STORAGE from microSD.
The SD extlinux files also support an independently installed compatible loader.
Inserting this SD into an unchanged stock installation does not start Linux.

The bootloader must already be unlocked. Unlocking can erase user data and is
not performed by the preparation scripts. The public AVB development key is
not manufacturer authorization or production verified-boot protection.

Before installation, save the device GPT, misc, both boot/vendor_boot/dtbo/vbmeta
slots, SPL boot regions, U-Boot and unique NV/calibration data. Keep the stock
V1.15 PAC and recovery tools offline. A stock PAC does not replace these unique
device backups.

The installed `installation_plan.py` validates a device audit and a hash manifest
before printing a manual-review plan. It never executes those commands. It
requires both GPT copies, boot0/boot1, misc/miscdata, both U-Boot copies,
prodnv/persist/runtime-NV, and both slots of fixed/delta NV, dtb, init_boot,
boot, vendor_boot, dtbo and vbmeta. Duplicated, missing, empty, foreign-device or
altered backup entries are rejected. This is a file-integrity gate, not proof
that a backup was captured correctly or that a recovery procedure has been tested.

The device audit must also record a confirmed Android boot, read-only fastboot
access, observed BootROM download mode, a read-only FDL probe, verified recovery
host assets and a separately verified device-backup capture. Missing any one of
these recovery gates prevents the plan from being generated.

Use the inactive slot; retain the working Android slot untouched. Write matching
boot, vendor_boot, dtbo and vbmeta carriers and activate the new slot last. Never
write SPL, U-Boot, trustos or modem partitions. No flashing command is executed
by the packaging recipe.

The vendor A/B code decreases retries until a boot is marked successful, and its
rollback path can restore SPL from a backup region. Do not repeatedly reboot an
unconfirmed installation. Return to the original slot with fastboot on first
failure. Removing the SD card alone is not a promised rollback mechanism.

Unattended OTA is disabled: Android carriers and SD SYSTEM/modules must be
updated as a matching set. The update .tar alone does not update the boot-slot
kernel. Read the boot-control audit and post-build test procedure before use.

Boot-success marking is opt-in and requires the exact booted-slot approval file.
It rejects a `misc` label on removable SD storage. This protection is not a global
read-only guarantee for eMMC. The selected entry path is described in `BOOT_PATH.md`; it does not implement
unchanged-stock SD-only boot or SD-removal rollback.
