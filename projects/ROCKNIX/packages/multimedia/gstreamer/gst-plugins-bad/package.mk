# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022-present Team LibreELEC (https://libreelec.tv)
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="gst-plugins-bad"
PKG_VERSION="$(get_pkg_version gstreamer)"
PKG_LICENSE="LGPL-2.1-or-later"
PKG_SITE="https://gstreamer.freedesktop.org/modules/gst-plugins-bad.html"
PKG_URL="https://gstreamer.freedesktop.org/src/gst-plugins-bad/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_TARGET="toolchain gstreamer gst-plugins-base"
PKG_LONGDESC="GStreamer Bad Plug-ins is a set of plug-ins that aren't up to par compared to the rest."

PKG_MESON_OPTS_TARGET="-Dgst_play_tests=false \
                         -Dwebp=disabled \
                         -Dbluez=disabled \
                         -Dgpl=enabled \
                         -Dhls=disabled \
                         -Dsctp-internal-usrsctp=disabled \
                         -Dexamples=disabled \
                         -Dtests=disabled \
                         -Dintrospection=disabled \
                         -Dnls=disabled \
                         -Dorc=disabled \
                         -Dpackage-name=gst-plugins-bad \
                         -Dpackage-origin=rocknix.org \
                         -Ddoc=disabled"

pre_configure_target() {
  export TARGET_LDFLAGS="${LDFLAGS} -lm"
}

post_makeinstall_target() {
  # clean up
  safe_remove ${INSTALL}/usr/include
  safe_remove ${INSTALL}/usr/lib/pkgconfig
  safe_remove ${INSTALL}/usr/share
}
