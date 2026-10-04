#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
. /etc/profile
export QT_QPA_PLATFORM=wayland
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
exec /usr/bin/odin-led-gui
