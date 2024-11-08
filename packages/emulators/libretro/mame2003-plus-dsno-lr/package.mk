# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-present Shanti Gilbert (https://github.com/shantigilbert)
# Maintenance 2020 351ELEC team (https://github.com/fewtarius/351ELEC)
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="mame2003-plus-dsno-lr"
PKG_VERSION="2110b9cd6a2533ec7d947ce2fef6e8279fce4c33"
PKG_LICENSE="MAME"
PKG_SITE="https://github.com/UzuCore/mame2003-plus-dsno-libretro"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="MAME - Multiple Arcade Machine Emulator"

PKG_TOOLCHAIN="make"

make_target() {
  make ARCH="" CC="${CC}" NATIVE_CC="${CC}" LD="${CC}"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
  cp mame2003_plus_libretro.so ${INSTALL}/usr/lib/libretro/mame2003_plus-dsno_libretro.so
}
