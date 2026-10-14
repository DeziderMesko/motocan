#!/usr/bin/env bash
# Long-running load test: does the bench link survive an hour rather than a burst?
#
#   ./tools/soak.sh [minutes] [outdir]        # default 30 minutes
#
# The short tests in ../history/bench-link.md prove the link works. They cannot distinguish
# "works" from "works until it doesn't", and the failure modes that only appear over
# time are exactly the ones that would ruin a real logging session: a heap leak, a
# watchdog reset, a queue that slowly falls behind, or the gs_usb transmit path
# wedging again the way it used to (see ../history/gs-usb-stall.md).
#
# The traffic profile deliberately imitates the R9 rather than maximising load. The
# ids and rates come from ../../../analysis/background/pre-bike-mapping.md and [S4] in it: engine speed, gear,
# TPS/APS, brake pressure and wheel speeds are broadcast at 100 Hz, temperatures at
# 1 Hz, and ../history/firmware.md quotes ~16 unique ids at ~1050 msg/s for the whole bus.
# A saturated bus was already shown to work in a burst; what is untested is a
# realistic bus for a long time, which is the case the device will actually meet.
set -euo pipefail
cd "$(dirname "$0")/.."

MINUTES="${1:-30}"
OUT="${2:-/tmp/bluecan-soak}"
IF="${IF:-can0}"
mkdir -p "$OUT"

SECS=$(( MINUTES * 60 ))
LOG="$OUT/feather.log"
SUM="$OUT/summary.txt"

ip link show "$IF" >/dev/null 2>&1 || { echo "$IF missing; run ./tools/usb-attach.sh and can-up.sh" >&2; exit 1; }
[[ -e /dev/ttyACM0 ]] || { echo "/dev/ttyACM0 missing; run ./tools/usb-attach.sh" >&2; exit 1; }

# id:gap_ms -- gap 10 = 100 Hz. Sixteen ids, ~900 frames/s in total.
PROFILE=(
    20A:10   # engine speed          [S4] 100 Hz
    215:10   # TPS + APS             [S4] 100 Hz
    236:10   # gear                  [S4] 100 Hz
    260:10   # brake pressure        [S4] 100 Hz
    264:10   # front/rear wheel speed [S4] 100 Hz
    216:10   # TPS, Tenere/MT-07 family
    268:20   # ABS state machine
    22E:20   # present, undecoded
    23A:20   # present, undecoded
    270:20
    280:50
    290:50
    2B0:50
    2C0:50
    2D0:50
    23E:1000 # water + air temp      [S4] 1 Hz
)

counter() { ip -statistics link show "$IF" | awk -v k="$1" '$1 == k":"{getline; print $2}'; }

echo ">> soak: $MINUTES min, $IF, ${#PROFILE[@]} ids, output in $OUT"
echo ">> profile: $(printf '%s ' "${PROFILE[@]}")"

# Put the board in a known state: counters zeroed, per-frame logging off, display on.
# Explicitly all three, because inheriting any of them makes runs incomparable. Logging
# off matters most: writing a line per frame over USB CDC blocks loop() for up to 25 ms
# when the host is slow to drain, which is a third of the RX queue's depth in time.
# Display on because that is how the device will actually run, and because it is the
# thing that broke the first time (../history/bench-link.md).
# Five minutes of margin, and killed explicitly below rather than left to time out.
# The logger outliving the load is not a nicety: if it stops first, the last frame
# count in the log is older than the counter it gets compared against, and the report
# reads that gap as lost frames. A 60-minute run once "lost" 164268 frames that way,
# every one of which had in fact arrived.
./tools/bench.py "$((SECS + 300))" -c 'zVd' --after 2 > "$LOG" 2>&1 &
BENCH=$!
sleep 6   # let the commands land and a couple of baseline lines accumulate

tx0=$(counter TX); rx0=$(counter RX)
start=$(date +%s)
echo ">> baseline $IF TX=$tx0 RX=$rx0 at $(date -Is)"

PIDS=()
for entry in "${PROFILE[@]}"; do
    id="${entry%%:*}"; gap="${entry##*:}"
    cangen "$IF" -I "$id" -L 8 -D i -g "$gap" >/dev/null 2>&1 &
    PIDS+=($!)
done
echo ">> ${#PIDS[@]} generators running"

cleanup() { kill "${PIDS[@]}" 2>/dev/null || true; }
trap cleanup EXIT

# Progress every minute, scheduled against an absolute deadline rather than by adding
# up sleeps. Each tick costs a few `ip -statistics` calls over usbip, and at sixty
# ticks that drift compounded into 3.4 minutes of overrun on the first hour-long run.
for (( m = 1; m <= MINUTES; m++ )); do
    target=$(( start + m * 60 ))
    while (( $(date +%s) < target )); do sleep 1; done
    alive=0
    for p in "${PIDS[@]}"; do kill -0 "$p" 2>/dev/null && alive=$((alive+1)); done
    printf '   %3d/%d min | %s TX=%s RX=%s | generators %d/%d | %s\n' \
        "$m" "$MINUTES" "$IF" "$(counter TX)" "$(counter RX)" "$alive" "${#PIDS[@]}" \
        "$(tail -1 "$LOG" | cut -c1-118)"
done

# Order matters here. Stop the generators, let the bus go quiet so both sides' counters
# settle with nothing in flight, read them, give the Feather a couple more status lines
# so the log definitely extends past the last frame, and only then stop the logger.
cleanup
sleep 3
tx1=$(counter TX); rx1=$(counter RX)
elapsed=$(( $(date +%s) - start ))
sleep 3
kill $BENCH 2>/dev/null || true
wait $BENCH 2>/dev/null || true

python3 tools/soak-report.py "$LOG" "$((tx1-tx0))" "$((rx1-rx0))" "$elapsed" \
    "$(ip -details -statistics link show "$IF" | sed -n '3p' | tr -s ' ')" | tee "$SUM"
