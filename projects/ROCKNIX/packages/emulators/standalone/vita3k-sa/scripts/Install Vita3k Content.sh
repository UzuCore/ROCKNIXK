#!/bin/bash

# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present ROCKNIX (https://rocknix.org/)

ROMS_PATH="/storage/roms/psvita"

#Iterate on the PUP Files and install them
cd $ROMS_PATH

for file in *.zip
do
echo "$file"
[ -f "$file" ] || continue
/usr/bin/Vita3K "$file"
rm $file
done
