# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="eden-sa"
PKG_VERSION="5a67d650e64ec45e90ef8c833209eaa95ede1620" # 260526
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

post_unpack_target() {
  # 기본 소스 코드 내의 -Werror 제거
  find "${PKG_BUILD}" -name "CMakeLists.txt" -o -name "*.cmake" | xargs sed -i 's/-Werror/-Wno-error/g' 2>/dev/null
  find "${PKG_BUILD}" -name "CMakeLists.txt" -o -name "*.cmake" | xargs sed -i 's/\/Werror/-Wno-error/g' 2>/dev/null
}

make_target() {
  # 1. PGO 데이터 설정
  local PGO_URL="https://github.com/Eden-CI/PGO/releases/download/v020525/eden.profdata"
  local PGO_FILE="${PKG_BUILD}/eden.profdata"

  # PGO 데이터 다운로드 (없을 경우에만 다운로드)
  if [ ! -f "$PGO_FILE" ]; then
    echo "Downloading PGO profile data..."
    curl -L "$PGO_URL" -o "$PGO_FILE"
  fi

  # 2. 환경 변수 필터링 및 타겟 조정
  local PGO_FLAGS="-fprofile-use=${PGO_FILE} -Wno-backend-plugin -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"

  local CPU_FIX="-mcpu=cortex-a78"
  local LTO_FLAGS="-flto=thin -fuse-ld=lld"

  local _v
  for _v in CFLAGS CXXFLAGS; do
    export ${_v}="$(echo ${!_v} | sed -e 's/-mabi=lp64//g' \
                                     -e 's/-mcpu=cortex-x3/-mcpu=cortex-a78/g' \
                                     -e 's/-mcpu=cortex-x4/-mcpu=cortex-a78/g' \
                                     -e 's/-march=armv9-a/-march=armv8.2-a/g' \
                                     -e 's/-march=armv9.2-a/-march=armv8.2-a/g') \
                  -Wno-error ${PGO_FLAGS} ${CPU_FIX} ${LTO_FLAGS}"
  done

  export LDFLAGS="$(echo ${LDFLAGS} | sed -e 's/-mabi=lp64//g' \
                                         -e 's/-mcpu=cortex-x3/-mcpu=cortex-a78/g' \
                                         -e 's/-mcpu=cortex-x4/-mcpu=cortex-a78/g' \
                                         -e 's/-march=armv9-a/-march=armv8.2-a/g' \
                                         -e 's/-march=armv9.2-a/-march=armv8.2-a/g' \
                                         -e 's/-fuse-ld=bfd/-fuse-ld=lld/g') \
                  -Wno-error ${PGO_FLAGS} ${CPU_FIX} ${LTO_FLAGS}"

  # 3. 도구 경로 지정
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

    # 여기에서도 다시 한번 CPU 타겟을 고정합니다.
    -DCMAKE_C_FLAGS="${CPU_FIX}"
    -DCMAKE_CXX_FLAGS="${CPU_FIX}"

    # 도구 설정
    -DCMAKE_LINKER="${EDEN_LLVM_BIN}/ld.lld"
    -DCMAKE_AR="${EDEN_LLVM_BIN}/llvm-ar"
    -DCMAKE_RANLIB="${EDEN_LLVM_BIN}/llvm-ranlib"
    -DCMAKE_NM="${EDEN_LLVM_BIN}/llvm-nm"

    # Eden 옵션
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

  # 4. CMake 실행
  cmake "${tgt_opts[@]}"

  # 5. 빌드 파일 내 -Werror 제거
  find . -name "*.ninja" -o -name "flags.make" | xargs sed -i 's/-Werror/-Wno-error/g' 2>/dev/null

  # 6. 빌드 시작
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
