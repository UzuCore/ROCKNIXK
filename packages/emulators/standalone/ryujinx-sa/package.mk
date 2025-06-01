# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024 ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="ryujinx-sa"
PKG_LICENSE="GPLv2"
PKG_VERSION="1.3.1"
PKG_SITE="https://ryujinx.app/"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="Nintendo Switch 1 Emulator"
PKG_TOOLCHAIN="manual"

case ${TARGET_ARCH} in
  x86_64)
    PKG_URL="https://github.com/Ryubing/Stable-Releases/releases/download/${PKG_VERSION}/ryujinx-${PKG_VERSION}-x64.AppImage"
  ;;
  aarch64)
    PKG_URL="https://github.com/Ryubing/Stable-Releases/releases/download/${PKG_VERSION}/ryujinx-${PKG_VERSION}-arm64.AppImage"
  ;;
esac

makeinstall_target() {
  export STRIP=true
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/${PKG_NAME}-${PKG_VERSION}.AppImage ${INSTALL}/usr/bin/${PKG_NAME}
  #cp -rf ${PKG_DIR}/scripts/start_ryujinx.sh ${INSTALL}/usr/bin
  chmod 755 ${INSTALL}/usr/bin/*
  mkdir -p ${INSTALL}/usr/config/ryujinx
  #cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/ryujinx/
}