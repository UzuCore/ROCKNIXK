# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="grim"
PKG_VERSION="1.5.0"
PKG_LICENSE="MIT"
PKG_SITE="https://wayland.emersion.fr/grim/"
PKG_URL="https://gitlab.freedesktop.org/emersion/grim/-/archive/v${PKG_VERSION}/${PKG_NAME}-v${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain wayland pixman libpng"
PKG_LONGDESC="Grab images from a Wayland compositor"
