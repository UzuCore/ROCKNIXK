# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="box64"
PKG_VERSION="129fc83e8d962ddae9b0090f1117e7ec1d36e795" # 260518
PKG_ARCH="aarch64"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/ptitSeb/box64"
PKG_URL="${PKG_SITE}.git"
PKG_DEPENDS_TARGET="toolchain ncurses SDL3 cabextract libXss libXdmcp libXft gtk2"
PKG_LONGDESC="Box64 lets you run x86_64 Linux programs (such as games) on non-x86_64 Linux systems, like ARM."
PKG_TOOLCHAIN="cmake"

PKG_CMAKE_OPTS_TARGET="-DBOX32_BINFMT=ON -DBOX32=ON -DARM_DYNAREC=ON"

case ${DEVICE} in
  RK3588)
    PKG_CMAKE_OPTS_TARGET+=" -DRK3588=On"
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

  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/.${TARGET_NAME}/box64 ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/tests/box64-bash ${INSTALL}/usr/bin/bash-x64

  mkdir -p ${INSTALL}/usr/config
    cp ${PKG_BUILD}/system/box64.box64rc ${INSTALL}/usr/config/box64.box64rc

  mkdir -p ${INSTALL}/etc
    ln -sf /storage/.config/box64.box64rc ${INSTALL}/etc/box64.box64rc

  mkdir -p ${INSTALL}/etc/binfmt.d
    cp -f ${PKG_DIR}/config/box64.conf ${INSTALL}/etc/binfmt.d/box64.conf
}
