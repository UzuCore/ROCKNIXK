# SPDX-License-Identifier: GPL-2.0-or-later
# Sourced by the project kernel recipe only for T618.
python3 "${PROJECT_DIR}/${PROJECT}/devices/T618/packages/t618-firmware/verify_firmware.py" \
  --manifest "${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json" \
  --source "${T618_PRIVATE_FIRMWARE_DIR}" \
  --destination "${BUILD}/initramfs/usr/lib/firmware" || die "T618 private firmware verification failed"

# This BSP opens the WCN filename directly during early boot, before the
# regular kernel-overlay firmware tree is mounted.
${PKG_BUILD}/scripts/config --set-str CONFIG_EXTRA_FIRMWARE ""
${PKG_BUILD}/scripts/config --set-str CONFIG_EXTRA_FIRMWARE_DIR ""
