# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2014 Stephan Raue (stephan@openelec.tv)

PKG_NAME="libtheora"
PKG_VERSION="1.1.1"
PKG_LICENSE="GPL"
PKG_SITE="https://github.com/xiph/theora"
PKG_URL="${PKG_SITE}/archive/refs/tags/v${PKG_VERSION}.tar.gz"
PKG_BUILD_DEPENDS="libogg libvorbis host-pkgconf"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="On2's VP3 codec"
PKG_TOOLCHAIN="autotools"
export CFLAGS="${CFLAGS} -fPIC"

# package specific configure options
PKG_CONFIGURE_OPTS_TARGET="--disable-examples --disable-oggtest --disable-vorbistest --disable-sdltest --enable-static --enable-shared --disable-spec"

post_makeinstall_target() {
  mkdir -p ${SYSROOT_PREFIX}/usr/lib/pkgconfig
  cat > ${SYSROOT_PREFIX}/usr/lib/pkgconfig/theora.pc << "EOF"
prefix=/usr
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: thoera
Description: MPEG video decoder
Requires:
Version: 1.1.1
Libs: -L${libdir} -ltheora
Cflags: -I${includedir}
EOF
}
