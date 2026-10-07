# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

. ${ROOT}/packages/devel/libfmt/package.mk

case ${DEVICE} in
  SDM845|SM4450|SM8250|SM8550|SM8650|SM8750|AMD64)
    ;;
  *)
    PKG_VERSION="9.1.0"
    PKG_SHA256="5dea48d1fcddc3ec571ce2058e13910a0d4a6bab4cc09a809d8b1dd1c88ae6f2"
    PKG_URL="https://github.com/fmtlib/fmt/archive/${PKG_VERSION}.tar.gz"
    ;;
esac
