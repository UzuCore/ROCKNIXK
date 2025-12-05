# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="rocknix-abl"
PKG_VERSION="1.3"
PKG_ARCH="aarch64"
PKG_SITE="https://github.com/ROCKNIX/packages"
PKG_URL="${PKG_SITE}/raw/refs/heads/main/rocknix-abl-e854aed3eedbf03a8ea2c22d8ec7ec21a89e43ed.tar.gz"
PKG_LONGDESC="ROCKNIX ABL."
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/bootloader/rocknix_abl
  cp ${PKG_BUILD}/abl_signed-${DEVICE}.elf ${INSTALL}/usr/share/bootloader/rocknix_abl/abl_signed.elf
  cp ${PKG_DIR}/sources/* ${INSTALL}/usr/share/bootloader/rocknix_abl
}
