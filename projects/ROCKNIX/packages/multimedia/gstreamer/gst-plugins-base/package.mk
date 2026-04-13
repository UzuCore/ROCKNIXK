# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="gst-plugins-base"
PKG_VERSION="$(get_pkg_version gstreamer)"
PKG_LICENSE="GPL-2.1-or-later"
PKG_SITE="https://gstreamer.freedesktop.org/modules/gst-plugins-base.html"
PKG_URL="https://gstreamer.freedesktop.org/src/gst-plugins-base/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_TARGET="toolchain gstreamer"
PKG_LONGDESC="Base GStreamer plugins and helper libraries"
PKG_BUILD_FLAGS="-gold"

pre_configure_target() {
  PKG_MESON_OPTS_TARGET="-Dgl=enabled \
                         -Dexamples=disabled \
                         -Dgl-graphene=disabled \
                         -Dtests=disabled \
                         -Dpackage-name=gst-plugins-base \
                         -Dpackage-origin=rocknix.org \
                         -Ddoc=disabled \
                         -Dnls=disabled"
}

post_makeinstall_target() {
  # clean up
  safe_remove ${SYSROOT_PREFIX}/usr/include/GL
  safe_remove ${INSTALL}/usr/include
  safe_remove ${INSTALL}/usr/lib/pkgconfig
  safe_remove ${INSTALL}/usr/share
}
