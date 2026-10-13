#!/usr/bin/env bash
# Bring a SocketCAN interface up. Needs root, so run it with sudo -- everything
# afterwards (candump, cansend, cangen) runs as a normal user.
#
#   sudo ./can-up.sh                       # can0, 500 kbps, normal mode
#   sudo ./can-up.sh 500000 --loopback     # self-test, no bus partner needed
#   sudo ./can-up.sh 500000 --listen-only  # never transmits, not even ACKs
#   sudo ./can-up.sh --one-shot            # bench against Bluecan: no retransmits
#   sudo ./can-up.sh --down
#
# 500 kbps is the R9's rate per analysis/r9-signals.md, so it is the default here.
#
# --listen-only is the mode to use on the bike: the adapter is then electrically
# passive and cannot ACK or emit error frames onto a bus shared with the ECU,
# ABS and IMU nodes.
#
# --one-shot is what to use on the bench against a board that never ACKs: a Bluecan
# built without CONFIG_CANBUS_ACK, or one that has tripped back to listen-only. Nothing
# on that bus ACKs, and without one-shot the controller retransmits every
# unacknowledged frame forever. The bus then floods with duplicates while cangen
# wedges and no new id gets out. Bluecan as built since patch 18 acknowledges a
# fraction of a second into the first traffic and does not need it. It is not a mode
# in the loopback/listen-only sense -- it is an independent flag, so it combines with
# any of them.
set -euo pipefail

IF="${IF:-can0}"
BITRATE=500000
MODE="loopback off listen-only off"   # default: plain normal mode
ONESHOT="one-shot off"                # default: retransmit, the kernel's own default

# Control modes are sticky: bringing an interface up without naming a mode keeps
# whatever was set last. So every mode here spells out the full set, including the
# offs -- otherwise "--normal" silently leaves a previous "--loopback" in force.
# one-shot is sticky the same way, and being independent of the other two it used to
# survive any bounce that did not name it; that is why it is always spelled out too.
for a in "$@"; do
    case "$a" in
        --loopback)    MODE="loopback on listen-only off" ;;
        --listen-only) MODE="listen-only on loopback off" ;;
        --normal)      MODE="loopback off listen-only off" ;;
        --one-shot)    ONESHOT="one-shot on" ;;
        --down)        ip link set "$IF" down && echo "$IF down"; exit 0 ;;
        [0-9]*)        BITRATE="$a" ;;
        *)             echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "needs root: sudo $0 $*" >&2
    exit 1
fi

ip link show "$IF" >/dev/null 2>&1 || {
    echo "$IF does not exist. Attach the adapter first:" >&2
    echo "  ./usb-attach.sh" >&2
    exit 1
}

ip link set "$IF" down 2>/dev/null || true

echo ">> $IF up: bitrate $BITRATE ($MODE $ONESHOT)"
if ! ip link set "$IF" up type can bitrate "$BITRATE" $MODE $ONESHOT; then
    echo "!! failed. If a mode was requested, the firmware may not advertise it;" >&2
    echo "   retry without the flag to check the interface itself works." >&2
    exit 1
fi

# SocketCAN defaults a CAN interface to txqueuelen 10, which is fine for hand-typed
# cansend but not for anything that fills the queue faster than the bus drains it:
# `cangen -g 0` returns "write: No buffer space available" after ~20 frames and looks
# like the adapter stalling. It is not -- it is ten slots. 1000 is what the kernel
# gives an ethernet device and is plenty to saturate 500 kbps.
ip link set "$IF" txqueuelen 1000

ip -details link show "$IF" | sed -n '1,4p'

# Verify what we asked for is what we got, since a silently-kept mode is exactly
# the failure this script was written to stop repeating. The control modes are the
# angle-bracket group on the "can <...> state ..." line -- absent entirely when none
# is set, which is what plain normal mode looks like.
actual=$(ip -details link show "$IF" | grep -oE 'can <[^>]*>' || true)
want=""
case "$MODE" in
    *"loopback on"*)    want="LOOPBACK" ;;
    *"listen-only on"*) want="LISTEN-ONLY" ;;
esac
if [[ "$ONESHOT" == *"one-shot on"* ]]; then
    want="${want:+$want }ONE-SHOT"
fi
for flag in LOOPBACK LISTEN-ONLY ONE-SHOT; do
    if [[ " $want " == *" $flag "* && "$actual" != *"$flag"* ]]; then
        echo "!! asked for $flag but interface reports ${actual:-none}" >&2
    elif [[ " $want " != *" $flag "* && "$actual" == *"$flag"* ]]; then
        echo "!! did not ask for $flag but interface reports it" >&2
    fi
done
