# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="chipone_tddi"
PKG_VERSION="ad4e075903c0c02728d22c3e87cc9f9e9a53be43"
PKG_LICENSE="GPL"
PKG_SITE="https://github.com/kevinkreiser/chipone_tddi"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_LONGDESC="chipone_tddi: ChipOne ICNL9922C TDDI touchscreen driver (KONKR Pocket FIT)"
PKG_TOOLCHAIN="manual"
PKG_IS_KERNEL_PKG="yes"

pre_make_target() {
  unset LDFLAGS
}

make_target() {
  kernel_make -C $(kernel_path) M=${PKG_BUILD}
}

makeinstall_target() {
  mkdir -p ${INSTALL}/$(get_full_module_dir)/${PKG_NAME}
    cp *.ko ${INSTALL}/$(get_full_module_dir)/${PKG_NAME}
}
