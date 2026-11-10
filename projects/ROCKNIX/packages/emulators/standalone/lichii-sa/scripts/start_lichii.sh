#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)
#
# Launch Lichii (Switch / Switch 2 emulator).
#
# Called from runemu.sh as: start_lichii.sh "<rom>" "<platform>"
# Called with no arguments from the Tools menu (Start Lichii.sh).
#
# Lichii keeps the Eden data layout, so it shares /storage/.config/eden and
# /storage/.local/share/eden with eden-sa. Seed only the files it needs and
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

# Lichii shares this file with eden-sa, whose shipped config pins values that
# make Lichii trap or run badly:
#
#   cpu_backend=1        NCE needs native code execution for the guest ISA.
#                        Switch 2 titles use instructions the host core does
#                        not implement, so it dies with SIGILL immediately.
#   nvdec_emulation=2    Hardware AV1 decode. Adreno 6xx has no AV1 decoder.
#   resolution_setup=2   1.5x render scale, unaffordable on the smaller SoCs.
#   use_speed_limit=true Throttles to 100% of guest speed.
#
# Enforce the working values on every launch instead of only when the file is
# first created, otherwise a user who has ever started eden gets a broken
# Lichii.
lichii_force_setting() {
    local section="${1}" key="${2}" value="${3}"
    local tmp="${CONF_FILE}.tmp.$$"

    [ -f "${CONF_FILE}" ] || return 0

    awk -v section="${section}" -v key="${key}" -v value="${value}" '
        function flush_section() {
            if (!in_target) return
            if (!seen_default) print key "\\default=false"
            if (!seen_value) print key "=" value
        }
        /^\[/ {
            flush_section()
            in_target = ("[" section "]" == $0)
            seen_default = 0
            seen_value = 0
        }
        in_target && index($0, key "\\default=") == 1 {
            print key "\\default=false"
            seen_default = 1
            next
        }
        in_target && index($0, key "=") == 1 {
            print key "=" value
            seen_value = 1
            next
        }
        { print }
        END { flush_section() }
    ' "${CONF_FILE}" > "${tmp}" && mv "${tmp}" "${CONF_FILE}"
}

for _preset in \
    "Cpu|cpu_backend|0" \
    "Renderer|nvdec_emulation|1" \
    "Renderer|native_async_draws|0" \
    "Renderer|resolution_setup|3" \
    "Core|use_speed_limit|false"; do
    IFS='|' read -r _section _key _value <<< "${_preset}"
    lichii_force_setting "${_section}" "${_key}" "${_value}"
done

# Firmware and keys live with the other Switch emulators.
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

# wideLines is missing from Adreno 6xx, which fails the Vulkan device
# suitability check. The driver supports the feature, it only reports a
# different range, so advertise it again.
export FD_DEV_FEATURES="${FD_DEV_FEATURES:-line_width_min=1.0:line_width_max=127.5}"

# Qt on Wayland; xcb is broken on these compositors.
export QT_QPA_PLATFORM=wayland

# Lichii reads the key file from its own directory.
export SDL_AUDIODRIVER=pulseaudio

set_kill set "-9 lichii"

if [ -n "${1:-}" ]; then
    # -f forces the render window fullscreen; without it the Qt frontend
    # overrides the saved setting and the game starts behind the menu bar.
    /usr/bin/lichii -f "${1}"
else
    sway_fullscreen "lichii" "class" &
    /usr/bin/lichii
fi
