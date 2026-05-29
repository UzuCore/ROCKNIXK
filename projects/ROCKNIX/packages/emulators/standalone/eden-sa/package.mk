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

if [ "${VULKAN_SUPPORT}" = "yes" ]; then
  PKG_DEPENDS_TARGET+=" vulkan-loader vulkan-headers"
fi

# Clang 경로 설정
EDEN_LLVM_BIN="${TOOLCHAIN}/bin"

# PGO 데이터 설정
PGO_URL="https://github.com/Eden-CI/PGO/releases/download/v020525/eden.profdata"
PGO_FILE="${PKG_BUILD}/eden.profdata"

post_unpack_target() {
  # -Werror 제거 (단일 find 명령으로 통합)
  find "${PKG_BUILD}" \( -name "CMakeLists.txt" -o -name "*.cmake" \) -exec sed -i 's/\(-\)\?Werror/-Wno-error/g' {} +
}

make_target() {
  # 1. PGO 데이터 캐싱 (빌드 디렉토리에 저장하여 재사용)
  if [ ! -f "$PGO_FILE" ]; then
    echo "Downloading PGO profile data..."
    curl -L --max-time 300 --retry 3 "$PGO_URL" -o "$PGO_FILE" || {
      echo "Warning: PGO download failed, building without PGO"
      PGO_FILE=""
    }
  fi

  # 2. 컴파일 플래그 최적화
  local PGO_FLAGS=""
  if [ -f "$PGO_FILE" ]; then
    PGO_FLAGS="-fprofile-use=${PGO_FILE} -Wno-backend-plugin -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
  fi

  local CPU_FIX="-mcpu=cortex-a78 -mtune=cortex-a78"
  local LTO_FLAGS="-flto=thin -fuse-ld=lld -Wl,--lto-O3"
  local OPT_FLAGS="-O3 -march=armv8.2-a -ffast-math -fslp-vectorize-aggressive"

  # 3. sed 명령 최적화 (단일 sed로 통합)
  local FLAGS_CLEAN="s/-mabi=lp64//g; s/-mcpu=cortex-x[34]/-mcpu=cortex-a78/g; s/-march=armv9(\.[2-9])?-a/-march=armv8.2-a/g"
  
  for _v in CFLAGS CXXFLAGS; do
    export ${_v}="$(echo ${!_v} | sed -e "$FLAGS_CLEAN") ${OPT_FLAGS} ${PGO_FLAGS} ${CPU_FIX} ${LTO_FLAGS}"
  done

  export LDFLAGS="$(echo ${LDFLAGS} | sed -e "$FLAGS_CLEAN" -e 's/-fuse-ld=bfd/-fuse-ld=lld/g') ${PGO_FLAGS} ${CPU_FIX} ${LTO_FLAGS}"

  # 4. 도구 경로 지정
  export AR="${EDEN_LLVM_BIN}/llvm-ar"
  export RANLIB="${EDEN_LLVM_BIN}/llvm-ranlib"
  export NM="${EDEN_LLVM_BIN}/llvm-nm"
  export CC="${EDEN_LLVM_BIN}/clang"
  export CXX="${EDEN_LLVM_BIN}/clang++"
  export LD="${EDEN_LLVM_BIN}/ld.lld"

  mkdir -p "${PKG_BUILD}/.${TARGET_NAME}"
  cd "${PKG_BUILD}/.${TARGET_NAME}"

  # 5. CMake 옵션 배열 (주석 정리 및 최적화)
  local -a tgt_opts=(
    # 빌드 설정
    -G Ninja
    -S "${PKG_BUILD}"
    -B "${PKG_BUILD}/.${TARGET_NAME}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr

    # 시스템 설정
    -DCMAKE_SYSTEM_NAME=Linux
    -DCMAKE_SYSTEM_PROCESSOR=aarch64
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
    -DCMAKE_C_FLAGS="${CPU_FIX} ${OPT_FLAGS}"
    -DCMAKE_CXX_FLAGS="${CPU_FIX} ${OPT_FLAGS}"

    # 링커 및 도구 설정
    -DCMAKE_LINKER="${EDEN_LLVM_BIN}/ld.lld"
    -DCMAKE_AR="${EDEN_LLVM_BIN}/llvm-ar"
    -DCMAKE_RANLIB="${EDEN_LLVM_BIN}/llvm-ranlib"
    -DCMAKE_NM="${EDEN_LLVM_BIN}/llvm-nm"

    # Eden 빌드 옵션
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
    -DUSE_FAST_MATH=ON
  )

  # 6. CMake 실행
  cmake "${tgt_opts[@]}" || return 1

  # 7. 빌드 파일 내 -Werror 제거 (단일 명령으로 통합)
  find . \( -name "*.ninja" -o -name "flags.make" \) -exec sed -i 's/-Werror/-Wno-error/g' {} + 2>/dev/null

  # 8. 병렬 빌드 지원
  ninja -j$(nproc) || ninja
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/.${TARGET_NAME}/bin/eden ${INSTALL}/usr/bin/ || return 1
  [ -d "${PKG_DIR}/scripts" ] && cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin/
  chmod 755 ${INSTALL}/usr/bin/*
  
  mkdir -p ${INSTALL}/usr/config/eden
  [ -d "${PKG_DIR}/config/${DEVICE}" ] && cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/eden/
}
