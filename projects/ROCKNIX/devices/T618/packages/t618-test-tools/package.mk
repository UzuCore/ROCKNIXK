# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="t618-test-tools"
PKG_VERSION="1"
PKG_LICENSE="GPL-2.0-or-later"
PKG_TOOLCHAIN="manual"
PKG_DEPENDS_TARGET="toolchain foot evtest alsa-utils"
PKG_LONGDESC="Read-only diagnostics menu for the emulator-free RG405M test image"

makeinstall_target() {
  install -Dm755 ${PKG_DIR}/sources/t618-test-status ${INSTALL}/usr/bin/t618-test-status
  install -Dm755 ${PKG_DIR}/sources/hardware-info.sh "${INSTALL}/usr/config/modules/T618 Hardware Information.sh"
  install -Dm644 ${PKG_DIR}/sources/test-build.txt ${INSTALL}/etc/t618-test-build
}
