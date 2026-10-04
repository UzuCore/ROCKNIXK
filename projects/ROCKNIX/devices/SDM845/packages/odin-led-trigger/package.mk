# SPDX-License-Identifier: GPL-2.0-only
PKG_NAME="odin-led-trigger"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-only"
PKG_TOOLCHAIN="manual"
PKG_IS_KERNEL_PKG="yes"
PKG_DEPENDS_TARGET="toolchain linux"
PKG_LONGDESC="AYN Odin synchronized GPIO LED scan trigger"

make_target() {
  kernel_make -C "$(kernel_path)" M="${PKG_BUILD}" modules
}

makeinstall_target() {
  mkdir -p "${INSTALL}/$(get_full_module_dir)/kernel/drivers/leds/trigger"
  cp "${PKG_BUILD}/ledtrig_odin_scan.ko" "${INSTALL}/$(get_full_module_dir)/kernel/drivers/leds/trigger/"
}
