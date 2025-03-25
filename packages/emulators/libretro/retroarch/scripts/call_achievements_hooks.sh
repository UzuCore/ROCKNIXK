#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024-present ROCKNIX (https://github.com/ROCKNIX)

if [ -z "${QUIRK_DEVICE}" ] || [ -z "${HW_DEVICE}" ]; then
  . /etc/profile
fi

if [ -f "/usr/lib/autostart/quirks/devices/${QUIRK_DEVICE}/bin/achievements" ]; then
  Q="/usr/lib/autostart/quirks/devices/${QUIRK_DEVICE}/bin/achievements"
elif [ -f "/usr/lib/autostart/quirks/platforms/${HW_DEVICE}/bin/achievements" ]; then
  Q="/usr/lib/autostart/quirks/platforms/${HW_DEVICE}/bin/achievements"
fi

for F in "${Q}" "/storage/.config/emulationstation/scripts/achievements"; do
  "${F}" "${1}" "${2}" "${3}" &
done
