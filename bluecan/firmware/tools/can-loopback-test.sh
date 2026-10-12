#!/usr/bin/env bash
# Definitive test for the gs_usb 3-frame stall: does a *single* socket, on a
# freshly opened channel, with a reader draining the interface, get further than
# three frames?
#
# The earlier version of this script used one cansend process per frame, which
# meant one socket open/close per frame -- a plausible confound. cangen uses one
# socket for the whole run, and candump keeps the receive side drained, so the
# only thing left under test is the driver/USB path.
#
# Both must start from a fresh channel: once the TX contexts are exhausted the
# interface stays wedged and every later write returns ENOBUFS, so any test run
# against a dirty interface measures nothing.
#
#   sudo ./can-loopback-test.sh [count] [gap_ms] [--normal]
set -euo pipefail

IF="${IF:-can0}"
COUNT="${1:-20}"
GAP="${2:-50}"
MODE="loopback on listen-only off"
[[ "${3:-}" == "--normal" ]] && MODE="loopback off listen-only off"

[[ $EUID -eq 0 ]] || { echo "needs root: sudo $0 $*" >&2; exit 1; }

txp() { ip -statistics link show "$IF" | awk '/TX:/{getline; print $2}'; }
rxp() { ip -statistics link show "$IF" | awk '/RX:/{getline; print $2}'; }

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

echo ">> fresh channel: $IF, 500000, $MODE"
ip link set "$IF" down 2>/dev/null || true
ip link set "$IF" up type can bitrate 500000 $MODE
sleep 0.5

tx0=$(txp); rx0=$(rxp)
echo "   baseline TX=$tx0 RX=$rx0"

candump -t d "$IF" > "$OUT" 2>&1 &
CD=$!
sleep 0.5

echo ">> cangen: $COUNT frames, one socket, ${GAP}ms apart"
set +e
cangen "$IF" -I 236 -L 8 -D i -g "$GAP" -n "$COUNT" 2>&1
rc=$?
set -e
echo "   cangen rc=$rc"

sleep 1
kill $CD 2>/dev/null || true
wait $CD 2>/dev/null || true

tx1=$(txp); rx1=$(rxp)
echo
echo ">> TX completed: $((tx1 - tx0)) of $COUNT"
echo ">> RX received:  $((rx1 - rx0))"
echo ">> candump saw:  $(wc -l < "$OUT") frames"
ip -details -statistics link show "$IF" | grep -A1 "re-started" | tail -1
