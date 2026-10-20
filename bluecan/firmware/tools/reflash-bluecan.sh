#!/usr/bin/env bash
# Compile and upload Bluecan, the reader firmware in ../bluecan/.
#
#   ./tools/reflash-bluecan.sh [profile] [flags]
#
# The profile is the board, and with it what the board has. Its pins and parts are in
# ../bluecan/src/boards/, one file per profile:
#
#   feather          the Feather ESP32-S3 Reverse TFT, with its display (the default)
#   atom             an M5Stack AtomS3 Lite on a Unit Mini CAN; sleeps when the bus is silent
#   feather-as-atom  TEMPORARY: the Feather standing in for an AtomS3 Lite -- the
#                    Feather's pins, the Lite's parts. Goes when a Lite is on the bench
#
# The flags are for the bench, and combine:
#
#   --debug       adds -DDEBUG: 5 s stats over serial
#   --sd-emulate  with no card at start-up, drive the SPI bus as one would
#   --recording   filter as RaceChrono does when it records, whatever it asks for
#   --no-standby  keep atom and feather-as-atom awake on a silent bus
#   --quick       short timeouts, for trying them out: the Wi-Fi mode gives way after
#                 30 s without a request instead of 5 min, standby sleeps after 15 s
#   --no-upload   compile only
#
# Why this exists next to reflash.sh: that one goes through build.sh, which has no debug
# build. This one takes --debug, and otherwise performs the same usb-attach dance around
# the ESP32-S3's mid-upload reset under WSL2. See ../../build.md.
#
# Reading the board afterwards: ./tools/console-log.sh keeps the console in a file,
# across sleeps and wakes; ./tools/bench.py 10 reads it for ten seconds, and a plain
# `cat /dev/ttyACM0` works too. Read it from WSL, not Windows, whose opening the port
# resets the board. The release build prints little; --debug adds the statistics.
set -euo pipefail
cd "$(dirname "$0")/.."

BLUECAN="bluecan"

PROFILE="feather"
DEBUG=""
SD_EMULATE=""
RECORDING=""
NO_STANDBY=""
QUICK=""
UPLOAD=1
for a in "$@"; do
    case "$a" in
        feather|atom|feather-as-atom) PROFILE="$a" ;;
        --debug)      DEBUG=1 ;;
        --sd-emulate) SD_EMULATE=1 ;;
        --recording)  RECORDING=1 ;;
        --no-standby) NO_STANDBY=1 ;;
        --quick)      QUICK=1 ;;
        --no-upload)  UPLOAD="" ;;
        *)            echo "unknown profile or option: $a" >&2; exit 2 ;;
    esac
done

# The Feather is built with the Lite's options -- PSRAM disabled, USBMode=hwcdc -- so
# that the bench measures what the Lite will run. Nothing depends on that: setting
# PSRAM=enabled and leaving USBMode out (TinyUSB) puts the Feather's own back with no
# other change. See ../bluecan/src/boards/feather_revtft.hpp.
FEATHER_FQBN="esp32:esp32:adafruit_feather_esp32s3_reversetft"
FEATHER_OPTS="PartitionScheme=tinyuf2_noota,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M"

FLAGS=()
case "$PROFILE" in
    feather)
        FQBN="$FEATHER_FQBN"; OPTS="$FEATHER_OPTS" ;;
    feather-as-atom)
        FQBN="$FEATHER_FQBN"; OPTS="$FEATHER_OPTS"
        FLAGS+=(-DBOARD_FEATHER_AS_ATOM) ;;
    atom)
        FQBN="esp32:esp32:m5stack_atoms3"
        OPTS="PartitionScheme=default_8MB,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M" ;;
esac
BUILD="build/bluecan-$PROFILE"
if [[ -n "$SD_EMULATE" ]]; then
    FLAGS+=(-DCONFIG_SDLOG_EMULATE)
    BUILD="$BUILD-sdemu"
fi
if [[ -n "$NO_STANDBY" ]]; then
    FLAGS+=(-DCONFIG_NO_STANDBY)
    BUILD="$BUILD-nostandby"
fi
if [[ -n "$QUICK" ]]; then
    FLAGS+=(-DCONFIG_WIFI_IDLE_S=30 -DCONFIG_STANDBY_TIMEOUT_S=15)
    BUILD="$BUILD-quick"
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

# The Wi-Fi mode's password, in a file git ignores: made once, with a random one, the
# first time anything is built here. Without letters and digits that read alike, for
# typing into a phone.
SECRET="$BLUECAN/src/web/secret.hpp"
if [[ ! -f "$SECRET" ]]; then
    PASS=$(python3 -c 'import secrets; print("".join(secrets.choice("abcdefghjkmnpqrstuvwxyz23456789") for _ in range(12)))')
    printf '// Written by tools/reflash-bluecan.sh, and ignored by git: the Wi-Fi mode'"'"'s password.\n#define CONFIG_WIFI_PASSWORD "%s"\n' "$PASS" > "$SECRET"
    echo ">> Wi-Fi password, kept in $SECRET: $PASS"
fi

echo ">> compiling $BLUECAN, profile $PROFILE"
arduino-cli compile --fqbn "$FQBN:$OPTS" "${EXTRA[@]}" --build-path "$BUILD" "$BLUECAN"

# The Feather's UF2 bootloader takes a .uf2: app partition offset from
# tinyuf2-partitions-4MB-noota.csv, ESP32-S3 family id from microsoft/uf2. Double-tap
# RESET and copy it onto FTHRS3BOOT; see ../../build.md.
if [[ "$FQBN" == "$FEATHER_FQBN" ]]; then
    python3 tools/uf2conv.py "$BUILD/$BLUECAN.ino.bin" --base 0x10000 --family 0xc47e5767 \
        --convert --output "$BUILD/$BLUECAN.uf2" >/dev/null
    echo ">> $BUILD/$BLUECAN.uf2"
fi

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
