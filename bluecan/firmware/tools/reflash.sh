#!/usr/bin/env bash
# Compile and upload a sketch, surviving the ESP32-S3's mid-upload identity switch.
#
#   ./tools/reflash.sh [sketch]        # default: bluecan_bench
#
# Why this exists. esptool resets the board into ROM download mode to flash it, and
# on this board USB is native to the SoC, so that reset is a USB *disconnect*: the
# device leaves as 239a:8123 and comes back as 303a:1001. usbipd attaches a device,
# not a port, so the newly-arrived identity is bound (see ../../wsl-setup.md) but not
# attached, /dev/ttyACM0 vanishes mid-operation, and the upload dies with
# "Could not open /dev/ttyACM0". Then it happens again in reverse when the freshly
# flashed sketch boots.
#
# usbipd's --auto-attach handles this, but it runs as a foreground watcher, which is
# awkward to own from a script. Re-attaching around each transition is simpler and
# leaves nothing running afterwards.
set -euo pipefail
cd "$(dirname "$0")/.."

SKETCH="${1:-bluecan_bench}"

./build.sh "$SKETCH"

for attempt in 1 2 3; do
    ./tools/usb-attach.sh >/dev/null 2>&1 || true
    sleep 2
    if ./build.sh "$SKETCH" --upload 2>&1 | tail -5; then
        echo ">> upload ok on attempt $attempt"
        # The board reboots into the new sketch and re-enumerates once more, so the
        # port only comes back after another attach.
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
