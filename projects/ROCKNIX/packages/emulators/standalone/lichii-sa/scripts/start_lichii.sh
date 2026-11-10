#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)
#
# Launch Lichii (Switch / Switch 2 emulator) from EmulationStation.
#
# Lichii keeps the Eden data layout, so it shares /storage/.config/eden and
# /storage/.local/share/eden with eden-sa. Only seed the files it needs and
# never overwrite what the user already changed.

. /etc/profile

CONF_DIR="/storage/.config/eden"
CONF_FILE="${CONF_DIR}/qt-config.ini"
GUI_FILE="${CONF_DIR}/gui_config.ini"
DATA_DIR="/storage/.local/share/eden"

mkdir -p "${CONF_DIR}" "${DATA_DIR}/keys"

# Seed defaults once, then leave the user's edits alone.
[ -f "${CONF_FILE}" ] || cp -f /usr/config/lichii/qt-config.ini "${CONF_FILE}"
[ -f "${GUI_FILE}" ]  || cp -f /usr/config/lichii/gui_config.ini  "${GUI_FILE}"

# Shared keys: prefer the copy living next to the other emulators.
if [ ! -f "${DATA_DIR}/keys/prod.keys" ]; then
    for src in /storage/roms/bios/nsw/prod.keys /storage/roms/bios/prod.keys; do
        [ -f "${src}" ] && cp -f "${src}" "${DATA_DIR}/keys/prod.keys" && break
    done
fi
if [ ! -f "${DATA_DIR}/keys/title.keys" ]; then
    for src in /storage/roms/bios/nsw/title.keys /storage/roms/bios/title.keys; do
        [ -f "${src}" ] && cp -f "${src}" "${DATA_DIR}/keys/title.keys" && break
    done
fi

# The Odin DualSense GUID differs between SDL2 and SDL3.
if [ "${HW_DEVICE}" = "SDM845" ] && [ -f "${CONF_FILE}" ]; then
    sed -i 's/guid:030000004c050000e60c000011810000/guid:030000004c050000e60c000000006800/g' "${CONF_FILE}"
fi

# Per-SoC tuning that has to happen at launch, not in a static ini.
if [ -n "${DEVICE_PLANE_ROTATION}" ] || [ "${DEVICE}" = "SDM845" ]; then
    export FD_DEV_FEATURES="${FD_DEV_FEATURES:-line_width_min=1.0:line_width_max=127.5}"
fi

exec /usr/bin/lichii "$@"
