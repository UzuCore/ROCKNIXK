# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="wxwidgets"
PKG_VERSION="b97aee419af105e5c724ead2917e1b04b9abb26a"
PKG_LICENSE="wxWindows Library Licence"
PKG_SITE="https://github.com/wxWidgets/wxWidgets"
PKG_URL="${PKG_SITE}.git"
PKG_DEPENDS_TARGET="toolchain zlib libpng libjpeg-turbo gdk-pixbuf gtk3 libaio"
PKG_LONGDESC="wxWidgets is a free and open source cross-platform C++ framework for writing advanced GUI applications using native controls."

PKG_CMAKE_OPTS_TARGET="-DCMAKE_SYSROOT=${SYSROOT_PREFIX} \
                       -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
