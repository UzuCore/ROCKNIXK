# SPDX-License-Identifier: GPL-2.0-or-later
# Sourced by the project kernel recipe only for T618.
python3 "${PROJECT_DIR}/${PROJECT}/devices/T618/packages/t618-firmware/verify_firmware.py" \
  --manifest "${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json" \
  --source "${T618_FIRMWARE_DIR}" \
  --destination "${BUILD}/initramfs/usr/lib/firmware" || die "T618 firmware verification failed"

if [ -d "${BUILD}/initramfs/lib" ] && [ ! -L "${BUILD}/initramfs/lib" ]; then
  die "T618 initramfs/lib shadows the usr/lib symlink; rebuild the initramfs tree"
fi

# This BSP opens the WCN filename directly during early boot, before the
# regular kernel-overlay firmware tree is mounted.
${PKG_BUILD}/scripts/config --set-str CONFIG_EXTRA_FIRMWARE ""
${PKG_BUILD}/scripts/config --set-str CONFIG_EXTRA_FIRMWARE_DIR ""
