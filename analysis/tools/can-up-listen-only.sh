#!/usr/bin/env bash
# Bring a SocketCAN interface up LISTEN-ONLY. Needs root; 2-session.sh calls it with
# sudo on that one line so everything the session writes stays user-owned.
#
#   sudo ./tools/can-up-listen-only.sh [bitrate] [iface]
#   sudo ./tools/can-up-listen-only.sh --down [iface]
#
# ONE mode on purpose. The bench has a fuller can-up.sh with loopback, normal and
# one-shot, and it lives with the bench because those modes are bench answers: on the
# desk the adapter must ACK the lone reader, here it must never ACK anything. The bike
# is a bus shared with the ECU, ABS and IMU, and an adapter that ACKs or emits error
# frames onto it is the one mistake with consequences outside the log. So the analysis
# half carries a copy that cannot be asked for any other mode.
#
# That still holds for this file. Since 2026-10-11 there is a second one, can-up-ack.sh,
# for the case the first capture turned up -- a node that only resends because nobody
# answers it. It is a separate script with its own guards so that this one stays unable
# to do anything but listen.
#
# 500 kbps is the R9's rate per ../r9-signals.md, so it is the default.
set -euo pipefail

BITRATE=500000
IF="${IF:-can0}"
DOWN=0
for a in "$@"; do
    case "$a" in
        --down)  DOWN=1 ;;
        [0-9]*)  BITRATE="$a" ;;
        *)       IF="$a" ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "needs root: sudo $0 $*" >&2
    exit 1
fi

ip link show "$IF" >/dev/null 2>&1 || {
    cat >&2 <<EOF
$IF does not exist -- the adapter is not attached to this WSL yet.

With the CANable plugged in, from an administrator PowerShell on Windows:

  winget install --exact dorssel.usbipd-win          # once per machine, if missing
  usbipd list                                        # find the CANable's BUSID
  usbipd bind --busid <BUSID>                        # once per device, persists
  usbipd attach --wsl --busid <BUSID> --auto-attach  # per session, WSL must be running

It then shows up as $IF in here. usbipd is a Windows program; the usbip inside WSL is
the other end of the link, not the same tool.
EOF
    exit 1
}

if (( DOWN )); then
    ip link set "$IF" down && echo "$IF down"
    exit 0
fi

ip link set "$IF" down 2>/dev/null || true

# Control modes are sticky: bringing an interface up without naming a mode keeps
# whatever was set last. "loopback off" is spelled out for that reason -- an earlier
# bench session in loopback would otherwise survive into this one silently.
echo ">> $IF up: bitrate $BITRATE (listen-only on, loopback off)"
if ! ip link set "$IF" up type can bitrate "$BITRATE" listen-only on loopback off; then
    echo "!! failed. If the firmware does not advertise listen-only, STOP -- do not" >&2
    echo "   fall back to normal mode on the bike." >&2
    exit 1
fi

# txqueuelen defaults to 10, which is fine for capture but not for anything that fills
# the queue faster than the bus drains it. 1000 is what an ethernet device gets.
ip link set "$IF" txqueuelen 1000

ip -details link show "$IF" | sed -n '1,4p'

# Verify what we asked for is what we got. A silently-kept mode is exactly the failure
# this spells everything out to prevent, and here it is the failure that matters.
actual=$(ip -details link show "$IF" | grep -oE 'can <[^>]*>' || true)
if [[ "$actual" != *"LISTEN-ONLY"* ]]; then
    echo "!! asked for LISTEN-ONLY but interface reports ${actual:-none}." >&2
    echo "   Do not capture on the bike in this state." >&2
    exit 1
fi
