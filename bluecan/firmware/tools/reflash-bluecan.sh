#!/usr/bin/env bash
# Compile and upload Bluecan, the reader firmware in ../bluecan/.
#
#   ./tools/reflash-bluecan.sh              # release build: silent on serial, BLE only
#   ./tools/reflash-bluecan.sh --debug      # adds -DDEBUG: 5 s stats over serial
#   ./tools/reflash-bluecan.sh --like-lite  # the Feather standing in for an AtomS3 Lite
#   ./tools/reflash-bluecan.sh --lite       # for an M5Stack AtomS3 Lite itself
#   ./tools/reflash-bluecan.sh --sd         # log every frame to a microSD card
#   ./tools/reflash-bluecan.sh --sd-emulate # ...and drive the SPI bus as one would if none
#   ./tools/reflash-bluecan.sh --recording  # bench: filter as RaceChrono does when it records
#   ./tools/reflash-bluecan.sh --standby    # sleep when the bus is silent (on for --lite)
#   ./tools/reflash-bluecan.sh --no-upload  # compile only
#
# The flags combine. --like-lite builds for the Feather as the Lite has it: no display,
# buttons, status pixel or fuel gauge (CONFIG_LIKE_ATOMS3_LITE), no PSRAM, and the
# chip's own USB serial port instead of TinyUSB, so the board appears as 303a:1001
# while it runs. A plain build afterwards puts everything back.
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

BOARD="feather"
DEBUG=""
SD=""
RECORDING=""
STANDBY=""
UPLOAD=1
for a in "$@"; do
    case "$a" in
        --debug)     DEBUG=1 ;;
        --like-lite) BOARD="like-lite" ;;
        --lite)      BOARD="lite" ;;
        --sd)        SD="sd" ;;
        --sd-emulate) SD="sd-emulate" ;;
        --recording) RECORDING=1 ;;
        --standby)   STANDBY=1 ;;
        --no-upload) UPLOAD="" ;;
        *)           echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

FLAGS=()
case "$BOARD" in
    feather)
        FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
        OPTS="PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M"
        BUILD="build/bluecan" ;;
    like-lite)
        FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
        OPTS="PartitionScheme=tinyuf2_noota,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M"
        FLAGS+=(-DCONFIG_LIKE_ATOMS3_LITE)
        BUILD="build/bluecan-like-lite" ;;
    lite)
        FQBN="esp32:esp32:m5stack_atoms3"
        OPTS="PartitionScheme=default_8MB,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M"
        BUILD="build/bluecan-lite" ;;
esac
case "$SD" in
    sd)         FLAGS+=(-DCONFIG_SDLOG); BUILD="$BUILD-sd" ;;
    sd-emulate) FLAGS+=(-DCONFIG_SDLOG -DCONFIG_SDLOG_EMULATE); BUILD="$BUILD-sdemu" ;;
esac
if [[ -n "$STANDBY" ]]; then
    FLAGS+=(-DCONFIG_STANDBY)
    BUILD="$BUILD-standby"
fi
if [[ -n "$RECORDING" ]]; then
    FLAGS+=(-DCONFIG_RC_BENCH_RECORDING)
    BUILD="$BUILD-rec"
fi
if [[ -n "$DEBUG" ]]; then
    FLAGS+=(-DDEBUG)
    BUILD="$BUILD-debug"
    echo ">> DEBUG build"
fi
EXTRA=()
if (( ${#FLAGS[@]} )); then
    EXTRA=(--build-property "compiler.cpp.extra_flags=${FLAGS[*]}")
fi
mkdir -p "$BUILD"

echo ">> compiling $BLUECAN for $BOARD"
arduino-cli compile --fqbn "$FQBN:$OPTS" "${EXTRA[@]}" --build-path "$BUILD" "$BLUECAN"

[[ -n "$UPLOAD" ]] || exit 0

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
