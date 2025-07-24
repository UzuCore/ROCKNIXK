# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2014 Stephan Raue (stephan@openelec.tv)

PKG_NAME="libtheora"
PKG_VERSION="1.1.1"
PKG_SHA256="b6ae1ee2fa3d42ac489287d3ec34c5885730b1296f0801ae577a35193d3affbc"
PKG_LICENSE="GPL"
PKG_SITE="http://downloads.xiph.org/releases/theora"
PKG_URL="${PKG_SITE}/${PKG_NAME}-${PKG_VERSION}.tar.bz2"
PKG_BUILD_DEPENDS="libogg libvorbis host-pkgconf"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="On2's VP3 codec"
PKG_TOOLCHAIN="autotools"

# package specific configure options
PKG_CONFIGURE_OPTS_TARGET="--disable-examples --disable-oggtest --disable-vorbistest --disable-sdltest --enable-static --disable-shared --disable-spec"

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
