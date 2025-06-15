# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022-24 JELOS (https://github.com/JustEnoughLinuxOS)
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="es-theme-art-book-next"
PKG_VERSION="435db0884ac4b5544b98ee0097cf8afb96dc4ef2"
PKG_LICENSE="CUSTOM"
PKG_SITE="https://github.com/UzuCore/es-theme-art-book-dc"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_LONGDESC="Art Book Next"
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/themes/${PKG_NAME}
    cp -rf * ${INSTALL}/usr/share/themes/${PKG_NAME}

    rm -rf ${INSTALL}/usr/share/themes/${PKG_NAME}/_inc/systems/artwork-circuit
    rm -rf ${INSTALL}/usr/share/themes/${PKG_NAME}/_inc/systems/artwork-classic
    rm -rf ${INSTALL}/usr/share/themes/${PKG_NAME}/_inc/systems/artwork-noir
    rm -rf ${INSTALL}/usr/share/themes/${PKG_NAME}/_inc/systems/artwork-outline
}
