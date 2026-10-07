# SPDX-License-Identifier: GPL-2.0-only
PKG_NAME="t618-kernel-integration"
PKG_VERSION="3"
PKG_LICENSE="GPL-2.0-only"
PKG_SECTION="virtual"
PKG_TOOLCHAIN="manual"
PKG_LONGDESC="Source-only RG405M/RG405V adaptations for the pinned UMS512 kernel"

# The kernel post-patch hook consumes these files; no standalone object is built.
