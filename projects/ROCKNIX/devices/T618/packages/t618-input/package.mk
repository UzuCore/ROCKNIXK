# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="t618-input"
PKG_VERSION="2"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/UzuCore/ROCKNIXK"
PKG_DEPENDS_TARGET="toolchain linux"
PKG_TOOLCHAIN="manual"
PKG_IS_KERNEL_PKG="yes"
PKG_LONGDESC="RG405M/RG405V SC2730 processed-ADC input adapter"

make_target() {
  kernel_make -C $(kernel_path) M=${PKG_BUILD} modules
}

makeinstall_target() {
  mkdir -p ${INSTALL}/$(get_full_module_dir)/${PKG_NAME}
  cp rg405m-analog.ko ${INSTALL}/$(get_full_module_dir)/${PKG_NAME}/
}
