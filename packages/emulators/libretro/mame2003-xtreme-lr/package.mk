# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-present Shanti Gilbert (https://github.com/shantigilbert)
# Maintenance 2020 351ELEC team (https://github.com/fewtarius/351ELEC)
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="mame2003-xtreme-lr"
PKG_VERSION="cc977122f26b1580bc1aa269f8737294a96f1274"
PKG_LICENSE="MAME"
PKG_SITE="https://github.com/UzuCore/mame2003-xtreme"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="MAME 2003 Xtreme-Amped"

PKG_TOOLCHAIN="make"

make_target() {
  make ARCH="" CC="${CC}" NATIVE_CC="${CC}" LD="${CC}"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
  cp km_mame2003_xtreme_amped_libretro.so ${INSTALL}/usr/lib/libretro/
}
