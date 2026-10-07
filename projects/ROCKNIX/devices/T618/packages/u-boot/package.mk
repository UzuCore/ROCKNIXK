# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="u-boot"
PKG_VERSION="rg405-dual-android-v4-2"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/UzuCore/ROCKNIXK"
PKG_DEPENDS_TARGET="toolchain linux mkbootimg:host t618-avbtool:host"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="RG405M/RG405V boot carriers for an unlocked, stock UMS512 boot chain"

# Stock UMS512 U-Boot truncates the beginning of the Android boot-image cmdline.
# Keep the sacrificial padding before the ROCKNIX arguments on both model profiles.
T618_CMDLINE_PAD="t618_uboot_cmdline_pad_0000000000000000000000000000000000000000000000000000"

make_target() {
  for model in RG405M RG405V; do \
    profile=$(echo ${model} | tr '[:upper:]' '[:lower:]'); \
    rotate=""; \
    model_cmdline="${EXTRA_CMDLINE}"; \
    if [ "${model}" = "RG405M" ]; then model_cmdline="rootwait quiet loglevel=0 systemd.show_status=false fbcon=rotate:1 panic=10"; fi; \
    if [ "${model}" = "RG405V" ]; then model_cmdline="rootwait quiet loglevel=0 systemd.show_status=false fbcon=rotate:1 panic=10"; fi; \
    python3 ${PKG_DIR}/pack_boot.py \
      --kernel "$(get_install_dir linux)/.image/Image" \
      --dtb "$(get_install_dir linux)/usr/share/bootloader/device_trees/ums512-${profile}.dtb" \
      --mkbootimg "${TOOLCHAIN}/mkbootimg/mkbootimg.py" \
      --avbtool "${TOOLCHAIN}/t618-avbtool/avbtool.py" \
      --development-key "${TOOLCHAIN}/t618-avbtool/testkey_rsa4096.pem" \
      --vbmeta-template "${PKG_DIR}/vbmeta-chain-template.img" \
      --device-model "${model}" \
      --cmdline "${T618_CMDLINE_PAD} ${model_cmdline} ${rotate} boot=LABEL=${DISTRO_BOOTLABEL} disk=LABEL=${DISTRO_DISKLABEL}" \
      --output "${PKG_BUILD}/carriers/${profile}"; \
  done
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/bootloader/extlinux/rg405m ${INSTALL}/usr/share/bootloader/extlinux/rg405v
  cp ${PKG_BUILD}/carriers/rg405m/* ${INSTALL}/usr/share/bootloader/extlinux/rg405m/
  cp ${PKG_BUILD}/carriers/rg405v/* ${INSTALL}/usr/share/bootloader/extlinux/rg405v/
  cp ${PKG_DIR}/INSTALL.md ${PKG_DIR}/BOOT_PATH.md ${INSTALL}/usr/share/bootloader/extlinux/
  cp ${PKG_DIR}/installation_plan.py ${INSTALL}/usr/share/bootloader/extlinux/rg405m/
  cat >${INSTALL}/usr/share/bootloader/extlinux/extlinux.conf <<EOF
LABEL ROCKNIXK-RG405M
  LINUX /KERNEL
  FDT /device_trees/ums512-rg405m.dtb
  APPEND boot=LABEL=${DISTRO_BOOTLABEL} disk=LABEL=${DISTRO_DISKLABEL} rootwait quiet fbcon=rotate:1 panic=10
LABEL ROCKNIXK-RG405V
  LINUX /KERNEL
  FDT /device_trees/ums512-rg405v.dtb
  APPEND boot=LABEL=${DISTRO_BOOTLABEL} disk=LABEL=${DISTRO_DISKLABEL} rootwait quiet fbcon=rotate:1 panic=10
EOF
}
