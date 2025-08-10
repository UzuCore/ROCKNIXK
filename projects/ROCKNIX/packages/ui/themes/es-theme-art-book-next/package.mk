# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="es-theme-art-book-next"
PKG_VERSION="f64efaf57c2be422be0ab2fb924bc0a962ce23d4"
PKG_LICENSE="CUSTOM"
PKG_SITE="https://github.com/UzuCore/es-theme-art-book-dc"
PKG_URL="https://github.com/UzuCore/es-theme-art-book-dc/archive/${PKG_VERSION}.tar.gz"
PKG_LONGDESC="Art Book Next"
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/themes/${PKG_NAME}
    cp -rf * ${INSTALL}/usr/share/themes/${PKG_NAME}
    rm -rf ${INSTALL}/usr/share/themes/${PKG_NAME}/_inc/systems/{artwork-circuit,artwork-classic,artwork-nintendont,artwork-noir,artwork-outline}
    sed -i '/<include name="\(noir\|nintendont\|circuit\|outline\)"/d' ${INSTALL}/usr/share/themes/${PKG_NAME}/theme.xml
}
