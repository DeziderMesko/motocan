#!/usr/bin/env python3
"""Turn a Bluecan microSD log, bluecan-NNNN.bin, into a candump log.

    ./tools/sdlog-to-candump.py bluecan-0007.bin > bluecan-0007.log
    ./tools/sdlog-to-candump.py bluecan-0007.bin --start 1792440000.0 > bluecan-0007.log
    ./tools/sdlog-to-candump.py bluecan-0007_2026-10-21_17-42-05+0200.bin > bluecan-0007.log

The card holds one 24-byte record per frame, little-endian (src/sdlog/sdlog.hpp):
the chip's microseconds since it started, u64; the id, u32, bit 31 set for a 29-bit
one; the length, u8; flags, u8, bit 0 a remote frame; two bytes of padding, zero; and
eight bytes of payload. The output is candump's log format, as the analysis tools read
it: `(seconds) can0 ID#PAYLOAD`. The seconds are the chip's own, counted from its
start, unless --start gives the wall-clock time of that start.

A file downloaded once the board's clock was set carries in its name the local time
it was last written, with its offset from UTC. That is about when its last frame
arrived, and without --start the start is taken from it: that time less the last
frame's. It is good to about two seconds: FAT keeps times to two, and the board brings
the time up to date every 5 s and as it closes the file.

A file opened again after a failed write begins at a sector boundary, which is
usually inside a record; the first whole record is found by trying the three offsets
a 4096-byte sector can leave against 24-byte records, 0, 8 and 16, and keeping the one
whose records look like records.
"""
import argparse
import datetime
import os
import re
import struct
import sys

RECORD = struct.Struct('<QIBBH8s')

# bluecan-NNNN_2026-10-21_17-42-05+0200.bin
STAMP = re.compile(r'_(\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d[+-]\d{4})\.bin$')


def written(path):
    """The time the board last wrote the file, epoch seconds, from its name; None if the
    name has no stamp."""
    m = STAMP.search(os.path.basename(path))
    if not m:
        return None
    return datetime.datetime.strptime(m.group(1), '%Y-%m-%d_%H-%M-%S%z').timestamp()


def plausible(buf, offset, count=64):
    """How many of the first `count` records at `offset` look like records: zero
    padding, a length of at most 8, no flag but bit 0, an id that fits its format, and
    a time no earlier than the record before and less than ten seconds after it."""
    good = 0
    last = None
    for i in range(count):
        at = offset + i * RECORD.size
        if at + RECORD.size > len(buf):
            break
        t_us, ident, dlc, flags, pad, _ = RECORD.unpack_from(buf, at)
        valid_id = ident <= 0x7FF or (ident & 0x80000000 and ident & 0x7FFFFFFF <= 0x1FFFFFFF)
        in_time = last is None or 0 <= t_us - last < 10_000_000
        if pad == 0 and dlc <= 8 and flags <= 1 and valid_id and in_time:
            good += 1
        last = t_us
    return good


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('file')
    p.add_argument('--start', type=float, default=None,
                   help='wall-clock time, epoch seconds, of the chip\'s start; '
                        'from the file\'s name if it has a stamp, else 0')
    p.add_argument('--interface', default='can0')
    a = p.parse_args()

    with open(a.file, 'rb') as f:
        buf = f.read()

    offset = max((0, 8, 16), key=lambda o: plausible(buf, o))
    if offset:
        print(f'{a.file}: records start {offset} bytes in', file=sys.stderr)

    start = a.start
    end = written(a.file)
    if start is None and end is not None:
        last = len(buf) - (len(buf) - offset) % RECORD.size - RECORD.size
        if last >= offset:
            start = end - RECORD.unpack_from(buf, last)[0] / 1e6
            print(f'{a.file}: last written {datetime.datetime.fromtimestamp(end).astimezone():%Y-%m-%d %H:%M:%S %z}, '
                  f'so the chip started at {start:.0f}', file=sys.stderr)
    if start is None:
        start = 0.0

    out = sys.stdout
    count = 0
    for at in range(offset, len(buf) - RECORD.size + 1, RECORD.size):
        t_us, ident, dlc, flags, _, data = RECORD.unpack_from(buf, at)
        dlc = min(dlc, 8)
        if ident & 0x80000000:
            name = f'{ident & 0x1FFFFFFF:08X}'
        else:
            name = f'{ident & 0x7FF:03X}'
        payload = 'R' if flags & 1 else data[:dlc].hex().upper()
        out.write(f'({start + t_us / 1e6:.6f}) {a.interface} {name}#{payload}\n')
        count += 1

    left = (len(buf) - offset) % RECORD.size
    print(f'{a.file}: {count} frames' + (f', {left} bytes left over at the end' if left else ''),
          file=sys.stderr)


if __name__ == '__main__':
    main()
