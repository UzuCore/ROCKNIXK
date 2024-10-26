#UzuCore

PKG_NAME="es-colorful-simplified-dc"
PKG_VERSION="6c9daff01a3acdc880e5e8cb7ce59d28526c1b10"
PKG_ARCH="any"
PKG_LICENSE="CUSTOM"
PKG_SITE="https://github.com/UzuCore/es-colorful-simplified-dc"
PKG_URL="${PKG_SITE}.git"
GET_HANDLER_SUPPORT="git"
PKG_SHORTDESC="Colorful (Simplified)"
PKG_LONGDESC="Colorful (Simplified)"
PKG_TOOLCHAIN="manual"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/share/themes/${PKG_NAME}
  cp -rf * ${INSTALL}/usr/share/themes/${PKG_NAME}
}
