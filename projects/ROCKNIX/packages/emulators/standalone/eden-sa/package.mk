# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="eden-sa"
PKG_VERSION="1be0cf6acd65e3e6b2d3f93279cfeb38d1a7d7ae" # v0.2.0
PKG_LICENSE="GPLv2"
PKG_DEPENDS_TARGET="toolchain llvm:host SDL3 boost libevdev libdrm ffmpeg zlib zstd alsa-lib qt6 libfmt"
PKG_LONGDESC="Eden is a high-performance and easy-to-use emulator, tailored for enthusiasts and developers alike."
PKG_SITE="https://github.com/UzuCore/eden"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_TOOLCHAIN="manual"

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

# Clang 경로 설정
EDEN_LLVM_BIN="${TOOLCHAIN}/bin"

make_target() {
  # 1. -Werror=xxx 형태가 아니라, 그냥 -Werror가 들어간 모든 문구를 
  # 컴파일러가 무시하는 안전한 플래그(-Wno-error)로 치환합니다.
  # 이렇게 하면 =all 같은 찌꺼기가 남아도 -Wno-error=all이 되어 빌드가 통과됩니다.
  find "${PKG_BUILD}" -name "CMakeLists.txt" -o -name "*.cmake" | xargs sed -i 's/-Werror/-Wno-error/g'
  find "${PKG_BUILD}" -name "CMakeLists.txt" -o -name "*.cmake" | xargs sed -i 's/\/Werror/-Wno-error/g'

  # 2. 나머지 환경 변수 설정 (기존과 동일)
  local _v
  for _v in CFLAGS CXXFLAGS LDFLAGS; do
    export ${_v}="$(echo ${!_v} | sed 's/-mabi=lp64//g') -Wno-shorten-64-to-32 -Wno-conversion"
  done

  # 3. 도구 경로 명시적 지정
  export AR="${EDEN_LLVM_BIN}/llvm-ar"
  export RANLIB="${EDEN_LLVM_BIN}/llvm-ranlib"
  export NM="${EDEN_LLVM_BIN}/llvm-nm"

  mkdir -p "${PKG_BUILD}/.${TARGET_NAME}"
  cd "${PKG_BUILD}/.${TARGET_NAME}"

  local -a tgt_opts=(
    -G Ninja
    -S "${PKG_BUILD}"
    -B "${PKG_BUILD}/.${TARGET_NAME}"
    -DCMAKE_SYSTEM_NAME=Linux
    -DCMAKE_SYSTEM_PROCESSOR=aarch64
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr
    -DCMAKE_SYSROOT="${SYSROOT_PREFIX}"
    -DCMAKE_FIND_ROOT_PATH="${SYSROOT_PREFIX}"
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
    
    # 컴파일러 설정
    -DCMAKE_C_COMPILER="${EDEN_LLVM_BIN}/clang"
    -DCMAKE_C_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    -DCMAKE_CXX_COMPILER="${EDEN_LLVM_BIN}/clang++"
    -DCMAKE_CXX_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    -DCMAKE_ASM_COMPILER="${EDEN_LLVM_BIN}/clang"
    -DCMAKE_ASM_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    
    # 도구 설정
    -DCMAKE_LINKER="${EDEN_LLVM_BIN}/ld.lld"
    -DCMAKE_AR="${EDEN_LLVM_BIN}/llvm-ar"
    -DCMAKE_RANLIB="${EDEN_LLVM_BIN}/llvm-ranlib"
    -DCMAKE_NM="${EDEN_LLVM_BIN}/llvm-nm"
    
    # Eden 전용 옵션
    -DYUZU_BUILD_PRESET=generic
    -DENABLE_QT_TRANSLATION=ON
    -DUSE_DISCORD_PRESENCE=OFF
    -DYUZU_USE_BUNDLED_SIRIT=ON
    -DYUZU_USE_BUNDLED_QT=OFF
    -DYUZU_USE_BUNDLED_SDL3=OFF
    -DYUZU_TESTS=OFF
    -DYUZU_USE_QT_MULTIMEDIA=OFF
    -DYUZU_USE_QT_WEB_ENGINE=OFF
    -DYUZU_ROOM=ON
    -DYUZU_ROOM_STANDALONE=OFF
    -DYUZU_CMD=OFF
    -DVulkanHeaders_FORCE_BUNDLED=ON
    -DENABLE_LTO=OFF
  )

  cmake "${tgt_opts[@]}"
  ninja
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/.${TARGET_NAME}/bin/eden  ${INSTALL}/usr/bin/
  cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin
  chmod 755 ${INSTALL}/usr/bin/*
  mkdir -p ${INSTALL}/usr/config/eden
  cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/eden/
}