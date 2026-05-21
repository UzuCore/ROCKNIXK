#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2022-present JELOS (https://github.com/JustEnoughLinuxOS)

. /etc/profile
. /etc/os-release

set_kill set "-9 drastic"

#Get game/platform info
GAME=$(echo "${1}"| sed "s#^/.*/##")
PLATFORM="nds"

#load gptokeyb support files
control-gen_init.sh
source /storage/.config/gptokeyb/control.ini
get_controls

#Copy drastic files to .config
if [ ! -d "/storage/.config/advancedrastic" ]; then
  mkdir -p /storage/.config/advancedrastic/
  cp -r /usr/config/advancedrastic/* /storage/.config/advancedrastic/
fi

if [ ! -d "/storage/.config/advancedrastic/system" ]; then
  mkdir -p /storage/.config/advancedrastic/system
fi

for bios in nds_bios_arm9.bin nds_bios_arm7.bin
do
  if [ ! -e "/storage/.config/advancedrastic/system/${bios}" ]; then
     if [ -e "/storage/roms/bios/${bios}" ]; then
       ln -sf /storage/roms/bios/${bios} /storage/.config/advancedrastic/system
     fi
  fi
done

#Copy drastic files to .config
if [ ! -f "/storage/.config/advancedrastic/drastic.gptk" ]; then
  cp -r /usr/config/advancedrastic/drastic.gptk /storage/.config/advancedrastic/
fi

#Make drastic savestate folder
if [ ! -d "/storage/roms/savestates/nds" ]; then
  mkdir -p /storage/roms/savestates/nds
fi

#Link savestates to roms/savestates/nds
rm -rf /storage/.config/advancedrastic/savestates
ln -sf /storage/roms/savestates/nds /storage/.config/advancedrastic/savestates

#Link saves to roms/nds/saves
rm -rf /storage/.config/advancedrastic/backup
ln -sf /storage/roms/nds /storage/.config/advancedrastic/backup

if [ "$QUIRK_DEVICE" = "Anbernic RG DS" ]; then
 	echo 'for_window [app_id="drastic"] output DSI-2 pos 0 0, output DSI-1 power on pos 0 480' >> /storage/.config/sway/config 
 	echo 'for_window [app_id="drastic"] floating enable, border none, fullscreen disable, resize set 640 960, move to output DSI-2, move absolute position 0 0' >> /storage/.config/sway/config 
	swaymsg reload
fi

cd /storage/.config/advancedrastic/
@HOTKEY@

$GPTOKEYB "drastic" -c "drastic.gptk" &
# Fix actual touch inputs by replacing touch->mouse translation and add hw mic support
export LD_PRELOAD=/storage/.config/advancedrastic/libs/libadvdrastic.so
export SDL_TOUCH_MOUSE_EVENTS="0"
export DSHOOK_MIC_THRESH="${MICTHRESH}"
./drastic "$1"
kill -9 $(pidof gptokeyb)

if [ "$QUIRK_DEVICE" = "Anbernic RG DS" ]; then
	sed -i '/pos 0 480/d' /storage/.config/sway/config
	sed -i '/resize set 640 960/d' /storage/.config/sway/config
	swaymsg reload
fi
