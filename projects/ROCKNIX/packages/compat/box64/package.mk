# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="box64"
PKG_VERSION="f204ae69004fab7a34bbd5cf80104b2b8cbaf4d6"
PKG_SHA256="3a18256a29012e16867ab82ea3ce2350113a940e003b58671b6613fcc08e507b"
PKG_ARCH="aarch64"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/ptitSeb/box64"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain ncurses SDL2 cabextract libxss libXdmcp libXft gtk2"
PKG_LONGDESC="Box64 lets you run x86_64 Linux programs (such as games) on non-x86_64 Linux systems, like ARM."
PKG_TOOLCHAIN="cmake"

# Dynarce is only enabled with OPT target is set,
# enable here so its enabled on all platforms
PKG_CMAKE_OPTS_TARGET="-DCMAKE_BUILD_TYPE=Release \
                       -DARM_DYNAREC=On \
                       -DBOX32=On \
                       -DBOX32_BINFMT=On \
                       -DARM_DYNAREC=On"

case ${DEVICE} in
  RK3588)
    PKG_CMAKE_OPTS_TARGET+=" -DRK3588=On"
    ;;
  SM4450)
    PKG_CMAKE_OPTS_TARGET+=" -DSD888=On"
    ;;
  SM8250)
    PKG_CMAKE_OPTS_TARGET+=" -DSD865=On"
    ;;
  SM8550)
    PKG_CMAKE_OPTS_TARGET+=" -DSD8G2=On"
    ;;
esac

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/box64/lib
    cp ${PKG_BUILD}/x64lib/* ${INSTALL}/usr/share/box64/lib

  mkdir -p ${INSTALL}/usr/share/box32/lib
    cp ${PKG_BUILD}/x86lib/* ${INSTALL}/usr/share/box32/lib

  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/.${TARGET_NAME}/box64 ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/tests/box64-bash ${INSTALL}/usr/bin/bash-x64

  mkdir -p ${INSTALL}/usr/config
    cp ${PKG_BUILD}/system/box64.box64rc ${INSTALL}/usr/config/box64.box64rc

  mkdir -p ${INSTALL}/etc
    ln -sf /storage/.config/box64.box64rc ${INSTALL}/etc/box64.box64rc

  mkdir -p ${INSTALL}/etc/binfmt.d
    cp ${PKG_BUILD}/.${TARGET_NAME}/system/*.conf ${INSTALL}/etc/binfmt.d
}
