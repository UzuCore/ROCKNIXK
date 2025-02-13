# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="daedalus-sa"
PKG_VERSION="d6873d8c4dca9996d903e2ba0f43ee67264761c7"
PKG_LICENSE="GPLv2"
PKG_SITE="https://github.com/DaedalusX64/daedalus"
PKG_URL="${PKG_SITE}.git"
PKG_GIT_CLONE_BRANCH="GLES31_PORT"
PKG_DEPENDS_TARGET="toolchain libfmt SDL2 SDL2_ttf glew mesa"
PKG_LONGDESC="DaedalusX64 is a Nintendo 64 emulator for PSP, 3DS, Vita, Linux, macOS and Windows"
PKG_PATCH_DIRS+="${DEVICE}"

if [ "${ARCH}" = "aarch64" ]; then
  PKG_TOOLCHAIN="manual"
else
  PKG_TOOLCHAIN="cmake"
fi

if [ "${OPENGL_SUPPORT}" = "yes" ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGL}"
elif [ "${OPENGLES_SUPPORT}" = yes ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGLES}"
fi

makeinstall_target() {
  if [ "${ARCH}" = "aarch64" ]
  then
    mkdir -p ${INSTALL}/usr
    cp -r ${ROOT}/build.${DISTRO}-${DEVICE}.arm/daedalus-sa-${PKG_VERSION}/.install_pkg/usr/* ${INSTALL}/usr/
    chmod +x ${INSTALL}/usr/bin/*
  else
    mkdir -p ${INSTALL}/usr/bin
    mkdir -p ${INSTALL}/usr/config/DaedalusX64
    cp ${PKG_BUILD}/.${TARGET_NAME}/Source/daedalus ${INSTALL}/usr/config/DaedalusX64/daedalus
    cp ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
    cp ${PKG_DIR}/config/* ${INSTALL}/usr/config/DaedalusX64
    cp -r ${PKG_BUILD}/Data/* ${INSTALL}/usr/config/DaedalusX64
    cp -r ${PKG_BUILD}/Source/SysGL/HLEGraphics/n64.psh ${INSTALL}/usr/config/DaedalusX64
    chmod +x ${INSTALL}/usr/bin/*
  fi
}
