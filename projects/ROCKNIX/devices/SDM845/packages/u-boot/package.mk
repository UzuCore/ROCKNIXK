# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="u-boot"
PKG_VERSION="2025.07"
PKG_SHA256="0f933f6c5a426895bf306e93e6ac53c60870e4b54cda56d95211bec99e63bec7"
PKG_LICENSE="GPL"
PKG_SITE="https://www.denx.de/wiki/U-Boot"
PKG_URL="https://ftp.denx.de/pub/u-boot/${PKG_NAME}-${PKG_VERSION}.tar.bz2"
PKG_DEPENDS_TARGET="toolchain gnutls:host mkbootimg:host"
PKG_LONGDESC="Das U-Boot for the AYN Odin. Flashed to the Android boot partition, it starts GRUB (EFI) from the SD card."
PKG_TOOLCHAIN="manual"

PKG_NEED_UNPACK="${PROJECT_DIR}/${PROJECT}/bootloader ${PROJECT_DIR}/${PROJECT}/devices/${DEVICE}/bootloader"
PKG_NEED_UNPACK+=" ${PROJECT_DIR}/${PROJECT}/options ${PROJECT_DIR}/${PROJECT}/devices/${DEVICE}/options"

PKG_UBOOT_CONFIG="qcom_defconfig"
PKG_DEVICE_TREE="qcom/sdm845-ayn-odin"

make_target() {
  [ "${BUILD_WITH_DEBUG}" = "yes" ] && PKG_DEBUG=1 || PKG_DEBUG=0
  setup_pkg_config_host

  DEBUG=${PKG_DEBUG} CROSS_COMPILE="${TARGET_KERNEL_PREFIX}" LDFLAGS="" ARCH=arm make mrproper
  DEBUG=${PKG_DEBUG} CROSS_COMPILE="${TARGET_KERNEL_PREFIX}" LDFLAGS="" ARCH=arm make HOSTCC="${HOST_CC}" HOSTCFLAGS="-I${TOOLCHAIN}/include" HOSTLDFLAGS="${HOST_LDFLAGS}" ${PKG_UBOOT_CONFIG}
  DEBUG=${PKG_DEBUG} CROSS_COMPILE="${TARGET_KERNEL_PREFIX}" LDFLAGS="" ARCH=arm make HOSTCC="${HOST_CC}" HOSTCFLAGS="-I${TOOLCHAIN}/include" HOSTLDFLAGS="${HOST_LDFLAGS}" HOSTSTRIP="true" DEVICE_TREE=${PKG_DEVICE_TREE}

  # Android boot image: gzipped u-boot with the appended dtb as "kernel"
  gzip -c ${PKG_BUILD}/u-boot-nodtb.bin > ${PKG_BUILD}/u-boot-nodtb.bin.gz
  cat ${PKG_BUILD}/u-boot-nodtb.bin.gz ${PKG_BUILD}/dts/upstream/src/arm64/${PKG_DEVICE_TREE}.dtb > ${PKG_BUILD}/u-boot-dtb.gz
  python3 ${TOOLCHAIN}/mkbootimg/mkbootimg.py \
    --kernel ${PKG_BUILD}/u-boot-dtb.gz \
    --kernel_offset 0x00008000 --pagesize 4096 --header_version 0 \
    --cmdline nodtbo \
    -o ${PKG_BUILD}/u-boot-ayn-odin.img
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/bootloader/u-boot
    cp -a ${PKG_BUILD}/u-boot-ayn-odin.img ${INSTALL}/usr/share/bootloader/u-boot/
}
