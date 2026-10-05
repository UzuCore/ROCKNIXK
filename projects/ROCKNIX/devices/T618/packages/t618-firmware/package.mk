# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="t618-firmware"
PKG_VERSION="3"
PKG_LICENSE="proprietary"
PKG_SITE="https://github.com/TheGammaSqueeze/GammaOSNext"
PKG_DEPENDS_TARGET="toolchain Python3"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="Locally supplied, hash-verified RG405M/RG405V T618 firmware"
PKG_NEED_UNPACK="${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json"

make_target() {
  python3 ${PKG_DIR}/verify_firmware.py \
    --manifest ${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json \
    --source "${T618_PRIVATE_FIRMWARE_DIR}" --verify-only
}

makeinstall_target() {
  python3 ${PKG_DIR}/verify_firmware.py \
    --manifest ${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json \
    --source "${T618_PRIVATE_FIRMWARE_DIR}" \
    --destination ${INSTALL}/$(get_full_firmware_dir)
}
