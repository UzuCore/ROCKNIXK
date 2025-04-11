#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

if [ -z "${QUIRK_DEVICE}" ] || [ -z "${HW_DEVICE}" ]; then
  . /etc/profile
fi

if [ -f "/usr/lib/autostart/quirks/devices/${QUIRK_DEVICE}/bin/achievements" ]; then
  "/usr/lib/autostart/quirks/devices/${QUIRK_DEVICE}/bin/achievements" $* &
elif [ -f "/usr/lib/autostart/quirks/platforms/${HW_DEVICE}/bin/achievements" ]; then
  "/usr/lib/autostart/quirks/platforms/${HW_DEVICE}/bin/achievements" $* &
fi
