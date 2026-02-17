# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="dejavu"
PKG_VERSION="2.5"
PKG_LICENSE="Bitstream"
PKG_SITE="https://github.com/naver/nanumfont"
PKG_URL="${PKG_SITE}/releases/download/VER${PKG_VERSION}/NanumGothicCoding-${PKG_VERSION}.zip"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="The DejaVu fonts are a font family based upon Bitstream Vera v1.10."
PKG_TOOLCHAIN="manual"

pre_make_target() {
    ### Extract will not correctly extract this package.
    unzip -oq ${SOURCES}/${PKG_NAME}/${PKG_NAME}-${PKG_VERSION}.zip -d ${PKG_BUILD}
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/fonts/truetype/dejavu/
  cp -rf ${PKG_BUILD}/NanumGothicCoding.ttf ${INSTALL}/usr/share/fonts/truetype/dejavu/
}
