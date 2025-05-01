# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://rocknix.org)

PKG_NAME="libgpiod"
PKG_VERSION="41231df28c9aecacaaae9e6493d31161023733d6"
PKG_LICENSE="GPL"
PKG_SITE="https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git"
PKG_URL="https://github.com/brgl/libgpiod/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain"
PKG_LONGDESC="libgpiod: C library and tools for interacting with the linux GPIO character device"
PKG_TOOLCHAIN="autotools"

PKG_CONFIGURE_OPTS_TARGET="--enable-tools"
