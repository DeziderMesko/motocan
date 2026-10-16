#!/usr/bin/env bash
# Run a logging session at the bike: bring the bus up, capture, and tag what you did.
#
#   ./tools/2-session.sh                         # at the bike, listen-only
#   ./tools/2-session.sh --ack                   # at the bike, the adapter ACKs frames
#   ./tools/2-session.sh --iface vcan0 --bench    # dry run, no bike, no adapter
#
# One script for the whole session, because the alternative is remembering six commands
# with a helmet on. It brings the interface up in listen-only, proves the bus is alive
# before burning a capture on it, starts candump, and then shows a menu whose keys write
# the reference sidecar the analysis correlates against.
#
# After every mark it says which bytes moved since the mark before, leaving out the ones
# that never sit still. That is the only analysis it does, and it is there so that the
# person at the bike can see the bus reacted to what they just did.
#
# The menu exists because of a specific failure. [T1] in ../background/analysis-tips.md botched a
# capture -- not the analysis, the recording: "I was starting to get confused there,
# honestly. The beeping was going on." The lesson taken from it was to keep the per-step
# load low, make the steps unambiguous, and give the rider a way to mark a botched cycle
# and repeat it rather than letting it quietly poison the log. That is the x key.
#
# What the keys write, in the CSS skill's sidecar format (epoch;kind;label;value):
#
#   anchor   a known physical value at a time -- "this is 2nd gear", "this is 4000 rpm".
#            The only reference a human can produce accurately, and what calibrate.py
#            fits scale and offset to. Settle FIRST, then press: calibrate's window is
#            the quiet stretch BEHIND the tag, not ahead of it.
#   event    a discrete moment, or a bracket around a sweep. correlate.py uses events for
#            discrete signals, and a start/end pair marks the stretch worth analysing.
#   void     a row this script writes when you press x. No CSS script reads this kind, so
#            it is inert to the analysis and legible to you afterwards.
#
# There is no `value` kind here, and that is a real limit rather than an omission: it is a
# continuous reference sampled densely, and a rider cannot produce one. Holds and brackets
# are what a human can do accurately. The continuous path is OCR of the dash
# (vision_reference.py), which needs a video sharing a clock with the capture -- see
# "Filming the dash is a real reference channel" in ../background/analysis-tips.md.
set -euo pipefail
INVOKED_FROM="$PWD"
cd "$(dirname "$0")/.."

IFACE=can0
BITRATE=500000
BENCH=0
ACK=0
OUTDIR=""
CENSUS_S=30

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iface)   IFACE="$2"; shift 2 ;;
        --bitrate) BITRATE="$2"; shift 2 ;;
        --out)     OUTDIR="$2"; shift 2 ;;
        --bench)   BENCH=1; shift ;;
        --ack)     ACK=1; shift ;;
        --census)  CENSUS_S="$2"; shift 2 ;;
        -h|--help) sed -n '2,38p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

(( ACK && BENCH )) && { echo "--ack is for the bike; --bench leaves the interface alone" >&2; exit 2; }

command -v candump >/dev/null || { echo "candump not found: apt install can-utils" >&2; exit 1; }

if [[ -z "$OUTDIR" ]]; then
    OUTDIR="$INVOKED_FROM/session-$(date +%Y-%m-%d_%H%M%S)"
elif [[ "$OUTDIR" != /* ]]; then
    OUTDIR="$INVOKED_FROM/$OUTDIR"
fi
mkdir -p "$OUTDIR"

CAPTURE="$OUTDIR/capture.log"
REF="$OUTDIR/ref.csv"
NOTES="$OUTDIR/notes.txt"
WATCHDOG_LOG="$OUTDIR/ack-watchdog.log"
MOVING="$OUTDIR/census-moving.txt"
IDS_SEEN="$OUTDIR/census-ids.txt"
LAST_LINE=1
CANDUMP_PID=""
CAPTURE_STARTED=0
IGNITION_ASKED=0

echo "epoch;kind;label;value" > "$REF"
{ echo "session $(date -Iseconds)"; echo "iface $IFACE @ $BITRATE"; } > "$NOTES"

now() { date +%s.%N; }
tag() { # tag <kind> <label> <value>
    printf '%s;%s;%s;%s\n' "$(now)" "$1" "$2" "${3:-}" >> "$REF"
    printf '%s  %-7s %-18s %s\n' "$(date +%H:%M:%S)" "$1" "$2" "${3:-}" >> "$NOTES"
    moved
}

# The feedback after a mark. A byte "moves" when it takes more than one value; the census
# records which bytes do that with the bike left alone, and those are left out afterwards
# -- otherwise every mark would list the same dozen noisy ones and say nothing. What is
# printed is id, byte number from 0, and how many different values it took.
#
# A second, quieter line takes the bytes that only bounce between two values: a reading
# sitting on the edge of a count does that for minutes and missed the census by luck. A
# setting changed once is one step, not a bounce, so it stays on the first line.
positions() { # positions <from-line>  ->  "id:byte values steps" for each byte that moved
    # head -n -1: candump may be half-way through writing the last line.
    tail -n +"$1" "$CAPTURE" | head -n -1 | awk '
        NF == 3 && split($3, a, "#") == 2 {
            n = int(length(a[2]) / 2)
            for (k = 0; k < n; k++) {
                key = a[1] ":" k; b = substr(a[2], 2 * k + 1, 2)
                if (!((key, b) in seen)) { seen[key, b] = 1; cnt[key]++ }
                if ((key in last) && last[key] != b) steps[key]++
                last[key] = b
            }
        }
        END { for (key in cnt) if (cnt[key] > 1) print key, cnt[key], steps[key] }' | sort -t: -k1,1 -k2,2n
}

moved() {
    [[ -f "$MOVING" ]] || return 0      # nothing to compare against before the census
    positions "$LAST_LINE" | awk -v base="$MOVING" '
        function add(which, id, item) {
            if (lastid[which] != id) { n[which]++; grp[which, n[which]] = id; lastid[which] = id }
            grp[which, n[which]] = grp[which, n[which]] " " item
        }
        function show(which, lead, cont,    i, line) {
            line = lead
            for (i = 1; i <= n[which]; i++) {
                if (length(line) + length(grp[which, i]) > 74 && line != lead) { print line; line = cont }
                line = line grp[which, i] "   "
            }
            sub(/ +$/, "", line); print line
        }
        BEGIN { while ((getline l < base) > 0) noisy[l] = 1 }
        !($1 in noisy) {
            split($1, a, ":")
            if ($2 == 2 && $3 >= 3) add("f", a[1], "b" a[2]); else add("m", a[1], "b" a[2] "(" $2 ")")
        }
        END {
            if (n["m"]) show("m", "   moved since the last mark: ", "      ")
            else print "   nothing new moved since the last mark"
            if (n["f"]) show("f", "   (only flickering: ", "      ")
        }' | sed '/only flickering/,$ { $ s/$/)/ }'
    LAST_LINE=$(wc -l < "$CAPTURE")
}

relearn() { # after the engine is started, or anything else that changes what is noise
    local from i
    echo "   sit still for 10 s, hands off -- learning what moves on its own now"
    from=$(wc -l < "$CAPTURE")
    for (( i = 10; i > 0; i-- )); do printf '\r   %2ds ' "$i"; sleep 1; done
    printf '\r        \r'
    { cat "$MOVING"; positions "$from" | cut -d' ' -f1; } | sort -u > "$MOVING.new"
    mv "$MOVING.new" "$MOVING"
    LAST_LINE=$(wc -l < "$CAPTURE")
    echo "   $(wc -l < "$MOVING") bytes are now treated as noise"
    echo "$(date +%H:%M:%S)  re-learned noise: $(wc -l < "$MOVING") bytes" >> "$NOTES"
}

# What the bus carries now that it did not before: ids never seen in this capture, and
# bytes that move with the bike left alone and were still until now. For a change that is
# a state rather than an action -- the display put on Track -- where "did the bus react to
# what I just did" is the wrong question and "is it sending more than it was" is the right
# one. An id that turns up with a payload that never changes moves no byte, so the ids are
# compared on their own; nothing else in this script would notice one.
#
# The ids are held against the census and the checks before this one, not against the
# capture up to the moment the key is pressed. The first version did the second, and its
# dry run reported no new ids with a new one on the bus: the thing being looked for
# starts when the display is switched, which is before anybody presses anything.
newdata() {
    local from i before now new gone fresh
    tag event still_start ""
    echo "   sit still for 15 s, hands off -- looking for ids and bytes that are new"
    from=$(wc -l < "$CAPTURE")
    for (( i = 15; i > 0; i-- )); do printf '\r   %2ds ' "$i"; sleep 1; done
    printf '\r        \r'
    before=$(cat "$IDS_SEEN")
    now=$(tail -n +"$(( from + 1 ))" "$CAPTURE" | head -n -1 | awk '{print $3}' | cut -d'#' -f1 | sort -u)
    new=$(comm -13 <(echo "$before") <(echo "$now") | tr '\n' ' ')
    gone=$(comm -23 <(echo "$before") <(echo "$now") | tr '\n' ' ')
    fresh=$(positions "$(( from + 1 ))" | cut -d' ' -f1 | { grep -vxFf "$MOVING" || true; } | tr '\n' ' ')
    # Written without tag(): the feedback it prints is about what moved since the last
    # mark, and this function has just said something better about the same stretch.
    printf '%s;event;still_end;\n' "$(now)" >> "$REF"
    printf '%s  %-7s %-18s\n' "$(date +%H:%M:%S)" event still_end >> "$NOTES"
    echo "   ids: $(grep -c . <<<"$now") in these 15 s, $(grep -c . <<<"$before") known before"
    echo "   NEW ids:               ${new:-none}"
    [[ -n "$gone" ]] && echo "   ids gone quiet:        $gone"
    echo "   bytes moving that were still (id:byte): ${fresh:-none}"
    {   echo "   still check: new ids: ${new:-none}"
        echo "   still check: ids gone quiet: ${gone:-none}"
        echo "   still check: bytes newly moving on their own: ${fresh:-none}"
    } >> "$NOTES"
    # They are noise from here on, like anything else that moves by itself; they are in
    # notes.txt, which is where they are wanted.
    { cat "$MOVING"; tr ' ' '\n' <<<"$fresh" | grep . || true; } | sort -u > "$MOVING.new"
    mv "$MOVING.new" "$MOVING"
    { echo "$before"; echo "$now"; } | grep . | sort -u > "$IDS_SEEN.new"
    mv "$IDS_SEEN.new" "$IDS_SEEN"
    LAST_LINE=$(wc -l < "$CAPTURE")
}

stop_capture() {
    if [[ -n "$CANDUMP_PID" ]] && kill -0 "$CANDUMP_PID" 2>/dev/null; then
        # SIGINT to the process itself, never to a wrapping shell: candump is the thing
        # holding the file, and a signal that does not reach it leaves the log ending
        # mid-frame with nothing warning. Measured while building 1-selftest.sh:
        # `jobs -p | xargs kill` hit the subshell and never candump, and the loss was
        # 35 frames and 1.7 s with no error anywhere -- the only symptom was one anchor
        # later fitting 6 samples instead of 41.
        kill -INT "$CANDUMP_PID" 2>/dev/null || true
        wait "$CANDUMP_PID" 2>/dev/null || true
    fi
    CANDUMP_PID=""
}
trap 'stop_capture' EXIT

# Every prompt calls frames(), so it is also where a dead capture gets noticed. With
# --ack the watchdog can take the interface down mid-session, and candump exits when it
# does. The warning goes to stderr because the callers capture stdout.
frames() {
    if (( CAPTURE_STARTED )) && [[ -n "$CANDUMP_PID" ]] && ! kill -0 "$CANDUMP_PID" 2>/dev/null; then
        printf '\n  !! THE CAPTURE HAS STOPPED -- press . then q to finish' >&2
        (( ACK )) && [[ -s "$WATCHDOG_LOG" ]] && printf '\n     %s' "$(tail -1 "$WATCHDOG_LOG")" >&2
        printf '\n' >&2
    fi
    [[ -f "$CAPTURE" ]] && wc -l < "$CAPTURE" || echo 0
}
# void rows are inert to the analysis, so they do not count as marks -- otherwise the
# counter goes up when you discard something, which is the wrong feedback entirely.
marks()  { grep -cvE '^(epoch;|[0-9.]+;void;)' "$REF" || true; }

# ---------------------------------------------------------------------------
# 1. the bus
# ---------------------------------------------------------------------------
echo
if (( BENCH )); then
    echo ">> BENCH MODE on $IFACE -- the interface is left exactly as it is."
    ip link show "$IFACE" >/dev/null 2>&1 || { echo "$IFACE does not exist" >&2; exit 1; }
else
    echo ">> bringing $IFACE up LISTEN-ONLY at $BITRATE"
    echo "   (the adapter cannot ACK or emit error frames onto a bus it shares with"
    echo "    the ECU, ABS and IMU nodes -- this is the one setting that matters)"
    # sudo on this line only: run the whole session as root and every file it writes
    # comes out root-owned. The helper knows exactly one mode, which is the point.
    sudo tools/can-up-listen-only.sh "$BITRATE" "$IFACE"
fi

# --ack: the adapter acknowledges frames. It gets there in two steps and never directly.
# An adapter that ACKs also flags every frame it reads as broken, and at the wrong bit
# rate that is every frame on the bus. So the bit rate and the wiring are proved FIRST,
# listen-only, where being wrong costs nothing: a controller only hands over frames whose
# CRC checked out, so frames arriving at all is the proof. Only then does the mode change,
# and from there tools/can-up-ack.sh keeps a root watchdog on it.
#
# It used to stop here and wait for the word "ack" to be typed. That was right for the
# first time on a bus nobody had listened to; since 2026-10-11 it is the only mode that
# works on the R9's connector and has run clean three times, so --ack on the command line
# is the consent and the probe above is the gate. It still says what it is about to do.
if (( ACK )); then
    echo
    echo ">> turn the ignition ON now"
    read -rsn1 -p "   press any key when the dash is lit... " _ || true
    echo
    IGNITION_ASKED=1
    echo ">> proving the bit rate listen-only, 3 s"
    probe=$( (timeout 3 candump -L "$IFACE" 2>/dev/null || true) | awk '{print $3}' | cut -d'#' -f1 )
    nprobe=$(grep -c . <<<"$probe" || true)
    if (( nprobe < 20 )); then
        echo "   only $nprobe frames in 3 s. NOT going active: the bit rate and wiring are"
        echo "   unproven, and an adapter that ACKs at the wrong rate breaks every frame."
        exit 1
    fi
    echo "   $nprobe clean frames, ids: $(sort -u <<<"$probe" | tr '\n' ' ')"
    cat <<'T'

   Taking the adapter OUT of listen-only, as --ack asked. From here on it
   writes to the bike's bus: one ACK bit into each frame it receives
   intact, and an error flag over any frame it receives broken. It sends
   no frames. A watchdog takes it off the bus if this session ends, if
   the controller reports errors, or if anything is transmitted.

T
    : > "$WATCHDOG_LOG"
    TX_BEFORE=$(cat "/sys/class/net/$IFACE/statistics/tx_packets" 2>/dev/null || echo 0)
    sudo tools/can-up-ack.sh "$BITRATE" "$IFACE" --owner $$ --log "$WATCHDOG_LOG"
    echo "mode: ACK (normal), watchdog log in ack-watchdog.log" >> "$NOTES"
else
    echo "mode: $( (( BENCH )) && echo "bench, interface untouched" || echo listen-only )" >> "$NOTES"
fi

# ---------------------------------------------------------------------------
# 2. record first, then look -- liveness and census are different questions
# ---------------------------------------------------------------------------
# An earlier version probed for 3 s into a temp file, printed a census and deleted it.
# Two things wrong with that, and the length was the smaller one. It threw the census
# data away, so the one result that does not need any rider action could never be
# re-examined; and 3 s answers "is the bus alive" while badly under-answering "which ids
# exist" -- a 1 Hz frame gives three samples and an event-driven one gives none, so a
# missing id reads as absent when it was only rare. Both are fixed by recording first:
# liveness stays a short go/no-go, the census is counted over the real capture, and
# survey.py can redo it afterwards over any window at all.
if (( ! IGNITION_ASKED )); then
    echo
    echo ">> turn the ignition ON now"
    read -rsn1 -p "   press any key when the dash is lit... " _ || true
    echo
fi

candump -L "$IFACE" > "$CAPTURE" 2>/dev/null &
CANDUMP_PID=$!
sleep 0.3
kill -0 "$CANDUMP_PID" 2>/dev/null || { echo "candump did not start" >&2; exit 1; }
CAPTURE_STARTED=1

sleep 3
if (( $(frames) == 0 )); then
    echo "   NOTHING on $IFACE in 3 s. Stopping -- this is a wrong bit rate, a wrong"
    echo "   pinout or a dead bus, and a capture now records none of it."
    exit 1
fi
echo "   bus is alive, recording to $CAPTURE"

# The census costs nothing but sitting still, and it does two jobs. It is the list of ids
# on the bus, the one result of the day that needs no rider input. And it is the baseline
# for the feedback after each mark: whatever moves now, with nobody touching the bike, is
# noise, and is left out of "moved since the last mark" from here on.
echo
echo ">> census: sit still for ${CENSUS_S}s, hands off -- what moves now counts as noise"
for (( i = CENSUS_S; i > 0; i-- )); do printf '\r   %2ds ' "$i"; sleep 1; done
printf '\r        \r'

total=$(frames)
ids=$(awk '{print $3}' "$CAPTURE" | cut -d'#' -f1 | sort -u)
nids=$(echo "$ids" | wc -l)
echo "   $total frames, ~$(( total / (CENSUS_S + 3) )) msg/s, $nids unique ids:"
echo "$ids" | tr '\n' ' ' | fold -s -w 66 | sed 's/^/     /'
echo   # the folded list ends without a newline
{ echo "census: $nids ids, ~$(( total / (CENSUS_S + 3) )) msg/s"; echo "$ids" | tr '\n' ' '; echo; } >> "$NOTES"

positions 1 | cut -d' ' -f1 > "$MOVING"
echo "$ids" > "$IDS_SEEN"
LAST_LINE=$total
echo "   $(wc -l < "$MOVING") bytes move on their own; they are left out of what follows"

# ---------------------------------------------------------------------------
# 4. the tasks
# ---------------------------------------------------------------------------
# The session is for collecting, not for analysing: the person at the bike needs to know
# what to do and whether the bus noticed, and nothing else. So the tasks say what to do in
# plain words and carry no ids -- an earlier version printed the predicted id and the
# rival readings with each one, and on 2026-10-11 none of those ids turned out to exist
# on the R9. What the frames mean is ../r9-signals.md's business, at a desk.
sweep_open=""
task=""

tasks() {
    cat <<'TASKS'

  ================= WHAT TO RECORD ==================================
   1  gear          through the box and back, N at both ends
   2  rpm           a rev sweep, and holds read off the dash
   3  throttle      one slow roll, one fast snap
   4  wheel speed   the rear wheel turning on a stand
   5  temps         dash readings as it warms up
   6  ride mode     each of the five
   7  rider aids    TCS, SCS, LIF, LCS, quickshifter
   8  lean          rock it, and hold it at an angle
   9  switches      lever, clutch, sidestand, brakes, buttons -- one at a time
   0  track         the Track display, and Track 1-4 that come with it

   .  back here    x  BOTCHED, undo last    m  note    q  finish
   b  sit still 10 s and re-learn the noise (do it after starting
      the engine, or every mark lists the same bytes)
  ===================================================================
TASKS
}

hdr() { echo; echo "  --- $* ---"; }

# Each task prints what it settles, then reads its own keys until '.'.
task_gear() {
    hdr "GEAR"
    cat <<'T'
   Settle in each gear, THEN press. Include N at both ends.
   Press 'e' while between two gears, if you can hold it there.
     n 1 2 3 4 5 6   the gear you are in        e   between gears
T
    local k
    while true; do
        printf '\r  [%s frames, %s marks] gear> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            n|N) tag anchor gear 0; echo "   N" ;;
            [1-6]) tag anchor gear "$k"; echo "   gear $k" ;;
            e) tag event gear_between ""; echo "   between gears" ;;
            .) return ;;
            x) undo ;;
            *) echo "   n 1-6, e, or . to go back" ;;
        esac
    done
}

task_rpm() {
    hdr "RPM"
    cat <<'T'
   In NEUTRAL, engine running. A sweep to show which bytes follow the
   revs, and holds to put numbers on them.
     s   start/stop a rev sweep -- up quickly, down slowly
     r   hold steady, read the dash, THEN press and type the rpm
T
    local k v
    while true; do
        printf '\r  [%s frames, %s marks] rpm> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            s) sweep revs ;;
            r) read -r -p "   rpm on the dash: " v; [[ -n "$v" ]] && { tag anchor rpm "$v"; echo "   $v rpm"; } ;;
            # `return 0`, not `return`: a bare one hands back the status of the test
            # before it, which is 1 exactly when no sweep is open -- and under set -e
            # that ended the whole session on the way out of a task, with no conversion.
            .) [[ -n "$sweep_open" ]] && { echo "   close the sweep first"; continue; }; return 0 ;;
            x) undo ;;
            *) echo "   s, r, or . to go back" ;;
        esac
    done
}

task_throttle() {
    hdr "THROTTLE"
    cat <<'T'
   Your grip and the throttle plate are two different signals: the ECU
   moves the plate, and it does not follow the grip exactly. Do one SLOW
   roll and one FAST snap, because that is where they come apart.
   The grip also turns FORWARD past closed -- what cancels the cruise
   control. Record that on its own, never inside a roll: let go, wait
   2 s, press c, push forward and hold 2 s, let go, press c again.
     s   start/stop a roll        f   snap it open and shut, once
     c   start/stop pushing it forward past closed
T
    local k
    while true; do
        printf '\r  [%s frames, %s marks] throttle> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            s) sweep throttle ;;
            f) tag event throttle_snap ""; echo "   snap" ;;
            c) sweep throttle_negative ;;
            .) [[ -n "$sweep_open" ]] && { echo "   close the sweep first"; continue; }; return 0 ;;
            x) undo ;;
            *) echo "   s, f, c, or . to go back" ;;
        esac
    done
}

task_speed() {
    hdr "WHEEL SPEED"
    cat <<'T'
   Needs a rear stand. With only the REAR wheel turning, whatever moves
   is the rear and whatever stays at zero is the front.
     s   start/stop rear wheel turning        v   type the dash speed
T
    local k v
    while true; do
        printf '\r  [%s frames, %s marks] speed> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            s) sweep rear_wheel ;;
            v) read -r -p "   dash km/h: " v; [[ -n "$v" ]] && { tag anchor speed "$v"; echo "   $v km/h"; } ;;
            .) [[ -n "$sweep_open" ]] && { echo "   close the sweep first"; continue; }; return 0 ;;
            x) undo ;;
            *) echo "   s, v, or . to go back" ;;
        esac
    done
}

task_temps() {
    hdr "TEMPERATURES"
    cat <<'T'
   Nothing to do but let it warm up and type the dash reading now and
   then. These move slowly: a handful of readings, spread out, is plenty.
   "nothing new moved" after one of these is normal.
     w   water temp from the dash        a   air temp from the dash
T
    local k v
    while true; do
        printf '\r  [%s frames, %s marks] temps> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            w) read -r -p "   water degC: " v; [[ -n "$v" ]] && { tag anchor water_temp "$v"; echo "   water $v"; } ;;
            a) read -r -p "   air degC: " v; [[ -n "$v" ]] && { tag anchor air_temp "$v"; echo "   air $v"; } ;;
            .) return ;;
            x) undo ;;
            *) echo "   w, a, or . to go back" ;;
        esac
    done
}

task_mode() {
    hdr "RIDE MODE"
    cat <<'T'
   Five of them. Settle on each mode, then press its key. ref.csv gets
   the number, because the analysis scripts read values as numbers.
     s  sport (1)    t  street (2)    r  rain (3)
     1  custom 1 (4) 2  custom 2 (5)
T
    local k v name
    while true; do
        printf '\r  [%s frames, %s marks] mode> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            s|t|r|1|2)
                case "$k" in
                    s) v=1 name=sport ;;
                    t) v=2 name=street ;;
                    r) v=3 name=rain ;;
                    1) v=4 name="custom 1" ;;
                    2) v=5 name="custom 2" ;;
                esac
                tag anchor ride_mode "$v"; echo "   $name ($v)" ;;
            .) return ;;
            x) undo ;;
            *) echo "   s, t, r, 1, 2, or . to go back" ;;
        esac
    done
}

task_aids() {
    hdr "RIDER AIDS"
    cat <<'T'
   Change one setting at a time, then press its letter and type the
   value now showing. One at a time is what lets the bytes be told apart.
     t  TCS     s  SCS     l  LIF     c  LCS     q  quickshifter
T
    local k v
    while true; do
        printf '\r  [%s frames, %s marks] aids> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            t|s|l|c|q)
                local name
                case "$k" in t) name=tcs ;; s) name=scs ;; l) name=lif ;; c) name=lcs ;; q) name=qs ;; esac
                read -r -p "   $name set to: " v
                [[ -n "$v" ]] && { tag anchor "$name" "$v"; echo "   $name = $v"; } ;;
            .) return ;;
            x) undo ;;
            *) echo "   t s l c q, or . to go back" ;;
        esac
    done
}

task_imu() {
    hdr "LEAN"
    cat <<'T'
   The bike has to actually move. Rock it side to side, and then lean
   it and HOLD at an angle you can estimate.
     s   start/stop rocking or leaning        h   holding at an angle now
T
    local k v
    while true; do
        printf '\r  [%s frames, %s marks] imu> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            s) sweep lean ;;
            h) read -r -p "   held at how many degrees (guess is fine): " v
               [[ -n "$v" ]] && { tag anchor lean "$v"; echo "   holding $v deg"; } ;;
            .) [[ -n "$sweep_open" ]] && { echo "   close the sweep first"; continue; }; return 0 ;;
            x) undo ;;
            *) echo "   s, h, or . to go back" ;;
        esac
    done
}

# One bracket per control, so that a bit which answers to exactly one of them can be told
# from the rest. The list is ../R9_CAN_unknowns_vs_manual.md's: the inputs the service
# manual says the ECU reads and that no session had touched on purpose.
task_switches() {
    hdr "SWITCHES"
    cat <<'T'
   One control at a time. Press its key, wait 2 s, work the control and
   HOLD it 2 s, let go, wait 2 s, press the same key again.
     u  shift lever pressed UP, no shift     d  lever pressed DOWN, no shift
     c  clutch lever                         k  sidestand (down, then up)
     f  front brake                          r  rear brake
     +  cruise RES+                          -  cruise SET-
     h  high beam                            w  REAR wheel turned by hand
     e  FRONT wheel turned by hand (front stand)
     p  start button, with the start blocked (in gear, clutch out)
T
    local k name
    while true; do
        printf '\r  [%s frames, %s marks] switches> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            u|d|c|k|f|r|+|-|h|w|e|p)
                case "$k" in
                    u) name=shift_lever_up ;;   d) name=shift_lever_down ;;
                    c) name=clutch ;;           k) name=sidestand ;;
                    f) name=front_brake ;;      r) name=rear_brake ;;
                    +) name=cruise_res ;;       -) name=cruise_set ;;
                    h) name=high_beam ;;        w) name=rear_wheel_by_hand ;;
                    e) name=front_wheel_by_hand ;;
                    p) name=start_blocked ;;
                esac
                sweep "$name" ;;
            .) [[ -n "$sweep_open" ]] && { echo "   close the sweep first"; continue; }; return 0 ;;
            x) undo ;;
            *) echo "   u d c k f r + - h w e p, or . to go back" ;;
        esac
    done
}

# The dash has a Track display, and with it four ride modes the other display does not
# offer. Two questions, and they need different marks: what each Track mode sets, which is
# an anchor per mode like any other mode; and whether the bus carries anything more with
# the display on Track, which is newdata(), because switching a display is a state and not
# an action.
task_track() {
    hdr "TRACK DISPLAY, TRACK 1-4"
    cat <<'T'
   Start on the normal display, in a mode recorded before. Then:
   display to Track, press d, press i and sit still. Then each Track
   mode: select it, settle, press its number -- and for every setting
   the dash shows for that mode, press v and type it. At the end the
   display back to normal, n, and i once more.
     d   the display is on TRACK now      n   back on the normal display
     i   sit still 15 s: any new ids, any bytes newly moving?
     1 2 3 4   settled in Track 1-4 (ride mode 6-9 in ref.csv)
     v   a setting the dash shows, as name and number:  pwr 2
T
    local k v name
    while true; do
        printf '\r  [%s frames, %s marks] track> ' "$(frames)" "$(marks)"
        read -rsn1 k || return; echo
        case "$k" in
            d) tag anchor display_track 1; echo "   display on Track" ;;
            n) tag anchor display_track 0; echo "   display back to normal" ;;
            i) newdata ;;
            # 6-9, carrying on from task_mode's 1-5, so that one ride_mode column
            # holds all nine and a single search runs across them.
            [1-4]) tag anchor ride_mode "$(( k + 5 ))"; echo "   track $k ($(( k + 5 )))" ;;
            v) read -r -p "   setting and number, e.g. pwr 2: " name v
               name=${name,,}
               if [[ -n "$name" && -n "$v" ]]; then tag anchor "$name" "$v"; echo "   $name = $v"
               else echo "   needs both: a name and a number"; fi ;;
            .) return ;;
            x) undo ;;
            *) echo "   d, n, i, 1-4, v, or . to go back" ;;
        esac
    done
}

sweep() { # sweep <label> -- bracket a stretch worth analysing
    if [[ "$sweep_open" == "$1" ]]; then
        tag event "${1}_end" ""; echo "   $1 ENDED"; sweep_open=""
    elif [[ -n "$sweep_open" ]]; then
        echo "   $sweep_open is still open -- close it first"
    else
        tag event "${1}_start" ""; echo "   $1 STARTED -- press again to stop"; sweep_open="$1"
    fi
}

undo() {
    # The botched-cycle key. Drops the last mark rather than leaving it to poison the
    # correlation, and leaves a void row in its place: no CSS script reads that kind, so
    # it is inert to the analysis and still legible to you afterwards.
    if (( $(marks) == 0 )); then echo "   nothing to undo"; return; fi
    local dropped; dropped=$(tail -1 "$REF")
    head -n -1 "$REF" > "$REF.tmp" && mv "$REF.tmp" "$REF"
    printf '%s;void;discarded;%s\n' "$(now)" "${dropped//;/,}" >> "$REF"
    printf '%s  %-7s %s\n' "$(date +%H:%M:%S)" "VOID" "discarded: $dropped" >> "$NOTES"
    echo "   discarded: $dropped"
    echo "   do it again -- the log keeps the botched attempt, the analysis will not"
    # A sweep is a PAIR, so undoing half of one has to put the other half back in the
    # state that matches: dropping the _end reopens the sweep (its start still stands),
    # dropping the _start closes it. Getting this wrong leaves an unclosed bracket in the
    # sidecar, which is a stretch of capture the analysis cannot bound.
    case "$dropped" in
        *";event;"*_end";"*)   sweep_open="${dropped#*;event;}"; sweep_open="${sweep_open%_end;*}"
                               echo "   ($sweep_open is open again -- its start still stands)" ;;
        *";event;"*_start";"*) sweep_open="" ;;
    esac
    return 0
}

tasks
while true; do
    printf '\r  [%s frames, %s marks] task: ' "$(frames)" "$(marks)"
    read -rsn1 key || break
    echo
    case "$key" in
        1) task_gear ;;
        2) task_rpm ;;
        3) task_throttle ;;
        4) task_speed ;;
        5) task_temps ;;
        6) task_mode ;;
        7) task_aids ;;
        8) task_imu ;;
        9) task_switches ;;
        0) task_track ;;
        m) read -r -p "   note: " v; [[ -n "$v" ]] && { tag event note "$v"; echo "   noted"; } ;;
        x) undo ;;
        b) [[ -n "$sweep_open" ]] && { echo "   $sweep_open is still open"; continue; }; relearn ;;
        q) [[ -n "$sweep_open" ]] && { echo "   $sweep_open is still open"; continue; }; break ;;
        *) tasks ;;
    esac
    tasks
done

# ---------------------------------------------------------------------------
# 5. stop, and say what to do next
# ---------------------------------------------------------------------------
echo
echo ">> stopping the capture"
sleep 0.5
stop_capture

# With --ack the watchdog's log is part of the result: it says whether the adapter stayed
# on the bus to the end or was pulled off, and why. Read before frames() is called again,
# so its own warning does not fire for a capture this script has just stopped.
CAPTURE_STARTED=0
if (( ACK )); then
    tx=$(( $(cat "/sys/class/net/$IFACE/statistics/tx_packets" 2>/dev/null || echo "$TX_BEFORE") - TX_BEFORE ))
    echo "   frames transmitted through $IFACE this session: $tx (must be 0)"
    echo "frames transmitted: $tx" >> "$NOTES"
    if grep -q TRIPPED "$WATCHDOG_LOG" 2>/dev/null; then
        echo "   WARNING: the watchdog pulled the adapter off the bus during the session:"
        sed 's/^/     /' "$WATCHDOG_LOG"
        echo "watchdog TRIPPED, see ack-watchdog.log" >> "$NOTES"
    fi
    echo "   $IFACE goes down by itself when this script exits"
fi

f=$(frames); m=$(marks)
span=$(awk -F'[()]' 'NR==1{a=$2} {b=$2} END{printf "%.0f", b-a}' "$CAPTURE" 2>/dev/null || echo 0)
echo "   $f frames over ${span}s, $m marks"
echo "   $f frames, $m marks, ${span}s" >> "$NOTES"

# The check that would have caught a truncated log: the capture has to outlive the last
# thing you tagged, or the analysis silently runs on less than you actually did.
last_frame=$(tail -1 "$CAPTURE" | sed -n 's/^(\([0-9.]*\)).*/\1/p')
last_ref=$(tail -1 "$REF" | cut -d';' -f1)
if [[ -n "$last_frame" ]] && awk -v f="$last_frame" -v r="${last_ref:-0}" 'BEGIN{exit !(f < r - 0.5)}'; then
    echo "   WARNING: the capture ends before the last mark -- the tail is missing."
fi

if ! ./tools/3-candump-to-webcan.py "$CAPTURE" -o "$OUTDIR/trace.csv"; then
    echo "   conversion failed; the raw log is still $CAPTURE" >&2
    exit 1
fi

cat <<EOF

>> session in $OUTDIR
     capture.log   the raw candump log, the thing to keep
     trace.csv     the same, converted for the analysis
     ref.csv       what you tagged
     notes.txt     the same in a form you can read
     census-moving.txt   the bytes that were treated as noise, as id:byte
     census-ids.txt      the ids the census and the still checks have seen

>> next, at a desk. Start with the census:

   cd $(pwd) && .venv/bin/python css-skill/.claude/skills/cansub-reverse-engineering/scripts/survey.py --trace $OUTDIR/trace.csv

   Then correlate/bitsearch per signal, exactly as tools/1-selftest.sh does it.
EOF
