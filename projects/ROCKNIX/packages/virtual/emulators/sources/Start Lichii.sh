#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://github.com/ROCKNIX)

source /etc/profile

set_kill set "lichii"

sway_fullscreen "lichii" "class" &

/usr/bin/start_lichii.sh >/dev/null 2>&1
