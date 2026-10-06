#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022-present JELOS (https://github.com/JustEnoughLinuxOS)

. /etc/profile

CONF_FILE="/storage/.config/eden/qt-config.ini"

#Check if eden exists in .config
if [ ! -d "/storage/.config/eden" ]; then
    mkdir -p "/storage/.config/eden"
        cp -r "/usr/config/eden" "/storage/.config/"
fi

#Check if qt-config.ini exists in .config/eden
if [ ! -f "${CONF_FILE}" ]; then
        cp -r "/usr/config/eden/qt-config.ini" "${CONF_FILE}"
fi

#Move Nand / Saves to switch roms folder
if [ ! -d "/storage/roms/bios/eden/nand" ]; then
    mkdir -p "/storage/roms/bios/eden/nand"
fi

rm -rf /storage/.config/eden/nand
ln -sf /storage/roms/bios/eden/nand /storage/.config/eden/nand

#Link eden keys to bios folder
if [ ! -d "/storage/roms/bios/eden/keys" ]; then
    mkdir -p "/storage/roms/bios/eden/keys"
fi

rm -rf /storage/.config/eden/keys
ln -sf /storage/roms/bios/eden/keys /storage/.config/eden/keys

#Link  .config/eden to .local
rm -rf /storage/.local/share/eden
ln -sf /storage/.config/eden /storage/.local/share/eden

# EmulationStation features
GAME=$(echo "${1}" | sed "s#^/.*/##")
PLATFORM=$(echo "${2}" | sed "s#^/.*/##")
LSFG_ENABLE=$(get_setting lsfg_enable "${PLATFORM}" "${GAME}")
LSFG_ENABLE=${LSFG_ENABLE:-0}
LSFG_MULTIPLIER=$(get_setting lsfg_multiplier "${PLATFORM}" "${GAME}")
LSFG_MULTIPLIER=${LSFG_MULTIPLIER:-2}
LSFG_FLOW_SCALE=$(get_setting lsfg_flow_scale "${PLATFORM}" "${GAME}")
LSFG_FLOW_SCALE=${LSFG_FLOW_SCALE:-auto}

# Eden's LSFG implementation reads shaders from a user-owned Lossless.dll.
mkdir -p /storage/.config/eden/lossless /storage/roms/bios/eden
LSFG_TARGET="/storage/.config/eden/lossless/Lossless.dll"
LSFG_DLL_PATH="${LSFG_TARGET}"

if [ ! -f "${LSFG_DLL_PATH}" ]; then
    for candidate in \
        "/storage/roms/bios/eden/Lossless.dll" \
        "/storage/games-internal/roms/steam/steamapps/common/Lossless Scaling/Lossless.dll" \
        "/storage/.local/share/Steam/steamapps/common/Lossless Scaling/Lossless.dll" \
        "/storage/roms/steam/steamapps/common/Lossless Scaling/Lossless.dll"; do
        if [ -f "${candidate}" ]; then
            ln -sf "${candidate}" "${LSFG_TARGET}"
            LSFG_DLL_PATH="${LSFG_TARGET}"
            break
        fi
    done
fi

# Update a QSettings value even when an older config does not contain the new key.
set_eden_renderer_setting() {
    local key="${1}"
    local value="${2}"
    local tmp="${CONF_FILE}.tmp.$$"

    awk -v key="${key}" -v value="${value}" '
        function add_missing() {
            if (!seen_default) print key "\\default=false"
            if (!seen_value) print key "=" value
        }
        /^\[/ {
            if (in_renderer) add_missing()
            in_renderer = ($0 == "[Renderer]")
        }
        in_renderer && index($0, key "\\default=") == 1 {
            print key "\\default=false"
            seen_default = 1
            next
        }
        in_renderer && index($0, key "=") == 1 {
            print key "=" value
            seen_value = 1
            next
        }
        { print }
        END {
            if (in_renderer) add_missing()
        }
    ' "${CONF_FILE}" > "${tmp}" && mv "${tmp}" "${CONF_FILE}"
}

set_eden_renderer_setting frame_gen_multiplier "${LSFG_MULTIPLIER}"
set_eden_renderer_setting frame_gen_target_rate 0
set_eden_renderer_setting frame_gen_queue_target 1
set_eden_renderer_setting frame_gen_fp16 true

if [ "${LSFG_FLOW_SCALE}" = "auto" ]; then
    set_eden_renderer_setting frame_gen_flow_scale_auto true
else
    set_eden_renderer_setting frame_gen_flow_scale_auto false
    set_eden_renderer_setting frame_gen_flow_scale "${LSFG_FLOW_SCALE}"
fi

if [ "${LSFG_ENABLE}" = "1" ] && [ -f "${LSFG_DLL_PATH}" ]; then
    set_eden_renderer_setting frame_gen true
else
    set_eden_renderer_setting frame_gen false
    if [ "${LSFG_ENABLE}" = "1" ]; then
        echo "Eden LSFG disabled: Lossless.dll not found in the Eden BIOS or Steam directories."
    fi
fi

# Eden uses its built-in frame generator; never stack the external LSFG-VK layer on top.
export DISABLE_LSFGVK=1
unset LSFGVK_ENV LSFGVK_DLL_PATH LSFGVK_MULTIPLIER LSFGVK_FLOW_SCALE LSFGVK_PERFORMANCE_MODE LSFGVK_PACING

#Set QT Platform to Wayland-EGL
export QT_QPA_PLATFORM=xcb

#eden won't work with the pipewire driver yet
export SDL_AUDIODRIVER=pulseaudio

set_kill set "-9 eden"

#Run eden emulator
/usr/bin/eden -f "${1}"
