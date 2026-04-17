#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2019-present Shanti Gilbert (https://github.com/shantigilbert)

# Source predefined functions and variables
. /etc/profile

CONFIG_DIR="/storage/.config/scummvm"
ROMSPATH="/storage/roms"
BIOSPATH="${ROMSPATH}/bios"
RATMPCONF="/storage/.config/retroarch/retroarch.cfg"

shopt -s extglob
case "$1" in
  add|create|libretro|grim)
    GAME="$2"
  ;;
  *)
    GAME="$1"
  ;;
esac

if [ ! -d "${CONFIG_DIR}/games" ]
then
  mkdir -p "${CONFIG_DIR}/games"
fi

create_svm(){
  /usr/bin/scummvm --list-targets | tail -n +3 | cut -d " " -f 1 | \
  while read line
  do
    id=($line);
    filename=$(sed -n "/^\[$id\]/,/^\[/{s/^description=//p}" ${CONFIG_DIR}/scummvm.ini | \
      sed -e 's# (.*)# ('${id}')#g' -e "s#'##g" -e "s#: # - #g" \
    )

    SVMPATH="$(sed -n "/^\[$id\]/,/^\[/{s/^path=//p}" ${CONFIG_DIR}/scummvm.ini)"
    echo '--path="'${SVMPATH}'" '${id} >"${CONFIG_DIR}/games/${filename}.scummvm"
  done
}

if [ ! -d "${CONFIG_DIR}" ]; then
 mkdir -p ${CONFIG_DIR}
 cp -rf /usr/config/scummvm/* ${CONFIG_DIR}/
fi

if [ ! -d "/storage/.config/scummvm-grim/" ]; then
    mkdir -p /storage/.config/scummvm-grim
    cp -rf /usr/config/scummvm-grim/* /storage/.config/scummvm-grim/
fi

if [ ! -f "/storage/.config/scummvm-grim/scummvm.ini" ]; then
    mkdir -p /storage/.config/scummvm-grim
    cp -rf /usr/config/scummvm-grim/scummvm.ini /storage/.config/scummvm-grim/scummvm.ini
fi

case $1 in
  "libretro")
    GAME=$(cat "${GAME}" | awk 'BEGIN {FS="\""}; {print $2}')
    cd "${GAME}"
    /usr/bin/retroarch -L /tmp/cores/scummvm_libretro.so --config ${RATMPCONF} .
  ;;

  "add")
    if [ ! -d "${ROMSPATH}/scummvm" ]; then
      mkdir "${ROMSPATH}/scummvm"
    fi
    /usr/bin/scummvm --add --path="${ROMSPATH}/scummvm" --recursive
    mkdir -p ${BIOSPATH}
    cp ${CONFIG_DIR}/scummvm.ini ${BIOSPATH}/scummvm.ini
  ;;

  "create")
    create_svm
  ;;

  "grim")
    set_kill set "-9 scummvm-grim"
    GAME=$(cat "${GAME}")
    GAME=$(echo "${GAME}" | tr -s ' \n' ' ')
    GAMEID=$(echo "${GAME}" | awk '{print $NF}')
    GAMEPATH=$(echo "${GAME}" | sed 's/--path="\(.*\)" .*/\1/')
    if ! grep -q "^\[${GAMEID}\]" /storage/.config/scummvm-grim/scummvm.ini; then
      /usr/bin/scummvm-grim --config=/storage/.config/scummvm-grim/scummvm.ini --add --path="${GAMEPATH}"
    fi

    systemctl start fluidsynth
    eval /usr/bin/scummvm-grim --config=/storage/.config/scummvm-grim/scummvm.ini \
        --fullscreen --joystick=0 \
        --themepath=/usr/config/scummvm-grim/themes --extrapath=/usr/local/share/scummvm-grim "${GAME}"
    systemctl stop fluidsynth
  ;;

  *)
    set_kill set "-9 scummvm"
    GAME=$(cat "${GAME}")
    systemctl start fluidsynth
    eval /usr/bin/scummvm --fullscreen --joystick=0 --themepath=/usr/config/scummvm/themes "${GAME}"
    systemctl stop fluidsynth
  ;;
esac
