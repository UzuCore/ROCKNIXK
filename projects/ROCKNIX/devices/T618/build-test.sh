#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
cd "${ROOT}"

export PROJECT=ROCKNIX DEVICE=T618 ARCH=aarch64 T618_TEST_BUILD=yes
export EMULATION_DEVICE=no TARGET_TYPE=none ENABLE_32BIT=false MODULES_PKG=no
export BUILD_SUFFIX=test-no-emulators IMAGE_SUFFIX=test-no-emulators
export BUILD_DIR="${BUILD_DIR:-${ROOT}}"
export TARGET_DIR="${TARGET_DIR:-${BUILD_DIR}/target-test-no-emulators}"
export T618_FIRMWARE_DIR="${T618_FIRMWARE_DIR:-${T618_PRIVATE_FIRMWARE_DIR:-${ROOT}/projects/ROCKNIX/devices/T618/firmware}}"
export THREADCOUNT="${THREADCOUNT:-2}" CONCURRENCY_MAKE_LEVEL="${CONCURRENCY_MAKE_LEVEL:-6}"
export CONCURRENCY_LOAD="${CONCURRENCY_LOAD:-12}" MTINTERVAL=30 MTPROGRESS=yes
export DISABLE_COLORS=yes LOCAL_CC=/usr/bin/gcc-12 LOCAL_CXX=/usr/bin/g++-12

case "${1:-image}" in
  image) exec make image ;;
  plan) exec ./scripts/pkgjson ;;
  package) shift; test "$#" -eq 1; exec ./scripts/build "$1" ;;
  *) printf 'Usage: %s [image|plan|package NAME]\n' "$0" >&2; exit 2 ;;
esac
