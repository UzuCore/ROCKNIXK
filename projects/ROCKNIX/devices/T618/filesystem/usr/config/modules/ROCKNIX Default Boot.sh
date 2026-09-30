#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Toggle "keep ROCKNIX as the default boot". Android stays one menu entry away ("Reboot to Android"), and if
# ROCKNIX fails to start several times in a row U-Boot still falls back to Android on its own.
F=/storage/.config/rg405m/allow-boot-control-write
if [ -f "$F" ]; then
  rm -f "$F"
  MSG="ROCKNIX default boot: OFF\n||After a few reboots the device returns to Android."
else
  # ROCKNIX can live in either slot (a stock OTA moves Android between slots), so approve the slot we booted from.
  SUFFIX=$(sed -n 's/.*androidboot\.slot_suffix=\(_[ab]\).*/\1/p' /proc/cmdline)
  case "$SUFFIX" in
    _a|_b) ;;
    *) /usr/bin/sdl2notify "Boot control not changed\n||Running slot unknown." 255 255 255 any; exit 1 ;;
  esac
  mkdir -p "${F%/*}" && echo "$SUFFIX" > "$F"
  if /usr/bin/python3 /usr/lib/rg405m/boot_success.py --commit --refresh-only; then
    MSG="ROCKNIX default boot: ON\n||Use Reboot to Android to switch."
  else
    rm -f "$F"
    MSG="Boot control not changed\n||Nothing was written."
  fi
fi
/usr/bin/sdl2notify "$MSG" 255 255 255 any
