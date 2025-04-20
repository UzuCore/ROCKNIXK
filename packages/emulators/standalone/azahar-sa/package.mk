# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="azahar-sa"
PKG_VERSION="a6782e8a136cba9b99bd19627e0a669678733338"
PKG_LICENSE="GPL"
PKG_SITE="https://github.com/azahar-emu/azahar"
PKG_URL="${PKG_SITE}.git"
PKG_DEPENDS_TARGET="toolchain ffmpeg mesa SDL2 boost zlib libusb boost zstd control-gen spirv-tools qt6"
PKG_LONGDESC="Azahar - Nintendo 3DS emulator"
PKG_TOOLCHAIN="cmake"

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

PKG_CMAKE_OPTS_TARGET+=" -DENABLE_QT_TRANSLATION=OFF \
                         -DENABLE_QT=ON \
                         -DENABLE_SDL2=ON \
                         -DENABLE_SDL2_FRONTEND=OFF \
                         -DENABLE_TESTS=OFF \
                         -DENABLE_ROOM=OFF \
                         -DUSE_DISCORD_PRESENCE=OFF \
                         -DENABLE_VULKAN=ON \
                         -DENABLE_OPENGL=ON"

makeinstall_target() {
  echo "PKG_BUILD=${PKG_BUILD}"
  ls -l ${PKG_BUILD}
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/.${TARGET_NAME}/bin/MinSizeRel/azahar ${INSTALL}/usr/bin/azahar
  cp ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin

  mkdir -p ${INSTALL}/usr/config/azahar
  cp -rf ${PKG_DIR}/config/common/* ${INSTALL}/usr/config/azahar
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/azahar
}
