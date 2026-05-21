# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-present Shanti Gilbert (https://github.com/shantigilbert)
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

PKG_NAME="scummvm-grim"
PKG_VERSION="2.8.1"
PKG_LICENSE="GPL2"
PKG_SITE="https://github.com/british-choi/scummvm"
PKG_URL="${PKG_SITE}/archive/refs/heads/branch-2-8-1.tar.gz"
PKG_BUILD="${BUILD}/scummvm-branch-2-8-1"
PKG_DEPENDS_TARGET="toolchain SDL2 SDL2_net freetype fluidsynth soundfont-generaluser pipewire libmad libtheora faad2"
PKG_LONGDESC="Script Creation Utility for Grim Fandango Virtual Machine"

pre_configure_target() {
  sed -i "s|sdl-config|sdl2-config|g" ${PKG_BUILD}/configure
  TARGET_CONFIGURE_OPTS="--host=${TARGET_NAME} --backend=sdl --disable-alsa --with-sdl-prefix=${SYSROOT_PREFIX}/usr/bin --disable-debug --enable-release --enable-vkeybd --enable-optimizations --disable-all-engines --enable-engine=grim"
}

post_makeinstall_target() {
  mkdir -p ${INSTALL}/usr/config/scummvm-grim/
  cp -rf ${PKG_DIR}/config/* ${INSTALL}/usr/config/scummvm-grim/

  mkdir -p ${INSTALL}/usr/config/scummvm-grim/themes
  cp -rf ${PKG_BUILD}/gui/themes ${INSTALL}/usr/config/scummvm-grim/themes

  mv ${INSTALL}/usr/local/bin ${INSTALL}/usr/
  mv ${INSTALL}/usr/bin/scummvm ${INSTALL}/usr/bin/scummvm-grim
  chmod 755 ${INSTALL}/usr/bin/*

  for i in appdata applications doc icons man; do
    rm -rf "${INSTALL}/usr/local/share/${i}"
  done
  mv ${INSTALL}/usr/local/share/scummvm ${INSTALL}/usr/local/share/scummvm-grim
}