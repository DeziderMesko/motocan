#!/usr/bin/env bash
# Bring a SocketCAN interface up so that it ACKNOWLEDGES frames, and guard it while it
# does. Needs root; 2-session.sh --ack calls it with sudo, never by hand.
#
#   sudo ./tools/can-up-ack.sh <bitrate> <iface> --owner <pid> --log <file>
#
# Why this exists at all, when can-up-listen-only.sh says the adapter must never ACK: the
# first capture off the R9 (2026-10-11, see ../captures/session-2026-10-11_115626/README.md) was one node resending
# one frame 3 800 times a second because nothing on the pair acknowledged it. A listening
# adapter cannot find out what that node says to something that answers. AiM's logger
# plugs into the same connector and ACKs as any node does, so this is the port being used
# as intended -- but it is the adapter writing to the bike's bus, so it is a separate
# script, asked for by name, and it does not trust itself.
#
# What "ACK" puts on the wire: one dominant bit in the ACK slot of each frame received
# intact, and an error flag over a frame received broken. Nothing else -- no frame is
# ever sent, and the watchdog below takes the interface down if one is.
#
# The watchdog is the point of the script. It runs as root, so it needs no second sudo to
# act, and it takes the interface DOWN -- the adapter off the bus -- the moment any of
# these is true:
#
#   the session that asked for it has exited, for any reason
#   the controller has left ERROR-ACTIVE (it has been flagging errors, or was told to)
#   the receive error counter has climbed, where the firmware reports one
#   anything at all has been transmitted through the interface
#
# It refuses to start without an owner, so there is no way to leave the adapter ACKing
# with nothing watching it.
set -euo pipefail

BITRATE=""
IF=""
OWNER=""
LOG=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --owner) OWNER="$2"; shift 2 ;;
        --log)   LOG="$2"; shift 2 ;;
        [0-9]*)  BITRATE="$1"; shift ;;
        *)       IF="$1"; shift ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "needs root, and is run by 2-session.sh --ack rather than by hand" >&2
    exit 1
fi
if [[ -z "$BITRATE" || -z "$IF" || -z "$OWNER" || -z "$LOG" ]]; then
    echo "usage: $0 <bitrate> <iface> --owner <pid> --log <file>" >&2
    exit 2
fi
kill -0 "$OWNER" 2>/dev/null || { echo "owner $OWNER is not running" >&2; exit 1; }
# The session creates the log, so it stays the user's file; root only appends to it.
[[ -f "$LOG" ]] || { echo "$LOG does not exist -- the session creates it" >&2; exit 1; }
ip link show "$IF" >/dev/null 2>&1 || { echo "$IF does not exist" >&2; exit 1; }

# REC at which the watchdog gives up. A receive error adds 1 (8 for some) and every good
# frame takes 1 back, so on a healthy bus this sits at 0; 32 is several bad frames in a
# row, and well short of the 96 where the controller itself raises a warning.
REC_LIMIT=32

ip link set "$IF" down 2>/dev/null || true

# Every mode is spelled out because control modes are sticky. restart-ms 0 means a
# controller that goes bus-off stays off rather than rejoining on its own.
echo ">> $IF up: bitrate $BITRATE (listen-only OFF -- the adapter will ACK)"
if ! ip link set "$IF" up type can bitrate "$BITRATE" \
        listen-only off loopback off restart-ms 0 berr-reporting on 2>/dev/null; then
    # berr-reporting is the only part of that a firmware may not offer.
    ip link set "$IF" down 2>/dev/null || true
    if ! ip link set "$IF" up type can bitrate "$BITRATE" \
            listen-only off loopback off restart-ms 0; then
        echo "!! failed to bring $IF up" >&2
        exit 1
    fi
fi

can_line() { ip -details link show "$IF" 2>/dev/null | grep -E '^\s+can ' || true; }
state_of() { can_line | sed -n 's/.* state \([A-Z-]*\).*/\1/p'; }
rec_of()   { can_line | sed -n 's/.*berr-counter tx [0-9]* rx \([0-9]*\).*/\1/p'; }

ip -details link show "$IF" | sed -n '1,4p'

flags=$(can_line | grep -oE '<[^>]*>' || true)
if [[ "$flags" == *"LISTEN-ONLY"* || "$flags" == *"LOOPBACK"* ]]; then
    ip link set "$IF" down
    echo "!! asked for plain normal mode but interface reports $flags. $IF is down." >&2
    exit 1
fi
if [[ "$(state_of)" != "ERROR-ACTIVE" ]]; then
    st=$(state_of)
    ip link set "$IF" down
    echo "!! $IF came up in state ${st:-unknown}, not ERROR-ACTIVE. $IF is down." >&2
    exit 1
fi
[[ -n "$(rec_of)" ]] || echo "   (this firmware reports no error counters; the watchdog goes by state alone)"

# setsid, and every stream closed or pointed at the log: the watchdog has to outlive
# sudo and must not hold the session's terminal.
setsid bash -c '
    IF=$1 OWNER=$2 LOG=$3 REC_LIMIT=$4
    can_line() { ip -details link show "$IF" 2>/dev/null | grep -E "^\s+can " || true; }
    say() { printf "%s  %s\n" "$(date +%H:%M:%S)" "$*" >> "$LOG"; }
    # The counter is the lifetime total of the interface, so an earlier bench session in
    # loopback leaves it non-zero. Only a rise from here on belongs to this session.
    # (No apostrophes in here: this whole block is one single-quoted string.)
    tx0=$(cat "/sys/class/net/$IF/statistics/tx_packets" 2>/dev/null || echo 0)
    say "watching $IF for session $OWNER"
    why=""
    while :; do
        (( $(cat "/sys/class/net/$IF/flags" 2>/dev/null || echo 0) & 1 )) || { say "$IF is no longer up; nothing to do"; exit 0; }
        line=$(can_line)
        state=$(sed -n "s/.* state \([A-Z-]*\).*/\1/p" <<<"$line")
        rec=$(sed -n "s/.*berr-counter tx [0-9]* rx \([0-9]*\).*/\1/p" <<<"$line")
        tx=$(cat "/sys/class/net/$IF/statistics/tx_packets" 2>/dev/null || echo 0)
        if   ! kill -0 "$OWNER" 2>/dev/null;        then why="the session has ended"
        elif [[ "$state" != "ERROR-ACTIVE" ]];      then why="TRIPPED: controller state is ${state:-unknown}"
        elif [[ -n "$rec" ]] && (( rec >= REC_LIMIT )); then why="TRIPPED: receive error counter at $rec"
        elif (( tx > tx0 ));                        then why="TRIPPED: $(( tx - tx0 )) frame(s) were transmitted through $IF"
        fi
        [[ -n "$why" ]] && break
        sleep 0.2
    done
    ip link set "$IF" down
    say "$why -- $IF taken down, the adapter is off the bus"
' watchdog "$IF" "$OWNER" "$LOG" "$REC_LIMIT" </dev/null >/dev/null 2>&1 &

echo "   watchdog running; it takes $IF down when this session ends or anything looks wrong"
