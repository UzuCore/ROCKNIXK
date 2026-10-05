# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="bios-downloader"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-or-later"
PKG_TOOLCHAIN="manual"
PKG_DEPENDS_TARGET="toolchain Python3 SDL2 SDL2_ttf noto-sans-cjk dejavu"
PKG_LONGDESC="Responsive BIOS downloader with graphical progress"

makeinstall_target() {
  mkdir -p "${INSTALL}/usr/share/bios-downloader"
  cp "${PKG_DIR}/sources/"*.py "${INSTALL}/usr/share/bios-downloader/"
}
