# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="citron-sa"
PKG_LICENSE="GPLv2"
PKG_DEPENDS_TARGET="toolchain SDL2 boost libevdev libdrm ffmpeg zlib libpng lzo libusb zstd ecm openal-soft pulseaudio alsa-lib llvm qt6 libfmt vulkan-loader vulkan-headers"

PKG_LONGDESC="Citron is a high-performance and easy-to-use emulator, tailored for enthusiasts and developers alike."
PKG_TOOLCHAIN="cmake"
PKG_SITE="https://git.citron-emu.org/citron/emu"
PKG_URL="${PKG_SITE}.git"
PKG_VERSION="51800e249bc44bd13b528220a8e064c3744c05d1"

PKG_CMAKE_OPTS_TARGET+="-DENABLE_QT=ON \
                    -DENABLE_QT6=ON \
                    -DUSE_SYSTEM_QT=ON \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DCITRON_ROOM=OFF \
                    -DCITRON_USE_BUNDLED_SDL2=OFF \
                    -DCITRON_USE_BUNDLED_QT=OFF \
                    -DCITRON_TESTS=OFF \
                    -DENABLE_SDL2=ON \
                    -DARCHITECTURE_x86_64=OFF \
                    -DARCHITECTURE_arm64=ON \
                    -DBUILD_SHARED_LIBS=OFF \
                    -DENABLE_WEB_SERVICE=OFF \
                    -DCITRON_USE_BUNDLED_FFMPEG=OFF \
                    -DCITRON_USE_FASTER_LD=ON \
                    -DCITRON_USE_QT_MULTIMEDIA=OFF \
                    -DCITRON_USE_QT_WEB_ENGINE=OFF \
                    -DCITRON_ENABLE_LTO=ON \
                    -DUSE_DISCORD_PRESENCE=OFF"

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
