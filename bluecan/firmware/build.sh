#!/usr/bin/env bash
# Build a bench sketch and produce a .uf2 for drag-and-drop flashing. Bluecan itself
# is built by tools/reflash-bluecan.sh.
#
# Usage:  ./build.sh [sketch_dir] [--upload|--flash]
#
#   (no flag)   compile only, and produce a .uf2
#   --upload    compile, then upload over serial via usbipd passthrough
#   --flash     compile, then copy the .uf2 to the TinyUF2 bootloader drive
#
# --upload is the normal path now that usbipd-win is set up: it needs no button
# presses. Run ./tools/usb-attach.sh first if /dev/ttyACM0 is missing.
#
# --flash is the fallback that needs no USB passthrough at all: the board's
# TinyUF2 bootloader exposes a mass-storage drive (FTHRS3BOOT) that Windows
# mounts and WSL sees under /mnt. Useful if passthrough misbehaves, which it can
# during the ESP32-S3's reset-into-bootloader re-enumeration. See ../history/wsl-setup.md.
#
# Both depend on the tinyuf2_noota partition scheme, which keeps the UF2
# bootloader in its own partition at 0x2d0000. Do not switch to huge_app: it
# overwrites the bootloader and you lose the fallback.
set -euo pipefail

SKETCH="bluecan_bringup"
MODE="build"
for a in "$@"; do
    case "$a" in
        --upload) MODE="upload" ;;
        --flash)  MODE="flash" ;;
        -*)       echo "unknown option: $a" >&2; exit 2 ;;
        *)        SKETCH="${a%/}" ;;
    esac
done

cd "$(dirname "$0")"

# Bluecan has board profiles, and their options are kept in one place.
if [[ "$SKETCH" == "bluecan" ]]; then
    echo "!! Bluecan is built by profile: ./tools/reflash-bluecan.sh [feather|atom]" >&2
    echo "   (--no-upload compiles only; the feather profile also writes a .uf2)" >&2
    exit 2
fi

FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
OPTS="PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M"

# App partition offset from tinyuf2-partitions-4MB-noota.csv, and the ESP32-S3
# UF2 family id from microsoft/uf2 uf2families.json.
APP_OFFSET=0x10000
FAMILY=0xc47e5767

BUILD="build/$SKETCH"
mkdir -p "$BUILD"

echo ">> compiling $SKETCH"
arduino-cli compile --fqbn "$FQBN:$OPTS" --build-path "$BUILD" "$SKETCH"

BIN="$BUILD/$SKETCH.ino.bin"
UF2="$BUILD/$SKETCH.uf2"

echo ">> converting to uf2"
python3 tools/uf2conv.py "$BIN" --base "$APP_OFFSET" --family "$FAMILY" --convert --output "$UF2"
ls -lh "$UF2"

if [[ "$MODE" == "upload" ]]; then
    PORT="${PORT:-/dev/ttyACM0}"
    if [[ ! -e "$PORT" ]]; then
        echo "!! $PORT missing. Attach the board into WSL first:"
        echo "   ./tools/usb-attach.sh"
        exit 1
    fi
    echo ">> uploading to $PORT"
    arduino-cli upload --fqbn "$FQBN:$OPTS" --input-dir "$BUILD" --port "$PORT" "$SKETCH"
    echo ">> done. The board resets and re-enumerates; if the port vanishes,"
    echo "   re-run ./tools/usb-attach.sh before opening the serial monitor."
fi

if [[ "$MODE" == "flash" ]]; then
    # The bootloader drive only exists while the board is in bootloader mode
    # (double-tap RESET). Its label is FTHRS3BOOT.
    DRIVE=""
    for d in /mnt/[a-z]; do
        [[ -e "$d/INFO_UF2.TXT" ]] && DRIVE="$d" && break
    done
    if [[ -z "$DRIVE" ]]; then
        echo "!! no UF2 drive found under /mnt."
        echo "   Double-tap the RESET button, wait for FTHRS3BOOT to appear in Windows,"
        echo "   then re-run with --flash."
        exit 1
    fi
    echo ">> flashing to $DRIVE ($(grep -i model "$DRIVE/INFO_UF2.TXT" 2>/dev/null | tr -d '\r'))"
    cp "$UF2" "$DRIVE/"
    sync
    echo ">> done. The board reboots into the new firmware automatically."
fi
