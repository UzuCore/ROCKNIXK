# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="firmware-ayn-odin"
PKG_VERSION="92fb887a6ad21a76eb07ea049fe4b71a35388724"
PKG_SHA256="e5b67e532244e57affaee5189ba6dd8989c5af46747aa20e0774bada4b1d97ee"
PKG_LICENSE="proprietary"
PKG_SITE="https://gitlab.com/jenneron/firmware-ayn-odin"
PKG_URL="${PKG_SITE}/-/archive/${PKG_VERSION}/${PKG_NAME}-${PKG_VERSION}.tar.gz"
PKG_LONGDESC="AYN Odin SLPI (sensor DSP) firmware, as used by postmarketOS firmware-ayn-odin."
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/$(get_full_firmware_dir)/qcom/sdm845/AYN/Odin
    cp -a ${PKG_BUILD}/lib/firmware/qcom/sdm845/AYN/Odin/* ${INSTALL}/$(get_full_firmware_dir)/qcom/sdm845/AYN/Odin/
}
