PKG_NAME="tqftpserv"
PKG_SITE="https://github.com/andersson/tqftpserv"
PKG_LICENSE="Sony"
PKG_VERSION="3a10a7dd6fffb70a158582557e274018a7f25294"
PKG_URL="${PKG_SITE}.git"
PKG_DEPENDS_TARGET="qrtr"
PKG_TOOLCHAIN="make"

post_install() {
  enable_service tqftpserv.service
}
