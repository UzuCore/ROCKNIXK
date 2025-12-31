# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018-present Frank Hartung (supervisedthinking (@) gmail.com)

PKG_NAME="gst-plugins-ugly"
PKG_VERSION="$(get_pkg_version gstreamer)"
PKG_LICENSE="LGPL-2.1-or-later"
PKG_SITE="https://gstreamer.freedesktop.org/modules/gst-plugins-ugly.html"
PKG_URL="https://gstreamer.freedesktop.org/src/gst-plugins-ugly/${PKG_NAME}-${PKG_VERSION}.tar.xz"
PKG_DEPENDS_TARGET="toolchain gstreamer gst-plugins-base"
PKG_LONGDESC="Good GStreamer plugins and helper libraries"

PKG_MESON_OPTS_TARGET="-Dtests=disabled \
                       -Dgpl=enabled \
                       -Dx264=enabled \
                       -Dnls=disabled"

  # Fix missing dispmanx
  if [ "${DEVICE}" = "RPi4" -o "${DEVICE}" = "RPi2" ]; then
    PKG_MESON_OPTS_TARGET+=" -Drpicamsrc=disabled"
  fi

pre_configure_target() {
  export TARGET_LDFLAGS="${LDFLAGS} -lm -logg"
}

post_makeinstall_target() {
  # clean up
  safe_remove ${INSTALL}/usr/share
}
