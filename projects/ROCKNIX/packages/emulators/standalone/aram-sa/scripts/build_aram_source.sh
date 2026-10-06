#!/bin/bash

set -euo pipefail

BUILD_DIR="${1:?missing package build directory}"
SOURCE_ROOT="${BUILD_DIR}/source"
GO_BIN="${ARAM_GO:-/usr/bin/go}"

: "${ARAM_EMU_REF:?missing ARAM_EMU_REF}"
: "${ARAM_CORE_REF:?missing ARAM_CORE_REF}"
: "${ARAM_FRONTEND_REF:?missing ARAM_FRONTEND_REF}"
: "${ARAM_AUTHD_REF:?missing ARAM_AUTHD_REF}"
: "${CC:?missing ROCKNIX target CC}"

if [ ! -x "${GO_BIN}" ]; then
  echo "aram-sa: Go compiler not found at ${GO_BIN}" >&2
  exit 1
fi

fetch_repo() {
  local repo="$1"
  local ref="$2"
  local dir="$3"

  if [ ! -d "${dir}/.git" ]; then
    rm -rf "${dir}"
    git clone --filter=blob:none --no-checkout "https://github.com/${repo}.git" "${dir}"
  fi
  git -C "${dir}" fetch --depth 1 origin "${ref}"
  git -C "${dir}" checkout --detach -f FETCH_HEAD
  git -C "${dir}" clean -fdx
}

mkdir -p "${SOURCE_ROOT}"
fetch_repo mirusu400/aram-emu "${ARAM_EMU_REF}" "${SOURCE_ROOT}/aram-emu"
fetch_repo mirusu400/aram-core "${ARAM_CORE_REF}" "${SOURCE_ROOT}/aram-core"
fetch_repo mirusu400/aram-frontend "${ARAM_FRONTEND_REF}" "${SOURCE_ROOT}/aram-frontend"
fetch_repo mirusu400/aram-authd "${ARAM_AUTHD_REF}" "${SOURCE_ROOT}/aram-authd"

git -C "${SOURCE_ROOT}/aram-core" apply \
  "${PKG_DIR}/patches-core/001-enable-linux-arm64-native-jit.patch"

git -C "${SOURCE_ROOT}/aram-frontend" apply \
  "${PKG_DIR}/patches-frontend/001-ignore-touch-devices-as-gamepads.patch"
git -C "${SOURCE_ROOT}/aram-frontend" apply \
  "${PKG_DIR}/patches-frontend/002-bound-audio-player-read-size.patch"

export GOOS=linux
export GOARCH=arm64
export CGO_ENABLED=1
export GOTOOLCHAIN=go1.25.0+auto
export GOPATH="${BUILD_DIR}/.gopath"
export GOMODCACHE="${GOPATH}/pkg/mod"
export GOCACHE="${BUILD_DIR}/.gocache"
export GOTMPDIR="${BUILD_DIR}/.gotmp"

mkdir -p "${GOPATH}" "${GOMODCACHE}" "${GOCACHE}" "${GOTMPDIR}"

if [ -n "${CFLAGS:-}" ]; then
  export CGO_CFLAGS="${CFLAGS}"
fi
if [ -n "${CPPFLAGS:-}" ]; then
  export CGO_CPPFLAGS="${CPPFLAGS}"
fi
if [ -n "${CXXFLAGS:-}" ]; then
  export CGO_CXXFLAGS="${CXXFLAGS}"
fi
if [ -n "${LDFLAGS:-}" ]; then
  export CGO_LDFLAGS="${LDFLAGS}"
fi

cd "${SOURCE_ROOT}/aram-emu"

BUILD_VERSION="ROCKNIX-${ARAM_EMU_REF:0:7}-native"
"${GO_BIN}" build -trimpath \
  -ldflags="-s -w -X=github.com/mirusu400/aram-frontend/frontend.BuildVersion=${BUILD_VERSION} -X=github.com/mirusu400/aram-frontend/frontend.SelfUpdateDisabled=1" \
  -o "${BUILD_DIR}/aram" ./cmd/aram

file "${BUILD_DIR}/aram"
