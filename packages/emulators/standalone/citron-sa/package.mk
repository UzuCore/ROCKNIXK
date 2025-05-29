# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="citron-sa"
PKG_LICENSE="GPLv2"
PKG_SITE="https://github.com/pkgforge-dev/Citron-AppImage/releases"
PKG_DEPENDS_TARGET="toolchain libevdev SDL2 qt6 mesa libcom-err"
PKG_LONGDESC="Citron Emulator appimage"
PKG_VERSION="0.6.1"
PKG_URL="${PKG_SITE}/download/v${PKG_VERSION}/Citron-v${PKG_VERSION}-anylinux-${TARGET_ARCH}.AppImage"
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  # Redefine strip or the AppImage will be stripped rendering it unusable.
  export STRIP=true
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/${PKG_NAME}-${PKG_VERSION}.AppImage ${INSTALL}/usr/bin/citron
  cp -rf ${PKG_DIR}/scripts/start_citron.sh ${INSTALL}/usr/bin
  chmod 755 ${INSTALL}/usr/bin/*
  mkdir -p ${INSTALL}/usr/config/citron
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/citron
}
