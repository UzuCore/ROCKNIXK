# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022-present JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="vita3k-sa"
PKG_VERSION="3935"
PKG_LICENSE="GPLv2"
PKG_SITE="https://github.com/Vita3K/Vita3K-builds"
PKG_DEPENDS_TARGET="toolchain SDL2 SDL2_image zlib libogg libvorbis gtk3 openssl ffmpeg"
PKG_LONGDESC="Vita3K appimage"
PKG_TOOLCHAIN="manual"
PKG_URL="${PKG_SITE}/releases/download/${PKG_VERSION}/Vita3K-aarch64.AppImage"

makeinstall_target() {
  # Redefine strip or the AppImage will be stripped rendering it unusable.
  export STRIP=true
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/${PKG_NAME}-${PKG_VERSION}.AppImage ${INSTALL}/usr/bin/Vita3K
  cp ${PKG_DIR}/scripts/*vita3k.sh ${INSTALL}/usr/bin
  chmod 0755 ${INSTALL}/usr/bin/*

  mkdir -p ${INSTALL}/usr/config/Vita3K/launcher
  cp ${PKG_DIR}/scripts/start_vita3k.sh ${INSTALL}/usr/config/Vita3K/launcher/_Start\ Vita3K.sh
  cp ${PKG_DIR}/scripts/scan_vita3k.sh ${INSTALL}/usr/config/Vita3K/launcher/_Scan\ Vita\ Games.sh
  cp ${PKG_DIR}/scripts/Install\ Vita3k\ Content.sh ${INSTALL}/usr/config/Vita3K/launcher/_Install\ Vita3k\ Content.sh
  cp ${PKG_DIR}/scripts/Install\ Vita3k\ FW.sh ${INSTALL}/usr/config/Vita3K/launcher/_Install\ Vita3k\ FW.sh
  chmod 0755 ${INSTALL}/usr/config/Vita3K/launcher/*sh

  cp ${PKG_DIR}/sources/vita-gamelist.txt ${INSTALL}/usr/config/Vita3K
  cp ${PKG_DIR}/sources/config.yml ${INSTALL}/usr/config/Vita3K
}
