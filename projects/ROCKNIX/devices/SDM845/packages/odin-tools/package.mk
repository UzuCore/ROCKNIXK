# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="odin-tools"
PKG_VERSION="1.0"
PKG_LICENSE="GPL-2.0-or-later"
PKG_TOOLCHAIN="cmake"
PKG_DEPENDS_TARGET="toolchain qt6 SDL2 Python3 noto-sans-cjk"
PKG_LONGDESC="Odin LED graphical settings"
PKG_CMAKE_OPTS_TARGET="-DQT_HOST_PATH=${TOOLCHAIN}/usr/local/qt6"
