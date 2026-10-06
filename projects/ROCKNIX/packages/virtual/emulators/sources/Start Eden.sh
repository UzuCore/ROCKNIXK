#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2023 JELOS (https://github.com/JustEnoughLinuxOS)

if [ "${HW_DEVICE}" = "SDM845" ]; then
  exec /usr/bin/start_eden.sh "$@"
fi

source /etc/profile

export QT_QPA_PLATFORM=xcb

set_kill set "eden"

sway_fullscreen "eden" "class" &

/usr/bin/eden >/dev/null 2>&1
