#!/usr/bin/env bash
# Prove the CAN analysis chain works, on a bus whose answer is known in advance.
#
#   ./tools/1-selftest.sh
#
# Needs no CANable, no Feather, no phone and no bike: it builds a virtual SocketCAN
# interface, puts a synthetic signal on it, and runs the real capture -> convert ->
# analyse path over it. The correct answer is chosen here rather than discovered, so a
# failure is the tooling's and not the bike's. Running it on the day the pigtail arrives
# is the wrong day to find out that a script does not take our logs.
#
# What it exercises, in the order a real session will:
#
#   candump -L > file       the capture, exactly as it will be taken at the bike
#   3-candump-to-webcan.py  our converter into the CSS skill's semicolon CSV
#   survey.py               which ids and bytes even move
#   correlate.py            rank candidate (id, byte, width, endianness)
#   bitsearch.py            refine to a bit field
#   build_dbc.py            fit scale and offset from the sweep
#   calibrate.py            fit them again from held anchors alone
#   verify.py               the PASS/UNCONFIRMED gate, parked AND moving
#
# Setup, once: ./tools/0-setup.sh, which builds ../.venv. The CSS scripts themselves are
# vendored at ../css-skill/ (MIT, pinned commit in css-skill/PINNED-COMMIT), so nothing
# here depends on a checkout somewhere else on the machine. --skill-dir still overrides,
# for testing against a newer upstream. See ../background/analysis-tips.md for why this chain and
# not one of our own.
#
# Their requirements.txt says python-can and python-can-cansub are "not needed for the
# offline-log workflow". They are: scripts/common.py imports python_can_cansub at module
# level, so every offline script fails on import without it. Both install from PyPI
# without any hardware attached, so this costs nothing but has to be known.

set -euo pipefail
cd "$(dirname "$0")/.."

CSS_SKILL="${CSS_SKILL:-$PWD/css-skill}"
IFACE="${IFACE:-vcan0}"
KEEP=0
HZ=25

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skill-dir) CSS_SKILL="$2"; shift 2 ;;
        --iface)     IFACE="$2"; shift 2 ;;
        --keep)      KEEP=1; shift ;;
        -h|--help)   sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

SCRIPTS="$CSS_SKILL/.claude/skills/cansub-reverse-engineering/scripts"
PY="${PY:-$PWD/.venv/bin/python}"
[[ -d "$SCRIPTS" ]] || { echo "no CSS skill at $CSS_SKILL -- see the header of $0" >&2; exit 1; }
[[ -x "$PY" ]] || { echo "no venv at $PY -- run ./tools/0-setup.sh first" >&2; exit 1; }
command -v candump >/dev/null || { echo "candump not found: apt install can-utils" >&2; exit 1; }

WORK=$(mktemp -d)
cleanup() {
    jobs -p | xargs -r kill 2>/dev/null || true
    (( KEEP )) && echo ">> working files kept in $WORK" || rm -rf "$WORK"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# The bus
# ---------------------------------------------------------------------------
if ! ip link show "$IFACE" >/dev/null 2>&1; then
    echo ">> creating $IFACE"
    sudo modprobe vcan
    sudo ip link add dev "$IFACE" type vcan
    sudo ip link set up "$IFACE"
fi

# ---------------------------------------------------------------------------
# The fixture, and why it is not sweep.sh's frame
# ---------------------------------------------------------------------------
# sweep.sh carries the same ramp four ways in one frame on purpose: it exists so a
# RaceChrono channel's byte order and width can be settled by eye in one pass. That
# makes it the wrong fixture for this test. Measured against it, the top NINE candidates
# tie at 0.9998 -- bytes 0-1 BE, 2-3 LE, byte 4 and byte 5 inverted are all exact
# monotone images of each other, and no rank-based scorer can separate a value from its
# own affine image. The right answer came out on top by tie-break order, not evidence,
# which proves nothing about the chain.
#
# So the fixture puts the ramp in ONE field and fills the rest with decoys that move
# without tracking it:
#
#   byte 0-1  16-bit big-endian   0..1000    <- the answer
#   byte 2    rolling counter     0..255        the degenerate-match adversary
#   byte 3    constant 0x5A                     must never win
#   byte 4-5  16-bit LE sawtooth  unrelated     moves, on a different period
#   byte 6-7  random                            noise
#
# The run has BOTH excitations the methodology asks for, because they test different
# halves of the chain and a sweep alone leaves the important half dark. Measured with
# the sweep only, verify reported "parked Spearman = n/a (n=0)": the signal never sits
# still, so the gate scored the moving segments and skipped the steady ones -- and the
# steady ones are where the failure it exists to catch shows up, since a wrong-but-
# deterministic encoding is exactly the thing that agrees while parked and diverges in
# motion. The holds also give calibrate.py the kind=anchor tags it reads.
#
#   sweep   600 steps, kind=value      identification, resolution, lag
#   holds   6 levels held ~3 s each    calibration, and verify's parked half
#
# The ramp is an ASYMMETRIC triangle -- 60 steps up, 140 down. A symmetric one makes the
# reference lag unidentifiable: the up and down halves shift in opposite directions, so
# the lag search finds a shallow optimum and reports noise. Measured on the symmetric
# sweep, correlate returned lag -0.4 s, and feeding that to build_dbc took R^2 from
# 0.9976 to 0.9494 on a signal that is exact. Asymmetric, the lag is real information.
FIXTURE_ID=201
STEPS=600
HOLD_FRAMES=75          # ~3 s per level at HZ, comfortably wider than the 2 s window

echo ">> putting the fixture on $IFACE (~$(( (STEPS + 6 * HOLD_FRAMES) / HZ )) s)"
# `candump -L iface > file` and not `candump -l`, which names its own file: with the
# redirect ours, the PID is ours too, and stopping the capture is one signal to one
# known process. The first version wrapped `candump -l` in a subshell to place the file,
# then killed the subshell -- which candump, its child, never saw. It kept capturing
# while the log was being read, and the read got a file that ended mid-frame, 35 frames
# and 1.7 s short. Nothing failed: the analysis simply ran on a capture whose tail was
# missing, and the only trace of it was one anchor window fitting 6 samples, not 41.
# At the bike that is the end of a run vanishing silently.
CAPTURE="$WORK/capture.log"
candump -L "$IFACE" > "$CAPTURE" 2>/dev/null &
CANDUMP_PID=$!
sleep 0.4

echo "epoch;kind;label;value" > "$WORK/ref.csv"

# unrelated background traffic, so the survey has a real candidate set to narrow
cangen "$IFACE" -g 7 -I 320 -L 8 -n $(( (STEPS + 6 * HOLD_FRAMES) * 4 )) >/dev/null 2>&1 &
cangen "$IFACE" -g 31 -I 3A0 -L 6 -n $(( STEPS / 2 )) >/dev/null 2>&1 &

DELAY=$(awk -v hz=$HZ 'BEGIN { printf "%.4f", 1.0 / hz }')
saw=0
ctr=0

emit() { # emit <true value>  -- one reference row and one frame carrying it
    local v16=$1 be sawle rnd
    ctr=$(( (ctr + 1) % 256 ))
    saw=$(( (saw + 137) % 65536 ))           # unrelated, different period

    be=$(printf '%02X%02X' $(( (v16 >> 8) & 0xFF )) $(( v16 & 0xFF )))
    sawle=$(printf '%02X%02X' $(( saw & 0xFF )) $(( (saw >> 8) & 0xFF )))
    rnd=$(printf '%02X%02X' $(( RANDOM % 256 )) $(( RANDOM % 256 )))

    # the reference row goes first: the analysis samples it with a forward
    # sample-and-hold, so a row written after its own frame biases the fit by one step
    printf '%s;value;ramp;%d\n' "$(date +%s.%N)" "$v16" >> "$WORK/ref.csv"
    cansend "$IFACE" "$FIXTURE_ID#${be}$(printf '%02X' $ctr)5A${sawle}${rnd}"
    sleep "$DELAY"
}

for (( step = 0; step < STEPS; step++ )); do
    phase=$(( step % 200 ))
    if (( phase < 60 )); then
        idx=$(( phase * 100 / 60 ))          # up over 60 steps
    else
        idx=$(( (200 - phase) * 100 / 140 )) # down over 140
    fi
    emit $(( idx * 10 ))                     # 0..1000
done

# The holds. The anchor is tagged at the END of each level, not the start, because
# calibrate's default window mode is "before": the operator settles first, then tags,
# and the window it fits is the quiet stretch behind the tag.
echo ">> holds"
for level in 0 200 400 600 800 1000; do
    for (( i = 0; i < HOLD_FRAMES; i++ )); do emit "$level"; done
    printf '%s;anchor;ramp;%d\n' "$(date +%s.%N)" "$level" >> "$WORK/ref.csv"
done

sleep 0.5
kill -INT "$CANDUMP_PID" 2>/dev/null || true
wait "$CANDUMP_PID" 2>/dev/null || true
jobs -p | xargs -r kill 2>/dev/null || true

# The capture must reach past the last reference row, or the run analysed less than it
# excited. This is the check that would have caught the truncation above.
last_frame=$(tail -1 "$CAPTURE" | sed -n 's/^(\([0-9.]*\)).*/\1/p')
last_ref=$(tail -1 "$WORK/ref.csv" | cut -d";" -f1)

# ---------------------------------------------------------------------------
# The chain
# ---------------------------------------------------------------------------
fail=0
check() { # check <label> <condition-description> <0|1>
    if (( $3 )); then
        echo "   ok    $1"
    else
        echo "   FAIL  $1 -- expected $2"
        fail=1
    fi
}

echo
echo ">> 1. convert"
./tools/3-candump-to-webcan.py "$CAPTURE" -o "$WORK/trace.csv"
rows=$(( $(wc -l < "$WORK/trace.csv") - 1 ))
check "converter produced $rows frames" "at least $STEPS" "$(( rows >= STEPS ))"
check "capture outlived the excitation" "a last frame at or after the last reference row" \
      "$(awk -v f="${last_frame:-0}" -v r="${last_ref:-1}" 'BEGIN { print (f >= r - 0.2) ? 1 : 0 }')"
check "header is the webCAN one" "the 12-column semicolon header" \
      "$(head -1 "$WORK/trace.csv" | grep -c '^TimestampEpoch;BusChannel;ID;IDE;DLC;DataLength;Dir;EDL;BRS;ESI;RTR;DataBytes$' || true)"

echo
echo ">> 2. survey"
$PY "$SCRIPTS/survey.py" --trace "$WORK/trace.csv" --no-plots --json "$WORK/survey.json" > "$WORK/survey.txt" 2>&1 \
    || { cat "$WORK/survey.txt"; exit 1; }
grep -E "^\s+$FIXTURE_ID\s" "$WORK/survey.txt" || true
check "survey found id 0x$FIXTURE_ID" "the fixture id in the table" \
      "$(grep -cE "^\s+$FIXTURE_ID\s" "$WORK/survey.txt" || true)"
check "survey flagged the rolling counter at byte 2" "ctr@ naming byte 2 on 0x$FIXTURE_ID" \
      "$(grep -E "^\s+$FIXTURE_ID\s" "$WORK/survey.txt" | grep -qc 'ctr@.*\b2\b' && echo 1 || echo 0)"

echo
echo ">> 3. correlate"
$PY "$SCRIPTS/correlate.py" --trace "$WORK/trace.csv" --sidecar "$WORK/ref.csv" \
    --type continuous --no-plots --top 8 --json "$WORK/cand.json" > "$WORK/corr.txt" 2>&1 \
    || { cat "$WORK/corr.txt"; exit 1; }
tail -14 "$WORK/corr.txt"
best=$(grep '^Best:' "$WORK/corr.txt" || true)
check "correlate's best is 0x$FIXTURE_ID byte 0 width 2 big-endian" "$best" \
      "$(echo "$best" | grep -qc "ID 0x$FIXTURE_ID byte 0 width 2 big-endian" && echo 1 || echo 0)"

# the decoys must lose, and by a margin -- this is the half the sweep.sh frame cannot test
LAG=$(echo "$best" | sed -n 's/.*lag \(-\?[0-9.]*\)s.*/\1/p'); LAG=${LAG:-0}
check "the constant byte 3 is not among the candidates" "no width-1 candidate at byte 3" \
      "$(awk '$1=="'"$FIXTURE_ID"'" && $2==3 && $3==1 {print}' "$WORK/corr.txt" | grep -qc . && echo 0 || echo 1)"

echo
echo ">> 4. bitsearch"
$PY "$SCRIPTS/bitsearch.py" --trace "$WORK/trace.csv" --sidecar "$WORK/ref.csv" \
    --id "0x$FIXTURE_ID" --lag "$LAG" --byte-align --no-plots \
    --json "$WORK/bitsearch.json" > "$WORK/bits.txt" 2>&1 \
    || { cat "$WORK/bits.txt"; exit 1; }
tail -4 "$WORK/bits.txt"

echo
echo ">> 5. build_dbc"
$PY "$SCRIPTS/build_dbc.py" --trace "$WORK/trace.csv" --sidecar "$WORK/ref.csv" \
    --id "0x$FIXTURE_ID" --byte 0 --width 2 --order big --lag "$LAG" \
    --name Ramp --no-plots --out "$WORK/Ramp.dbc" > "$WORK/dbc.txt" 2>&1 \
    || { cat "$WORK/dbc.txt"; exit 1; }
grep -E "scale|offset|R\^2" "$WORK/dbc.txt" || true
r2=$(sed -n 's/.*R\^2 = \([0-9.]*\).*/\1/p' "$WORK/dbc.txt" | head -1)
check "fit R^2 = ${r2:-none} on an exact reference" "at least 0.99" \
      "$(awk -v r="${r2:-0}" 'BEGIN { print (r >= 0.99) ? 1 : 0 }')"

echo
echo ">> 6. calibrate (from the held anchors alone, not the sweep)"
$PY "$SCRIPTS/calibrate.py" --trace "$WORK/trace.csv" --sidecar "$WORK/ref.csv" \
    --id "0x$FIXTURE_ID" --byte 0 --width 2 --order big --window 2.0 \
    --name RampHeld --out "$WORK/RampHeld.dbc" > "$WORK/cal.txt" 2>&1 \
    || { cat "$WORK/cal.txt"; exit 1; }
grep -E "scale|offset|R\^2|anchor" "$WORK/cal.txt" | head -8
cal_scale=$(sed -n 's/.*scale *= *\([0-9.eE+-]*\).*/\1/p' "$WORK/cal.txt" | head -1)
check "calibrate recovered scale ${cal_scale:-none} from anchors" "1.0" \
      "$(awk -v v="${cal_scale:-0}" 'BEGIN { print (v > 0.99 && v < 1.01) ? 1 : 0 }')"

echo
echo ">> 7. verify"
$PY "$SCRIPTS/verify.py" --trace "$WORK/trace.csv" --dbc "$WORK/Ramp.dbc" \
    --sidecar "$WORK/ref.csv" --png "$WORK/verify.png" > "$WORK/verify.txt" 2>&1 \
    || { cat "$WORK/verify.txt"; exit 1; }
grep -E "VERDICT|parked|moving" "$WORK/verify.txt" | head -8
check "verify returned PASS" "VERDICT: PASS, not UNCONFIRMED" \
      "$(grep -qE 'VERDICT: *PASS' "$WORK/verify.txt" && echo 1 || echo 0)"
# The holds exist so this half is not empty. A PASS scored on the moving segments
# alone is the weaker claim, and the earlier sweep-only fixture only ever made that one.
parked_n=$(sed -n 's/.*parked *Spearman=[^(]*(n=\([0-9]*\)).*/\1/p' "$WORK/verify.txt" | head -1)
check "verify scored ${parked_n:-0} parked samples" "more than zero" \
      "$(( ${parked_n:-0} > 0 ))"

echo
if (( fail )); then
    echo ">> SELFTEST FAILED -- the chain does not do what this test says it does."
    echo ">> Working files: $WORK (re-run with --keep to hold them)"
    KEEP=1
    exit 1
fi
echo ">> selftest passed: capture, convert and the whole CSS chain agree on a signal"
echo ">> whose answer was chosen here. The analysis half is ready for the bike."
