#!/usr/bin/env python3
"""Convert a candump log into the webCAN CSV the CSS Electronics analysis chain reads.

    candump -l can0                       # writes candump-2026-08-23_120000.log
    ./tools/candump-to-webcan.py candump-2026-08-23_120000.log -o trace.csv

Why this exists: the CSS skill's analysis half -- survey, correlate, bitsearch,
calibrate, build_dbc, verify -- is not tied to their hardware. Only capture.py and
detect_bus.py touch the CANsub. Everything downstream reads one semicolon CSV, so a
capture taken with the CANable and can-utils runs through their scripts once it wears
that header. See ../background/analysis-tips.md.

The header, from their common.py load_trace():

    TimestampEpoch;BusChannel;ID;IDE;DLC;DataLength;Dir;EDL;BRS;ESI;RTR;DataBytes

Three of those columns are not free choices, and getting them wrong is silent:

  Dir       load_trace() drops every row whose Dir is not 0 unless asked otherwise, so
            a wrong value here yields an empty analysis rather than an error. The
            candump log format records no direction at all, so every frame is written
            as RX. On the bike that is simply true. On the bench it is the useful lie:
            SocketCAN shows locally-sent frames to other sockets on the same host, so
            sweep.sh's own output lands in the capture, and marking it TX would delete
            the one signal the run was for.
  ID        hex, no 0x, parsed with int(s, 16). candump's own field width is what says
            whether a frame is extended -- 3 hex digits for an 11-bit id, 8 for a
            29-bit one -- not the numeric value, since 0x123 is a legal id in both.
  DLC       for classic CAN this equals the byte count. For CAN FD it is the 0..15
            length code, which stops matching the byte count above 8 bytes.

Error frames are dropped, and counted in the summary. They carry CAN_ERR_FLAG
(0x20000000) in the id field, so an 8-digit id like 20000004 is a bus error and not a
frame from a node; left in, they arrive as a phantom extended id whose payload is the
controller's error state, and survey.py reports its bits as an active signal.
"""

import argparse
import re
import sys

WEBCAN_COLUMNS = [
    "TimestampEpoch", "BusChannel", "ID", "IDE", "DLC", "DataLength",
    "Dir", "EDL", "BRS", "ESI", "RTR", "DataBytes",
]

CAN_ERR_FLAG = 0x20000000

# (1787515591.296367) vcan0 200#03E803E8FAA5A55A
LINE_RE = re.compile(
    r"^\((?P<ts>\d+\.\d+)\)\s+(?P<iface>\S+)\s+(?P<id>[0-9A-Fa-f]+)"
    r"(?P<sep>##?)(?P<rest>\S*)\s*$"
)

# CAN FD's length code above 8 bytes: DLC 9..15 mean these payload sizes.
FD_DLC_LENGTHS = [12, 16, 20, 24, 32, 48, 64]


def dlc_for(nbytes, is_fd):
    """The 0..15 length code for a payload of nbytes."""
    if not is_fd or nbytes <= 8:
        return nbytes
    for i, size in enumerate(FD_DLC_LENGTHS):
        if nbytes <= size:
            return 9 + i
    return 15


def convert(lines, channel, keep_errors):
    """Yield webCAN rows. Returns counts via the stats dict passed back by the caller."""
    stats = {"frames": 0, "errors": 0, "rtr": 0, "fd": 0, "unparsed": 0}
    rows = []

    for lineno, line in enumerate(lines, 1):
        line = line.rstrip("\n")
        if not line.strip():
            continue
        m = LINE_RE.match(line)
        if not m:
            stats["unparsed"] += 1
            if stats["unparsed"] <= 3:
                print(f"warning: line {lineno} not in candump -l format: {line[:70]}",
                      file=sys.stderr)
            continue

        id_field = m.group("id")
        can_id = int(id_field, 16)

        # An error frame is flagged in the id, and candump always prints it 8 wide.
        if len(id_field) == 8 and (can_id & CAN_ERR_FLAG):
            stats["errors"] += 1
            if not keep_errors:
                continue

        # candump prints 3 hex digits for an 11-bit id and 8 for a 29-bit one; the
        # width is the only thing that distinguishes them.
        ext = 1 if len(id_field) == 8 else 0
        is_fd = m.group("sep") == "##"
        rest = m.group("rest")

        rtr = 0
        brs = 0
        esi = 0
        data = ""

        if is_fd:
            stats["fd"] += 1
            if not rest:
                stats["unparsed"] += 1
                continue
            flags = int(rest[0], 16)
            brs = 1 if flags & 0x01 else 0
            esi = 1 if flags & 0x02 else 0
            data = rest[1:]
        elif rest.startswith(("R", "r")):
            # 300#R8 -- remote frame, the digits after R are the requested DLC and
            # there is no payload.
            stats["rtr"] += 1
            rtr = 1
            data = ""
            requested = rest[1:]
            nbytes = int(requested) if requested.isdigit() else 0
            rows.append((m.group("ts"), channel, id_field.upper(), ext, nbytes, 0,
                         0, 0, 0, 0, 1, ""))
            stats["frames"] += 1
            continue
        else:
            data = rest

        if len(data) % 2:
            stats["unparsed"] += 1
            print(f"warning: line {lineno} has an odd number of hex digits", file=sys.stderr)
            continue

        nbytes = len(data) // 2
        rows.append((m.group("ts"), channel, id_field.upper(), ext,
                     dlc_for(nbytes, is_fd), nbytes,
                     0, 1 if is_fd else 0, brs, esi, rtr, data.upper()))
        stats["frames"] += 1

    return rows, stats


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", help="a candump -l log, or - for stdin")
    ap.add_argument("-o", "--output", required=True, help="webCAN CSV to write")
    ap.add_argument("--channel", type=int, default=1,
                    help="BusChannel to stamp on every row (default 1)")
    ap.add_argument("--keep-errors", action="store_true",
                    help="keep CAN error frames instead of dropping them; they will "
                         "appear to the analysis as an extended id that does not exist")
    args = ap.parse_args()

    if args.logfile == "-":
        lines = sys.stdin.readlines()
    else:
        with open(args.logfile) as fh:
            lines = fh.readlines()

    rows, stats = convert(lines, args.channel, args.keep_errors)

    if not rows:
        print("no frames converted -- is this a 'candump -l' log? A log taken with "
              "relative timestamps (-t d, -t z) has no epoch to convert.", file=sys.stderr)
        return 1

    with open(args.output, "w") as fh:
        fh.write(";".join(WEBCAN_COLUMNS) + "\n")
        for r in rows:
            fh.write(";".join(str(x) for x in r) + "\n")

    span = float(rows[-1][0]) - float(rows[0][0])
    ids = len({r[2] for r in rows})
    print(f"{stats['frames']} frames, {ids} ids, {span:.1f} s "
          f"({stats['frames'] / span:.1f} msg/s)" if span > 0 else
          f"{stats['frames']} frames, {ids} ids")
    for key, label in (("errors", "error frames dropped"), ("rtr", "remote frames"),
                       ("fd", "CAN FD frames"), ("unparsed", "lines skipped")):
        if stats[key]:
            verb = "kept" if key == "errors" and args.keep_errors else label
            print(f"  {stats[key]} {verb}")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
