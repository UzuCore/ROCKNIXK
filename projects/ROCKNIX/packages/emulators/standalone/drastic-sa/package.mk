# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022-present JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="drastic-sa"
PKG_VERSION="1.0"
PKG_LICENSE="Proprietary:DRASTIC.pdf"
PKG_ARCH="aarch64"
PKG_URL="https://github.com/trngaje/advanced_drastic/releases/download/rocknix/advanced_drastic_rocknix_rgds_260425_v2.tar.gz"
PKG_DEPENDS_TARGET="toolchain rocknix-hotkey"
PKG_LONGDESC="Install Drastic Launcher script, will download bin on first run"
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
  chmod +x ${INSTALL}/usr/bin/start_drastic.sh
  
  mkdir -p ${INSTALL}/usr/config/drastic/config
  cp -rf ${PKG_BUILD}/* ${INSTALL}/usr/config/drastic/
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/drastic/config/
  cp -rf ${PKG_DIR}/config/drastic.gptk ${INSTALL}/usr/config/drastic/

  mkdir -p ${INSTALL}/usr/config/drastic/microphone
  cp -f ${PKG_DIR}/sources/microphone.wav ${INSTALL}/usr/config/drastic/microphone/
}

post_install() {
    case ${DEVICE} in
      RK3588)
        HOTKEY="export HOTKEY="guide""
      ;;
      *)
        HOTKEY=""
      ;;
    esac
    sed -e "s/@HOTKEY@/${HOTKEY}/g" \
        -i ${INSTALL}/usr/bin/start_drastic.sh
}
