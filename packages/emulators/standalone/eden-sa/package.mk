# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="eden-sa"
PKG_LICENSE="GPLv3"
PKG_DEPENDS_TARGET="toolchain SDL2 boost libevdev libdrm ffmpeg zlib libpng lzo libusb zstd ecm openal-soft pulseaudio alsa-lib llvm qt6 libfmt"
PKG_LONGDESC="Eden is the world's most popular open-source Nintendo Switch emulator, forked from the Yuzu emulator."
PKG_TOOLCHAIN="cmake"
PKG_SITE="https://git.eden-emu.dev/eden-emu/eden"
PKG_URL="${PKG_SITE}.git"
PKG_VERSION="fb3988a78a54b4a75090594a6d374ba819e0afcb"

if [ ! "${OPENGL}" = "no" ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGL} glu libglvnd"
fi

if [ "${OPENGLES_SUPPORT}" = yes ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGLES}"
fi

if [ "${VULKAN_SUPPORT}" = "yes" ]
then
  PKG_DEPENDS_TARGET+=" vulkan-loader vulkan-headers"
fi

PKG_CMAKE_OPTS_TARGET+="-DENABLE_QT=ON \
                    -DENABLE_QT6=ON \
                    -DUSE_SYSTEM_QT=ON \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DBUILD_SHARED_LIBS=OFF \
                    -DENABLE_SDL2=ON \
                    -DYUZU_USE_EXTERNAL_SDL2=OFF \
                    -DENABLE_QT=ON \
                    -DENABLE_QT_TRANSLATION=ON \
                    -DUSE_DISCORD_PRESENCE=OFF \
                    -DYUZU_TESTS=OFF \
                    -DYUZU_ENABLE_LTO=ON \
                    -DYUZU_USE_FASTER_LD=ON \
                    -DENABLE_WEB_SERVICE=OFF \
                    -DYUZU_DOWNLOAD_ANDROID_VVL=OFF \
                    -DYUZU_ENABLE_PORTABLE=OFF \
                    -DYUZU_USE_BUNDLED_FFMPEG=OFF"

#pre_configure_target() {
  #echo ${PKG_DEPENDS_TARGET}
  #sleed 1d
#}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/.${TARGET_NAME}/bin/eden  ${INSTALL}/usr/bin/
  cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
  chmod 755 ${INSTALL}/usr/bin/*
  mkdir -p ${INSTALL}/usr/config/eden
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/eden/
}
