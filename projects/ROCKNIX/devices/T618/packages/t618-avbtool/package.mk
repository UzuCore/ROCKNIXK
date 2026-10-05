# SPDX-License-Identifier: GPL-2.0-or-later
PKG_NAME="t618-avbtool"
PKG_VERSION="d2c46084c67688a46dac2fd37e3cad0d65850387"
PKG_SHA256="91e2e9aae890addd71eb6af48ada43cce418bffdd208be4f4077ac678ca6663d"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/LineageOS/android_external_avb"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_HOST="openssl:host"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="Pinned Android Verified Boot packaging utility"

make_host() { :; }

makeinstall_host() {
  mkdir -p ${TOOLCHAIN}/t618-avbtool
  cp avbtool.py test/data/testkey_rsa4096.pem LICENSE ${TOOLCHAIN}/t618-avbtool/
}
