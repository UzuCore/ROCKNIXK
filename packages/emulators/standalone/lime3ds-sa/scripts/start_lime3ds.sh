#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

. /etc/profile
set_kill set "-9 lime3ds"

# Load gptokeyb support files
control-gen_init.sh
source /storage/.config/gptokeyb/control.ini
get_controls

# Make sure Lime3DS config directory exists
[ ! -d /storage/.config/lime3ds ] && cp -r /usr/config/lime3ds /storage/.config

# Move sdmc & nand to 3ds roms folder
[ ! -d /storage/roms/3ds/lime3ds/sdmc ] && mkdir -p /storage/roms/3ds/lime3ds/sdmc
rm -rf /storage/.config/lime3ds/sdmc
ln -sf /storage/roms/3ds/lime3ds/sdmc /storage/.config/lime3ds/sdmc

[ ! -d /storage/roms/3ds/lime3ds/nand ] && mkdir -p /storage/roms/3ds/lime3ds/nand
rm -rf /storage/.config/lime3ds/nand
ln -sf /storage/roms/3ds/lime3ds/nand /storage/.config/lime3ds/nand

# RK3588 - handle different config files for ACE / CM5
if [ "${HW_DEVICE}" = "RK3588" ] && [ ! -f "/storage/.config/lime3ds/qt-config.ini" ]; then
  if echo ${QUIRK_DEVICE} | grep CM5; then
    cp /usr/config/lime3ds/qt-config_CM5.ini /storage/.config/lime3ds/qt-config.ini
  else
    cp /usr/config/lime3ds/qt-config_ACE.ini /storage/.config/lime3ds/qt-config.ini
  fi
fi

# Make sure QT config file exists
[ ! -f "/storage/.config/lime3ds/qt-config.ini" ] && cp /usr/config/lime3ds/qt-config.ini /storage/.config/lime3ds

# Make sure gptokeyb mapping file exists
[ ! -f "/storage/.config/lime3ds/lime3ds.gptk" ] && cp /usr/config/lime3ds/lime3ds.gptk /storage/.config/lime3ds

# Emulation Station Features
GAME=$(echo "${1}"| sed "s#^/.*/##")
PLATFORM=$(echo "${2}"| sed "s#^/.*/##")
CPU=$(get_setting cpu_speed "${PLATFORM}" "${GAME}")
EMOUSE=$(get_setting emulate_mouse "${PLATFORM}" "${GAME}")
RENDERER=$(get_setting graphics_backend "${PLATFORM}" "${GAME}")
RES=$(get_setting resolution_scale "${PLATFORM}" "${GAME}")
ROTATE=$(get_setting rotate_screen "${PLATFORM}" "${GAME}")
SLAYOUT=$(get_setting screen_layout "${PLATFORM}" "${GAME}")
CSHADERS=$(get_setting cache_shaders "${PLATFORM}" "${GAME}")
HSHADERS=$(get_setting hardware_shaders "${PLATFORM}" "${GAME}")
ACCURATE_HW_SHADERS=$(get_setting accurate_hardware_shaders "${PLATFORM}" "${GAME}")

# CPU Underclock
sed -i '/^cpu_clock_percentage\\default=/c\cpu_clock_percentage\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${CPU}" in
  0) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=100' /storage/.config/lime3ds/qt-config.ini;;
  1) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=90' /storage/.config/lime3ds/qt-config.ini;;
  2) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=80' /storage/.config/lime3ds/qt-config.ini;;
  3) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=70' /storage/.config/lime3ds/qt-config.ini;;
  4) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=60' /storage/.config/lime3ds/qt-config.ini;;
  5) sed -i '/^cpu_clock_percentage=/c\cpu_clock_percentage=50' /storage/.config/lime3ds/qt-config.ini;;
esac

# Resolution Scale
sed -i '/^resolution_factor\\default=/c\resolution_factor\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${RES}" in
  0) sed -i '/^resolution_factor=/c\resolution_factor=0' /storage/.config/lime3ds/qt-config.ini;;
  1) sed -i '/^resolution_factor=/c\resolution_factor=1' /storage/.config/lime3ds/qt-config.ini;;
  2) sed -i '/^resolution_factor=/c\resolution_factor=2' /storage/.config/lime3ds/qt-config.ini;;
  3) sed -i '/^resolution_factor=/c\resolution_factor=3' /storage/.config/lime3ds/qt-config.ini;;
esac

# Rotate Screen
sed -i '/^upright_screen\\default=/c\upright_screen\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${ROTATE}" in
  0) sed -i '/^upright_screen=/c\upright_screen=false' /storage/.config/lime3ds/qt-config.ini;;
  1) sed -i '/^upright_screen=/c\upright_screen=true' /storage/.config/lime3ds/qt-config.ini;;
esac

# Cache Shaders
sed -i '/^use_disk_shader_cache\\default=/c\use_disk_shader_cache\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${CSHADERS}" in
  0) sed -i '/^use_disk_shader_cache=/c\use_disk_shader_cache=false' /storage/.config/lime3ds/qt-config.ini;;
  1) sed -i '/^use_disk_shader_cache=/c\use_disk_shader_cache=true' /storage/.config/lime3ds/qt-config.ini;;
esac

# Hardware Shaders
sed -i '/^use_hw_shader\\default=/c\use_hw_shader\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${HSHADERS}" in
  1) sed -i '/^use_hw_shader=/c\use_hw_shader=true' /storage/.config/lime3ds/qt-config.ini;;
  *) sed -i '/^use_hw_shader=/c\use_hw_shader=false' /storage/.config/lime3ds/qt-config.ini;;
esac

# Use accurate multiplication in hardware shaders
sed -i '/^shaders_accurate_mul\\default=/c\shaders_accurate_mul\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${ACCURATE_HW_SHADERS}" in
  1) sed -i '/^shaders_accurate_mul=/c\shaders_accurate_mul=true' /storage/.config/lime3ds/qt-config.ini;;
  *) sed -i '/^shaders_accurate_mul=/c\shaders_accurate_mul=false' /storage/.config/lime3ds/qt-config.ini;;
esac

# Screen Layout
sed -i '/^layout_option\\default=/c\layout_option\\default=false' /storage/.config/lime3ds/qt-config.ini
sed -i '/^swap_screen\\default=/c\swap_screen\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${SLAYOUT}" in
  0)
    # Default (Top / Bottom)
    sed -i '/^layout_option=/c\layout_option=0' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=false' /storage/.config/lime3ds/qt-config.ini
    ;;
  1a)
    # Single Screen (TOP)
    sed -i '/^layout_option=/c\layout_option=1' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=false' /storage/.config/lime3ds/qt-config.ini
    ;;
  1b)
    # Single Screen (BOTTOM)
    sed -i '/^layout_option=/c\layout_option=1' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=true' /storage/.config/lime3ds/qt-config.ini
    ;;
  2)
    # Large Screen, Small Screen
    sed -i '/^layout_option=/c\layout_option=2' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=false' /storage/.config/lime3ds/qt-config.ini
    ;;
  3)
    # Side by Side
    sed -i '/^layout_option=/c\layout_option=3' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=false' /storage/.config/lime3ds/qt-config.ini
    ;;
  4)
    # Hybrid
    sed -i '/^layout_option=/c\layout_option=5' /storage/.config/lime3ds/qt-config.ini
    sed -i '/^swap_screen=/c\swap_screen=false' /storage/.config/lime3ds/qt-config.ini
    ;;
esac

# Video Backend
sed -i '/^graphics_api\\default=/c\graphics_api\\default=false' /storage/.config/lime3ds/qt-config.ini

case "${RENDERER}" in
  1) sed -i '/^graphics_api=/c\graphics_api=1' /storage/.config/lime3ds/qt-config.ini;;
  *) sed -i '/^graphics_api=/c\graphics_api=2' /storage/.config/lime3ds/qt-config.ini;;
esac

rm -rf /storage/.local/share/lime3ds
ln -sf /storage/.config/lime3ds /storage/.local/share/lime3ds

# Run Lime Emulator
if [ "${EMOUSE}" = "0" ]; then
  /usr/bin/lime3ds "${1}"
else
  ${GPTOKEYB} lime3ds -c /storage/.config/lime3ds/lime3ds.gptk &
  /usr/bin/lime3ds "${1}"
  kill -9 "$(pidof gptokeyb)"
fi
