# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="u-boot"
PKG_VERSION="rg405m-android-v4-1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/UzuCore/ROCKNIXK"
PKG_DEPENDS_TARGET="toolchain linux mkbootimg:host t618-avbtool:host"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="RG405M boot carriers for an unlocked, stock UMS512 boot chain"

# Stock UMS512 U-Boot prepends ~1.4 KiB of androidboot.* args and drops the first ~20 characters
# of the boot-image cmdline (seen on device: boot= -> oot=). Sacrificial padding goes first and
# boot=/disk= go last. Verified with the dbg3 bring-up image.
T618_CMDLINE_PAD="t618_uboot_cmdline_pad_0000000000000000000000000000000000000000000000000000"

make_target() {
  python3 ${PKG_DIR}/pack_boot.py \
    --kernel "$(get_install_dir linux)/.image/Image" \
    --dtb "$(get_install_dir linux)/usr/share/bootloader/device_trees/ums512-rg405m.dtb" \
    --mkbootimg "${TOOLCHAIN}/mkbootimg/mkbootimg.py" \
    --avbtool "${TOOLCHAIN}/t618-avbtool/avbtool.py" \
    --development-key "${TOOLCHAIN}/t618-avbtool/testkey_rsa4096.pem" \
    --vbmeta-template "${PKG_DIR}/vbmeta-chain-template.img" \
    --cmdline "${T618_CMDLINE_PAD} ${EXTRA_CMDLINE} boot=LABEL=${DISTRO_BOOTLABEL} disk=LABEL=${DISTRO_DISKLABEL}" \
    --output "${PKG_BUILD}/carriers"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/bootloader/extlinux/rg405m
  cp ${PKG_BUILD}/carriers/* ${INSTALL}/usr/share/bootloader/extlinux/rg405m/
  cp ${PKG_DIR}/INSTALL.md ${PKG_DIR}/BOOT_PATH.md ${PKG_DIR}/installation_plan.py ${PKG_DIR}/../../SAFETY.md ${INSTALL}/usr/share/bootloader/extlinux/rg405m/
  cat >${INSTALL}/usr/share/bootloader/extlinux/extlinux.conf <<EOF
LABEL ROCKNIXK-RG405M
  LINUX /KERNEL
  FDT /device_trees/ums512-rg405m.dtb
  APPEND boot=LABEL=${DISTRO_BOOTLABEL} disk=LABEL=${DISTRO_DISKLABEL} ${EXTRA_CMDLINE}
EOF
}
