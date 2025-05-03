# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2009-2016 Stephan Raue (stephan@openelec.tv)
# Copyright (C) 2018-present Team LibreELEC (https://libreelec.tv)

PKG_NAME="diskdev_cmds"
PKG_VERSION="332.14"
PKG_SHA256="68e98a4dbedf42e1e89f221322ec9a10db49fcb27615194b6d15512861549a2a"
PKG_LICENSE="APSL"
PKG_SITE="http://src.gnu-darwin.org/DarwinSourceArchive/expanded/diskdev_cmds/"
PKG_URL="http://www.opensource.apple.com/tarballs/diskdev_cmds/${PKG_NAME}-${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain openssl"
PKG_LONGDESC="The fsck and mkfs utliities for hfs and hfsplus filesystems."

pre_make_target() {
  PKG_MAKE_OPTS_TARGET="-f Makefile.lnx CC=${CC}"

  export CFLAGS="${TARGET_CFLAGS} -g3 -Wall -I${PKG_BUILD}/include -DDEBUG_BUILD=0 -D_FILE_OFFSET_BITS=64 -D LINUX=1 -D BSD=1"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/sbin
    cp fsck_hfs.tproj/fsck_hfs ${INSTALL}/usr/sbin
      ln -sf fsck_hfs ${INSTALL}/usr/sbin/fsck.hfs
      ln -sf fsck_hfs ${INSTALL}/usr/sbin/fsck.hfsplus
}

make_init() {
  : # we reuse make_target()
}
