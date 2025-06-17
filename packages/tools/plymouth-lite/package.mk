# SPDX-License-Identifier: GPL-2.0-or-laterAdd commentMore actions
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)

PKG_NAME="plymouth-lite"
PKG_VERSION="0.6.0"
PKG_LICENSE="GPL"
PKG_SITE="http://www.meego.com"
PKG_URL="http://sources.libreelec.tv/devel/${PKG_NAME}-${PKG_VERSION}.tar.bz2"
PKG_DEPENDS_INIT="toolchain gcc:init libpng"
PKG_LONGDESC="Boot splash screen based on Fedora's Plymouth code"
PKG_TOOLCHAIN="make"

pre_configure_init() {
  # plymouth-lite dont support to build in subdirs
  cd ${PKG_BUILD}
    rm -rf .${TARGET_NAME}-init
}

makeinstall_init() {
  mkdir -p ${INSTALL}/usr/bin
    cp ply-image ${INSTALL}/usr/bin

  mkdir -p ${INSTALL}/splash
    cp -f ${DISTRO_DIR}/${DISTRO}/splash/*.png ${INSTALL}/splash
}
