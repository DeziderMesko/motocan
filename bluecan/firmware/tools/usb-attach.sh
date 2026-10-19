#!/usr/bin/env bash
# Attach a USB device from the Windows host into WSL via usbipd-win.
#
# WHAT SURVIVES A REBOOT AND WHAT DOESN'T
#   usbipd bind    -- once per device, needs an ADMINISTRATOR PowerShell,
#                     and persists across reboots and replugs.
#   usbipd attach  -- every session, does NOT need administrator, and is
#                     therefore what this script does for you.
#
# So after a Windows restart you normally only need this script. You only need
# to go back to an admin PowerShell and re-run `bind` if the device was never
# bound, or if you ran `usbipd unbind`.
#
#   Admin PowerShell, once per device:
#       usbipd list
#       usbipd bind --busid <BUSID>
#
# Usage:
#   ./usb-attach.sh              attach every known project device it finds
#   ./usb-attach.sh 1-5          attach one specific busid
#   ./usb-attach.sh --list       just show what Windows sees
set -euo pipefail

USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
[[ -x "$USBIPD" ]] || { echo "usbipd.exe not found. Install it on Windows with:" >&2
                        echo "  winget install --exact --id dorssel.usbipd-win" >&2; exit 1; }

# VID:PID of the devices this project cares about.
#   239a:8123  Feather ESP32-S3 Reverse TFT, running a sketch (USB CDC)
#   239a:0123  ...same board in TinyUF2 bootloader mode (mass storage)
#   303a:1001  ...same board in ESP32-S3 ROM download mode, or running Bluecan, which
#              is built on the chip's own USB serial port; the AtomS3 Lite likewise
#   1d50:606f  CANable with candleLight firmware -> becomes can0
#   16d0:117e  CANable variants also claimed by gs_usb
#   0483:df11  ...same adapter in STM32 ROM bootloader mode (DFU), for reflashing --
#              and where it lands by itself if the 5V rail dips, see ../../bench.md
KNOWN='239a:8123|239a:0123|303a:1001|1d50:606f|16d0:117e|16d0:0f30|0483:df11'

list() { "$USBIPD" list 2>&1 | tr -d '\r'; }

if [[ "${1:-}" == "--list" ]]; then
    list
    exit 0
fi

if [[ -n "${1:-}" ]]; then
    BUSIDS="$1"
else
    BUSIDS=$(list | awk '/^[0-9]+-[0-9]+/ { print $1, $2 }' | grep -Ei "$KNOWN" | awk '{print $1}')
    [[ -n "$BUSIDS" ]] || { echo "No known project device connected." >&2; list >&2; exit 1; }
fi

for b in $BUSIDS; do
    state=$(list | awk -v b="$b" '$1 == b { $1=""; $2=""; print }')
    if [[ "$state" == *Attached* ]]; then
        echo "busid $b already attached"
        continue
    fi
    echo "attaching busid $b"
    # Not shared -> never bound. Say so plainly instead of failing obscurely.
    if [[ "$state" == *"Not shared"* ]]; then
        echo "  busid $b is not bound. In an ADMINISTRATOR PowerShell run:" >&2
        echo "      usbipd bind --busid $b" >&2
        continue
    fi
    "$USBIPD" attach --wsl --busid "$b" 2>&1 | tr -d '\r'
done

# pipefail is on, so build these without pipelines that can "fail" on no match
ttys=$(ls -1 /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | tr '\n' ' ') || true
cans=$(ip -br link show type can 2>/dev/null | awk '{print $1}' | tr '\n' ' ') || true

echo
echo "serial devices: ${ttys:-none}"
echo "can interfaces: ${cans:-none}"
