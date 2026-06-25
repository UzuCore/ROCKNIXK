# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="plutovg"
PKG_VERSION="1.3.2"
PKG_LICENSE="GPLv3"
PKG_SITE="https://github.com/sammycage/plutovg"
PKG_URL="${PKG_SITE}/archive/refs/tags/v${PKG_VERSION}.tar.gz"
PKG_LONGDESC="Tiny 2D vector graphics library in C"
PKG_DEPENDS_TARGET="toolchain"
PKG_TOOLCHAIN="cmake"

PKG_BUILD_FLAGS="+pic"
PKG_CMAKE_OPTS_TARGET="-DCMAKE_BUILD_TYPE=Release \
                        -DBUILD_SHARED_LIBS=ON \
                        -DINSTALL_DOCS=OFF"
