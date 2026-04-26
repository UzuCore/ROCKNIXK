#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

source /etc/profile

#Check if PCSX2 exists in .config
if [ ! -d "/storage/.config/PCSX2" ]; then
    mkdir -p "/storage/.config/PCSX2"
        cp -r "/usr/config/PCSX2" "/storage/.config/"
fi

#Make PCSX2 bios folder
if [ ! -d "/storage/roms/bios/pcsx2" ]; then
    mkdir -p "/storage/roms/bios/pcsx2"
fi

set_kill set "pcsx2-qt"

#Set OpenGL 3.3 on panfrost
  export MESA_GL_VERSION_OVERRIDE=3.3
  export MESA_GLSL_VERSION_OVERRIDE=330

#Set QT enviornment to wayland
  export QT_QPA_PLATFORM=wayland

sway_fullscreen "pcsx2-qt" &

/usr/share/pcsx2-sa/pcsx2-qt >/dev/null 2>&1
