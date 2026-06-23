# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="sdl2notify"
PKG_VERSION="v1.0"
PKG_LICENSE="GPLv2"
PKG_DEPENDS_TARGET="toolchain SDL2 SDL2_ttf"
PKG_LONGDESC="SDL2 notification app"
PKG_TOOLCHAIN="make"

pre_make_target() {
  cp -f ${PKG_DIR}/Makefile ${PKG_BUILD}
  cp -f ${PKG_DIR}/sdl2notify.cpp ${PKG_BUILD}
  CFLAGS+=" -I${SYSROOT_PREFIX}/usr/include/SDL2 -D_REENTRANT"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/sdl2notify ${INSTALL}/usr/bin
  chmod 0755 ${INSTALL}/usr/bin/*
}
