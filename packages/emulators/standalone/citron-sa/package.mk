# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="citron-sa"
PKG_LICENSE="GPLv2"
PKG_DEPENDS_TARGET="toolchain SDL2 boost libevdev libdrm ffmpeg zlib libpng lzo libusb zstd ecm openal-soft pulseaudio alsa-lib llvm qt6 libfmt"

PKG_LONGDESC="Citron is a high-performance and easy-to-use emulator, tailored for enthusiasts and developers alike."
PKG_TOOLCHAIN="cmake"
PKG_SITE="https://git.citron-emu.org/citron/emu"
PKG_URL="${PKG_SITE}.git"
PKG_VERSION="51800e249bc44bd13b528220a8e064c3744c05d1"

if [ "${OPENGL_SUPPORT}" = "yes" ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGL} glu libglvnd"
fi

if [ "${VULKAN_SUPPORT}" = "yes" ]
then
  PKG_DEPENDS_TARGET+=" vulkan-loader vulkan-headers glslang"
fi

PKG_CMAKE_OPTS_TARGET+="-DENABLE_QT=ON \
                    -DENABLE_QT6=ON \
                    -DUSE_SYSTEM_QT=ON \
		    -DCMAKE_BUILD_TYPE=Release \
                    -DBUILD_SHARED_LIBS=OFF \
                    -DENABLE_SDL2=ON \
                    -DCITRON_USE_EXTERNAL_SDL2=OFF \
                    -DENABLE_QT=ON \
                    -DENABLE_QT_TRANSLATION=ON \
                    -DUSE_DISCORD_PRESENCE=OFF \
                    -DCITRON_TESTS=OFF \
                    -DENABLE_WEB_SERVICE=OFF \
                    -DCITRON_DOWNLOAD_ANDROID_VVL=OFF \
                    -DCITRON_ENABLE_PORTABLE=OFF \
                    -DCMAKE_CXX_FLAGS=-Wno-error=shadow"

pre_configure_target() {
  CFLAGS=$(echo ${CFLAGS} | sed -e "s|-Ofast|-O3|")
  CXXFLAGS=$(echo ${CXXFLAGS} | sed -e "s|-Ofast|-O3|")
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/.${TARGET_NAME}/bin/citron  ${INSTALL}/usr/bin/
		cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
    chmod +x ${INSTALL}/usr/bin/start_citron.sh

  mkdir -p ${INSTALL}/usr/config/citron
    cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/citron
}
