#!/bin/bash

TITLE="System BIOS Downloader"
WORK_DIR="/tmp/.bios"
BIOS_DIR="/storage/roms/bios"

ZIP_URL="https://github.com/UzuCore/minimal-Bios/archive/refs/heads/main.zip"
ZIP_FILE="$WORK_DIR/main.zip"
SRC_DIR="$WORK_DIR/minimal-Bios-main"

msg() {
	command -v message_stream >/dev/null 2>&1 && message_stream "$1" || echo "$1"
}

fail() {
	text_viewer -m "$1" -t "$TITLE"
	exit 1
}

cleanup() {
	rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$WORK_DIR" || fail "Failed to create working directory"

text_viewer -w -y -t "$TITLE" -m "\nDownload BIOS from the network."
[ $? -ne 21 ] && exit 0

ping -q -c 1 -W 2 github.com >/dev/null 2>&1 || fail "Network error"

# 1. Download
msg "[1/4] Downloading..."
if command -v curl >/dev/null 2>&1; then
	curl -L --progress-bar -o "$ZIP_FILE" "$ZIP_URL" || fail "Download failed"
else
	wget --no-check-certificate -O "$ZIP_FILE" "$ZIP_URL" || fail "Download failed"
fi

[ -s "$ZIP_FILE" ] || fail "Downloaded file is empty"

# 2. Verify
msg "[2/4] Verifying..."
unzip -t "$ZIP_FILE" >/dev/null 2>&1 || fail "Invalid ZIP file"

# 3. Extract
msg "[3/4] Extracting..."
rm -rf "$SRC_DIR"
unzip -oq "$ZIP_FILE" -d "$WORK_DIR" || fail "Extraction failed"

[ -d "$SRC_DIR" ] || fail "Extracted folder not found"

# 4. Install
msg "[4/4] Installing..."
mkdir -p "$BIOS_DIR"
cp -a "$SRC_DIR"/. "$BIOS_DIR"/ || fail "Installation failed"

COUNT=$(find "$SRC_DIR" -type f | wc -l)

text_viewer -m "Done!\nFiles installed: $COUNT" -t "$TITLE"
