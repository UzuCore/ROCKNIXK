# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)
#
# Lichii is an experimental Switch/Switch 2 emulator (Eden/yuzu fork).
#
# It must be built from source on the target device rather than shipped as an
# upstream binary: the release archives are generic (or armv9) builds, and on
# small-core SoCs like SDM845 they trap with SIGILL as soon as the guest starts
# executing code. Building here also pins -march/-mtune to the actual SoC.

PKG_NAME="lichii-sa"
PKG_VERSION="093e264c8a7959dbeffe60248750aeec30c92235"
PKG_LICENSE="GPL-3.0-or-later"
PKG_LONGDESC="Lichii is an experimental Nintendo Switch and Switch 2 emulator derived from Eden and yuzu."
PKG_SITE="https://github.com/zortos293/Lichii"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_TOOLCHAIN="manual"

# Qt6 + Vulkan + ffmpeg are already part of every ROCKNIX image that ships a
# standalone Switch emulator, so reuse the same dependency set as eden-sa.
PKG_DEPENDS_TARGET="toolchain llvm:host SDL3 boost libevdev libdrm ffmpeg zlib zstd opus alsa-lib qt6 libfmt"

if [ ! "${OPENGL}" = "no" ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGL} glu libglvnd"
fi

if [ "${OPENGLES_SUPPORT}" = yes ]; then
  PKG_DEPENDS_TARGET+=" ${OPENGLES}"
fi

if [ "${VULKAN_SUPPORT}" = "yes" ]; then
  PKG_DEPENDS_TARGET+=" vulkan-loader vulkan-headers"
fi

LICHII_LLVM_BIN="${TOOLCHAIN}/bin"

# Translate the build system's TARGET_CPU/TARGET_CPU_FLAGS into flags that only
# use instructions the device actually implements. Anything newer than the host
# core traps at runtime, so keep the mapping conservative.
lichii_cpu_flags() {
  # Follow the extensions each device/options file declares, so the compiler
  # only emits instructions the SoC really has. The values below are the
  # TARGET_CPU/TARGET_CPU_FLAGS of the matching device, spelled as -march so
  # that the extension list stays explicit and reviewable.
  #
  # -mcpu is deliberately not used: it silently enables extensions the device
  # never declared (cortex-a75 also turns on dotprod and fp16, for example).
  case "${DEVICE}" in
    SDM845)
      # Cortex-A75 + A55. Every Snapdragon 845 in the tree runs the
      # "cortex-a75.cortex-a55" pair with only crypto+crc added.
      echo "-march=armv8.2-a+crc+crypto -mtune=cortex-a75"
      ;;
    SM8250)
      # Cortex-A77 + A55 (Snapdragon 865). The device options only declare
      # crypto+crc, so tune for A77 without enabling anything else.
      echo "-march=armv8.2-a+crc+crypto -mtune=cortex-a77"
      ;;
    SM8550)
      # Cortex-X3 + A715 + A510 (Snapdragon 8 Gen 2, Odin 2 Portal/Mini).
      # These are the extensions the SM8550 options file already builds the
      # whole userspace with, so every core in the cluster has them.
      echo "-march=armv8.2-a+crc+crypto+fp16+i8mm+bf16+sm4+sha3 -mtune=cortex-x3"
      ;;
    SM8650|SM8750)
      # Cortex-X4 class (Snapdragon 8 Gen 3 / 8 Elite).
      echo "-march=armv8.2-a+crc+crypto+fp16+i8mm+bf16+sm4+sha3 -mtune=cortex-x4"
      ;;
    *)
      # Unknown SoC. Plain ARMv8.0-A is the only level every aarch64 core in
      # the tree implements, so never guess higher here.
      echo "-march=armv8-a -mtune=generic"
      ;;
  esac
}

make_target() {
  local CPU_FLAGS
  CPU_FLAGS="$(lichii_cpu_flags)"

  local OPT_FLAGS="-O3"
  local FLAGS_CLEAN="s/-mabi=lp64//g; s/-mcpu=[^ ]*//g; s/-march=[^ ]*//g; s/-mtune=[^ ]*//g"

  for _v in CFLAGS CXXFLAGS; do
    export ${_v}="$(echo ${!_v} | sed -e "$FLAGS_CLEAN") ${OPT_FLAGS} ${CPU_FLAGS}"
  done
  export LDFLAGS="$(echo ${LDFLAGS} | sed -e "$FLAGS_CLEAN" -e 's/-fuse-ld=bfd/-fuse-ld=lld/g') -fuse-ld=lld"

  export AR="${LICHII_LLVM_BIN}/llvm-ar"
  export RANLIB="${LICHII_LLVM_BIN}/llvm-ranlib"
  export NM="${LICHII_LLVM_BIN}/llvm-nm"
  export CC="${LICHII_LLVM_BIN}/clang"
  export CXX="${LICHII_LLVM_BIN}/clang++"
  export LD="${LICHII_LLVM_BIN}/ld.lld"

  mkdir -p "${PKG_BUILD}/.${TARGET_NAME}"
  cd "${PKG_BUILD}/.${TARGET_NAME}"

  local -a tgt_opts=(
    -G Ninja
    -S "${PKG_BUILD}"
    -B "${PKG_BUILD}/.${TARGET_NAME}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr

    -DCMAKE_SYSTEM_NAME=Linux
    -DCMAKE_SYSTEM_PROCESSOR=aarch64
    -DCMAKE_SYSROOT="${SYSROOT_PREFIX}"
    -DCMAKE_FIND_ROOT_PATH="${SYSROOT_PREFIX}"
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY

    -DCMAKE_C_COMPILER="${LICHII_LLVM_BIN}/clang"
    -DCMAKE_C_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    -DCMAKE_CXX_COMPILER="${LICHII_LLVM_BIN}/clang++"
    -DCMAKE_CXX_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    -DCMAKE_ASM_COMPILER="${LICHII_LLVM_BIN}/clang"
    -DCMAKE_ASM_COMPILER_TARGET=aarch64-rocknix-linux-gnu
    -DCMAKE_C_FLAGS="${CFLAGS}"
    -DCMAKE_CXX_FLAGS="${CXXFLAGS}"

    -DCMAKE_LINKER="${LICHII_LLVM_BIN}/ld.lld"
    -DCMAKE_AR="${LICHII_LLVM_BIN}/llvm-ar"
    -DCMAKE_RANLIB="${LICHII_LLVM_BIN}/llvm-ranlib"
    -DCMAKE_NM="${LICHII_LLVM_BIN}/llvm-nm"

    -DYUZU_BUILD_PRESET=custom
    -DENABLE_QT_TRANSLATION=ON
    -DUSE_DISCORD_PRESENCE=OFF
    -DYUZU_USE_BUNDLED_SIRIT=ON
    -DYUZU_USE_BUNDLED_QT=OFF
    -DYUZU_USE_BUNDLED_SDL3=OFF
    -DYUZU_TESTS=OFF
    -DYUZU_USE_QT_MULTIMEDIA=OFF
    -DYUZU_USE_QT_WEB_ENGINE=OFF
    -DYUZU_ROOM=OFF
    -DYUZU_CMD=OFF
    -DYUZU_CRASH_DUMPS=OFF
    -DENABLE_UPDATE_CHECKER=OFF
    -DENABLE_WEB_SERVICE=OFF
    -DENABLE_LTO=OFF
    -DENABLE_WERROR=OFF

    # The sysroot ships Vulkan-Headers and Vulkan-Utility-Libraries as separate
    # packages at different versions, which CPMUtil refuses to mix. Force the
    # bundled copies so configure succeeds.
    -DVulkanHeaders_FORCE_BUNDLED=ON
    -DENABLE_LIBUSB=ON
  )

  # Frame generation is a pure shader pass, but it costs GPU time that the
  # older Adreno parts cannot spare. Leave it out where it would only slow
  # emulation down.
  case "${DEVICE}" in
    SM8550|SM8650|SM8750)
      tgt_opts+=(-DENABLE_LSFG=ON)
      ;;
    *)
      tgt_opts+=(-DENABLE_LSFG=OFF)
      ;;
  esac

  cmake "${tgt_opts[@]}" || return 1

  ninja -j${CONCURRENCY_MAKE_LEVEL:-4}
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
  cp ${PKG_BUILD}/.${TARGET_NAME}/bin/lichii ${INSTALL}/usr/bin/ || return 1
  [ -f ${PKG_BUILD}/.${TARGET_NAME}/bin/libnxformat.so ] && \
    cp ${PKG_BUILD}/.${TARGET_NAME}/bin/libnxformat.so ${INSTALL}/usr/bin/
  [ -d "${PKG_DIR}/scripts" ] && cp -rf ${PKG_DIR}/scripts/* ${INSTALL}/usr/bin/
  chmod 755 ${INSTALL}/usr/bin/*

  # Shared defaults for every device; a device may override individual files
  # by shipping its own config/${DEVICE}/ directory.
  mkdir -p ${INSTALL}/usr/config/lichii
  if [ -d "${PKG_DIR}/config/common" ]; then
    cp -rf ${PKG_DIR}/config/common/* ${INSTALL}/usr/config/lichii/
  fi
  if [ -d "${PKG_DIR}/config/${DEVICE}" ]; then
    cp -rf ${PKG_DIR}/config/${DEVICE}/* ${INSTALL}/usr/config/lichii/
  fi

  # Nothing to install must still be a success: the last test in a function
  # decides its exit status, and config/${DEVICE} is optional.
  return 0
}
