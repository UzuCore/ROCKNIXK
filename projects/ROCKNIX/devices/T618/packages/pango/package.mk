# SPDX-License-Identifier: GPL-2.0-or-later
. ${ROOT}/packages/graphics/pango/package.mk

pre_configure_target() {
  PKG_MESON_OPTS_TARGET="-Ddocumentation=false -Dintrospection=disabled \
                         -Dbuild-testsuite=false -Dbuild-examples=false \
                         --wrap-mode=nofallback"
}
