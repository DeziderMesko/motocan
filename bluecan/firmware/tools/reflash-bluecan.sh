#!/usr/bin/env bash
# Compile and upload Bluecan, the reader firmware in ../bluecan/.
#
#   ./tools/reflash-bluecan.sh              # release build: silent on serial, BLE only
#   ./tools/reflash-bluecan.sh --debug      # adds -DDEBUG: 5 s stats over serial
#
# Why this exists next to reflash.sh: that one goes through build.sh, which has no debug
# build. This one takes --debug, and otherwise performs the same usb-attach dance around
# the ESP32-S3's mid-upload identity switch under WSL2. See ../../build.md.
#
# Reading the board afterwards: ./tools/bench.py 10 works as a plain serial reader for
# any sketch -- what matters is that it asserts DTR, which the ESP32-S3's USB CDC gates
# its output on. A `cat /dev/ttyACM0` shows nothing and looks exactly like a dead board.
# The release build prints nothing anyway; only --debug does.
set -euo pipefail
cd "$(dirname "$0")/.."

BLUECAN="bluecan"

FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
OPTS="PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M"

BUILD="build/bluecan"
EXTRA=()
if [[ "${1:-}" == "--debug" ]]; then
    BUILD="build/bluecan-debug"
    EXTRA=(--build-property "compiler.cpp.extra_flags=-DDEBUG")
    echo ">> DEBUG build"
fi
mkdir -p "$BUILD"

echo ">> compiling $BLUECAN"
arduino-cli compile --fqbn "$FQBN:$OPTS" "${EXTRA[@]}" --build-path "$BUILD" "$BLUECAN"

for attempt in 1 2 3; do
    ./tools/usb-attach.sh >/dev/null 2>&1 || true
    sleep 2
    if arduino-cli upload --fqbn "$FQBN:$OPTS" --input-dir "$BUILD" --port /dev/ttyACM0 "$BLUECAN" 2>&1 | tail -3; then
        echo ">> upload ok on attempt $attempt"
        sleep 3
        ./tools/usb-attach.sh >/dev/null 2>&1 || true
        sleep 2
        ls -l /dev/ttyACM* 2>/dev/null || echo "!! no serial port; run ./tools/usb-attach.sh"
        exit 0
    fi
    echo ">> attempt $attempt failed (expected on the first pass); re-attaching"
done

echo "!! upload failed three times" >&2
exit 1
