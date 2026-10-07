# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="t618-firmware"
PKG_VERSION="4"
PKG_LICENSE="proprietary"
PKG_SITE="https://github.com/TheGammaSqueeze/GammaOSNext"
PKG_DEPENDS_TARGET="toolchain Python3"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="Hash-verified T618 firmware inputs bundled with the device project"
PKG_NEED_UNPACK="${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json"

make_target() {
  python3 ${PKG_DIR}/verify_firmware.py \
    --manifest ${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json \
    --source "${T618_FIRMWARE_DIR}" --verify-only
}

makeinstall_target() {
  python3 ${PKG_DIR}/verify_firmware.py \
    --manifest ${PROJECT_DIR}/${PROJECT}/devices/T618/firmware-manifest.json \
    --source "${T618_FIRMWARE_DIR}" \
    --destination ${INSTALL}/$(get_full_firmware_dir)
}
