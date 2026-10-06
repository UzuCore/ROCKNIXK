# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

PKG_NAME="modules"
PKG_VERSION="1.0"
PKG_LICENSE="custom"
PKG_SITE=""
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain rclone commander qterminal bios-downloader"
PKG_LONGDESC="OS Modules Package"
PKG_TOOLCHAIN="manual"

case ${DEVICE} in
  SDM845|RK3399|RK3588|SM8250|SM8550|SM8650|SM8750|SM4450|SM6115)
    PKG_DEPENDS_TARGET+=" gamepadtester"
    ;;
esac

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/config/modules
    cp -rf ${PKG_DIR}/sources/* ${INSTALL}/usr/config/modules
    chmod 0755 "${INSTALL}/usr/config/modules/download_bios.sh"
}

post_makeinstall_target() {
  if [[ "${DEVICE}" == "SDM845" ]]; then
    "${TOOLCHAIN}/bin/python3" - "${INSTALL}/usr/config/modules/gamelist.xml" <<'PY'
import sys
import xml.etree.ElementTree as ET
path = sys.argv[1]
tree = ET.parse(path)
root = tree.getroot()
if not any(game.findtext('path') == './Start Eden.sh' for game in root.findall('game')):
    game = ET.SubElement(root, 'game')
    for tag, value in (('path', './Start Eden.sh'), ('name', 'Eden'),
                       ('desc', 'Nintendo Switch emulator settings and game library.'),
                       ('genre', 'Tool'), ('players', '1')):
        ET.SubElement(game, tag).text = value
tree.write(path, encoding='utf-8', xml_declaration=True)
PY
  fi
  case ${DEVICE} in
    SM8650|SM8750) rm -f ${INSTALL}/usr/config/modules/*32bit* ;;
  esac

  if [[ "${INSTALLER_SUPPORT}" != "yes" || "${DISPLAYSERVER}" != "wl" ]]; then
    rm -f ${INSTALL}/usr/config/modules/Install*
  fi
}
