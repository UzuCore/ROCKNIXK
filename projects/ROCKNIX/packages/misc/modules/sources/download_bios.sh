#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
. /etc/profile
export PYTHONDONTWRITEBYTECODE=1
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
exec /usr/bin/python3 /usr/share/bios-downloader/main.py "$@"
