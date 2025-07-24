# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="eden-sa"
PKG_LICENSE="GPLv3"
PKG_LONGDESC="Eden is the world's most popular open-source Nintendo Switch emulator, forked from the Yuzu emulator."
PKG_TOOLCHAIN="manual"
PKG_SITE="https://github.com/pflyly/eden-nightly"
PKG_VERSION="2025-07-18-d42d379733"
PKG_REL_VERSION="27470"

case ${TARGET_ARCH} in
  x86_64)
    PKG_URL="${PKG_SITE}/releases/download/${PKG_VERSION}/Eden-${PKG_REL_VERSION}-Common-light-x86_64_v3.AppImage"
  ;;
  aarch64)
    PKG_URL="${PKG_SITE}/releases/download/${PKG_VERSION}/Eden-${PKG_REL_VERSION}-linux-light-aarch64.AppImage"
  ;;
esac

makeinstall_target() {
  export STRIP=true
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/${PKG_NAME}-${PKG_VERSION}.AppImage ${INSTALL}/usr/bin/eden
  cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
  chmod 755 ${INSTALL}/usr/bin/*
  mkdir -p ${INSTALL}/usr/config/eden
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/eden/
}
