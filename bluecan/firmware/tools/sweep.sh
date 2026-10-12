#!/usr/bin/env bash
# Put a synthetic signal on can0 whose true value is known, so a RaceChrono channel
# can be validated before any of the R9's real signals are.
#
#   ./tools/sweep.sh              # 0x200, triangle wave, 20 Hz, forever
#   ./tools/sweep.sh --id 300     # a different arbitration id
#   ./tools/sweep.sh --hz 5       # slower, easier to read off a phone
#   ./tools/sweep.sh --once       # one pass up and down, then stop
#   ./tools/sweep.sh --sidecar ref.csv   # also write the true value as a reference
#
# Why this exists: entering a channel in RaceChrono means committing to a CAN id, a
# bit offset, a length, a byte order and an equation, and a wrong reading afterwards
# could be any of the five -- or the CAN mapping the equation came from. Sweeping a
# value the bench chose removes four of those unknowns at once. Get the channel
# reading 0..1000 against this, and a wrong reading on the bike is the mapping's
# fault, not the workflow's.
#
# The frame carries the same ramp four ways, so one capture settles byte order and
# width without re-sending anything:
#
#   byte 0-1  16-bit, big-endian     0..1000     (RaceChrono: "MSB first")
#   byte 2-3  16-bit, little-endian  0..1000     (RaceChrono: "LSB first")
#   byte 4    8-bit                  0..250
#   byte 5    8-bit, inverted        250..0      (a channel that should fall while
#                                                 the others rise)
#   byte 6-7  0xA5 0x5A              constant    (a value that must never move)
#
# --sidecar writes the reference series the analysis chain correlates against, in the
# CSS skill's own sidecar format (epoch;kind;label;value), one kind=value row per frame
# carrying the ramp's true 0..1000. On the bike that column is a human dragging a slider
# or OCR of the dash, and it carries reaction lag; here it is exact, because this script
# knows the number before it sends it. That is what makes the run a test of the tooling
# and not of the operator: the correct answer is byte 0-1 big-endian, and anything else
# winning is the chain's fault. See ../../../analysis/background/analysis-tips.md.
#
# The Feather is listen-only, so nothing on this bench ACKs the CANable and it climbs to
# ERROR-PASSIVE within a couple of dozen frames. That is listen-only working, not a
# fault -- see ../bench-link.md -- and, measured rather than assumed, it is also not a
# reason to do anything: an error-passive controller still transmits, and 60 of 60
# frames at 10 Hz reach the Feather with the interface sitting in that state throughout.
# Only BUS-OFF actually stops it, and this adapter cannot restart itself from bus-off
# ("Device doesn't support restart from Bus Off"), so the interface has to be bounced by
# hand. That is what this watches for, and it is the whole reason this is a script
# rather than a cangen one-liner. On the bike it cannot arise, since the ECU, ABS and
# IMU nodes ACK each other.
set -euo pipefail
# Before the cd, so a --sidecar path given on the command line still means what the
# caller meant by it rather than something under firmware/.
INVOKED_FROM="$PWD"
cd "$(dirname "$0")/.."

ID=200
HZ=20
ONCE=0
IFACE=can0
SIDECAR=

while [[ $# -gt 0 ]]; do
    case "$1" in
        --id)    ID="$2"; shift 2 ;;
        --hz)    HZ="$2"; shift 2 ;;
        --iface) IFACE="$2"; shift 2 ;;
        --once)  ONCE=1; shift ;;
        --sidecar) SIDECAR="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

command -v cansend >/dev/null || { echo "cansend not found: apt install can-utils" >&2; exit 1; }
ip link show "$IFACE" >/dev/null 2>&1 || { echo "$IFACE is not up; run sudo ./tools/can-up.sh" >&2; exit 1; }

DELAY=$(awk -v hz="$HZ" 'BEGIN { printf "%.4f", 1.0 / hz }')

if [[ -n "$SIDECAR" ]]; then
    [[ "$SIDECAR" != /* ]] && SIDECAR="$INVOKED_FROM/$SIDECAR"
    echo "epoch;kind;label;value" > "$SIDECAR"
fi

bounces=0
bounce() {
    if (( bounces == 0 )); then
        echo ">> $IFACE stopped transmitting: nothing on this bench ACKs us, because"
        echo ">> the Feather is listen-only, and this adapter cannot restart itself"
        echo ">> from bus-off. Bouncing the interface to keep the sweep going."
    elif (( bounces % 10 == 0 )); then
        echo ">> $bounces bounces"
    fi
    bounces=$(( bounces + 1 ))
    sudo ip link set "$IFACE" down
    sudo ip link set "$IFACE" up type can bitrate 500000
    # the link is briefly down either way; without this the next frames write into it
    sleep 0.1
}

# Deliberately not "is it ERROR-ACTIVE". Bouncing on error-passive throws away a link
# that is still delivering every frame, and costs about half a second each time: an
# earlier version of this script did exactly that and turned a 10 Hz sweep into 2 Hz.
# Only these two states actually stop traffic.
healthy() {
    ! ip -details link show "$IFACE" 2>/dev/null | grep -qE 'state (BUS-OFF|STOPPED)'
}

echo ">> sweeping id 0x$ID on $IFACE at $HZ Hz, ^C to stop"
[[ -n "$SIDECAR" ]] && echo ">> reference series -> $SIDECAR"
echo ">> byte 0-1 BE 0..1000 | byte 2-3 LE 0..1000 | byte 4 0..250 | byte 5 250..0 | byte 6-7 A5 5A"

trap 'echo; echo ">> stopped"; exit 0' INT

step=0
n=0
while true; do
    # triangle rather than sawtooth: a wrap from 1000 to 0 looks like a glitch on a
    # phone screen, and a value that comes back down proves the channel tracks rather
    # than merely moves
    phase=$(( step % 200 ))
    idx=$(( phase < 100 ? phase : 200 - phase ))

    v16=$(( idx * 10 ))              # 0..1000
    v8=$(( idx * 5 / 2 ))            # 0..250
    inv=$(( 250 - v8 ))              # 250..0

    be=$(printf '%02X%02X' $(( (v16 >> 8) & 0xFF )) $(( v16 & 0xFF )))
    le=$(printf '%02X%02X' $(( v16 & 0xFF )) $(( (v16 >> 8) & 0xFF )))

    frame="$ID#${be}${le}$(printf '%02X%02X' "$v8" "$inv")A55A"

    # Before the send, not after, and it was measured the wrong way round first. The
    # analysis samples the reference with a forward sample-and-hold -- the most recent
    # row at or before each frame's timestamp -- so a row written after its own frame
    # pairs every frame with the PREVIOUS step. That is a systematic offset of exactly
    # one step: -10 counts here, and a fit that should be scale 1 offset 0 came back
    # offset -10. Written first, the same fit is scale 1, offset -10 -> 0.
    if [[ -n "$SIDECAR" ]]; then
        printf '%s;value;sweep;%d\n' "$(date +%s.%N)" "$v16" >> "$SIDECAR"
    fi

    # A write that fails is the interface already down or bus-off, which the periodic
    # check below would not notice for another two dozen frames.
    if ! cansend "$IFACE" "$frame" 2>/dev/null; then
        bounce
        cansend "$IFACE" "$frame" 2>/dev/null || true
    fi

    step=$(( step + 1 ))
    n=$(( n + 1 ))

    # cheap enough at these rates, and the alternative is a sweep that silently stops
    if (( n % 25 == 0 )) && ! healthy; then
        bounce
    fi

    if (( ONCE == 1 && step >= 200 )); then
        echo ">> one pass done"
        exit 0
    fi

    sleep "$DELAY"
done
