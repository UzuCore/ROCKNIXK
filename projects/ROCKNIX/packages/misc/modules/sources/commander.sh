#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025 ROCKNIX (https://github.com/ROCKNIX)

. /etc/profile
set_kill set "commander"

# Work around GPU hangs on SM8550
[[ "${HW_DEVICE}" == "SM8550" ]] && export MESA_LOADER_DRIVER_OVERRIDE="zink"

sway_fullscreen "commander" &

/usr/bin/commander
