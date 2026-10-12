#!/usr/bin/env bash
# Compile and upload Bluecan, the reader firmware, which lives OUTSIDE this repo.
#
#   ./tools/reflash-bluecan.sh              # release build: silent on serial, BLE only
#   ./tools/reflash-bluecan.sh --debug      # adds -DDEBUG: 5 s stats over serial
#
# Bluecan is a separate repository -- it is forked from an upstream RaceChrono
# project and keeps that remote -- so it is checked out beside motocan rather than
# inside it. Override with BLUECAN=/path/to/checkout.
#
#   https://github.com/DeziderMesko/racechrono-canbus   (branch trunk)
#
# Why this exists next to reflash.sh: that script builds sketches from this repo's
# firmware/ directory and produces a .uf2. This one takes a sketch directory that is
# not ours, and otherwise performs the same usb-attach dance around the ESP32-S3's
# mid-upload identity switch. See ../../wsl-setup.md for what that dance is for.
#
# Reading the board afterwards: ./tools/bench.py 10 works as a plain serial reader for
# any sketch -- what matters is that it asserts DTR, which the ESP32-S3's USB CDC gates
# its output on. A `cat /dev/ttyACM0` shows nothing and looks exactly like a dead board.
# The release build prints nothing anyway; only --debug does.
set -euo pipefail
cd "$(dirname "$0")/.."

BLUECAN="${BLUECAN:-$HOME/projects/racechrono-canbus}"
[[ -d "$BLUECAN" ]] || { echo "Bluecan checkout not found at $BLUECAN" >&2
                      echo "  git clone https://github.com/DeziderMesko/racechrono-canbus \"$BLUECAN\"" >&2
                      exit 1; }

FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
OPTS="PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M"

BUILD="build/racechrono-canbus"
EXTRA=()
if [[ "${1:-}" == "--debug" ]]; then
    BUILD="build/racechrono-canbus-debug"
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
