#!/usr/bin/env python3
"""Seed bluecan_clock's clock from this machine, and say how well it landed.

    ./tools/clock-seed.py                 # local time, /dev/ttyACM0
    ./tools/clock-seed.py --utc           # show UTC on the glass instead
    ./tools/clock-seed.py --check         # report the current error, change nothing
    ./tools/clock-seed.py --drift 120     # measure the board's RATE, 120 s window

Seed it from the SAME machine that runs candump, and from inside WSL rather than
from Windows. The point of the exercise is not a correct clock, it is one clock:
the log's timestamps come from the WSL2 kernel, so a Feather seeded from that same
kernel is consistent with the log whether or not either of them is right. Seeding
from Windows reintroduces exactly the offset this is meant to remove -- WSL2's clock
is known to drift away from the host's across a suspend.

How the seed avoids the write latency: the board answers `T` with its own monotonic
microsecond count, and `S` then maps *that* instant to an epoch. So the time `S`
takes to arrive never enters the result -- only the `T` round trip does, and we send
several and keep the fastest, which is the sample least likely to have been delayed
in either direction.

Prints the UTC offset it used. That is the number `vision_reference.py` wants as
--display-utc-offset when it reads this clock out of the video.

THE CLOCK TO DISTRUST HERE IS WSL2's, NOT THE BOARD'S. Measured three ways over
the same 124 s window, with Windows' own wall clock as the reference:

    windows  wall  124.380 s   (reference)
    wsl monotonic  120.194 s    -33658 ppm   3.4% slow, and free-running
    wsl realtime   123.562 s     -6573 ppm   slewed and stepped toward Windows
    feather timer  124.372 s        -61 ppm   a normal crystal, ~0.2 s per hour

So the board keeps better time than the machine seeding it. CLOCK_REALTIME -- which
is what candump timestamps a log with -- oscillates around true time by roughly a
second, correcting itself in ~1 s steps rather than drifting away for good. Bounded,
but it is half the pipeline's +/-2 s lag budget, and it wobbles WITHIN a session,
which a single-lag correlation cannot take out.

Two things follow. Do not read `--check` a few minutes after a seed as board drift;
most of what it shows is the host. And `--drift`, which is the honest question about
the board, asks Windows rather than either WSL clock.
"""
import argparse
import datetime
import subprocess
import sys
import time

import serial

PS = '/mnt/c/WINDOWS/System32/WindowsPowerShell/v1.0/powershell.exe'


def ping(ser):
    """One T round trip -> (rtt, board_local_us, host_epoch_at_that_instant)."""
    ser.reset_input_buffer()
    t0, m0 = time.time(), time.monotonic()
    ser.write(b'T\n')
    ser.flush()
    line = ser.readline().decode('utf-8', 'replace').strip()
    t1, m1 = time.time(), time.monotonic()
    if not line.startswith('t '):
        return None
    try:
        local_us = int(line.split()[1])
    except (IndexError, ValueError):
        return None
    # Best estimate of when the board took that reading: the middle of the trip.
    # The error left is the asymmetry of the two legs, bounded by rtt/2.
    # rtt from the monotonic pair: a CLOCK_REALTIME step landing inside the sample
    # would otherwise look like a fast trip rather than the poisoned one it is.
    return (m1 - m0, local_us, (t0 + t1) / 2.0, (m0 + m1) / 2.0)


def wall():
    """Windows' clock, over interop. The only reference here that is neither the board
    nor a WSL2 clock -- and both WSL2 clocks turned out to need one. Same trick
    serial-monitor.sh uses, for the same reason: Windows owns the hardware."""
    try:
        out = subprocess.run([PS, '-NoProfile', '-Command',
                              '[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()'],
                             capture_output=True, text=True, timeout=15).stdout.strip()
        return int(out) / 1000.0
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def wall_pair(ser):
    """One Windows reading, and where the board was at that moment.

    Spawning powershell.exe costs the best part of a second, but the timestamp is read
    at the END of the spawn -- so it is the ping taken immediately AFTER the spawn
    returns that the reading belongs with, not one taken before it and not a midpoint.
    Getting that pairing wrong is worth thousands of ppm over a few minutes, which is
    an order of magnitude more than the crystal error being measured. Do not "improve"
    this by averaging several spawns: that spreads the reading over the whole burst and
    puts the uncertainty back as placement error.
    """
    w = wall()
    p = ping(ser)
    if w is None or p is None:
        return None
    return (w, p[1] / 1e6)


def drift(ser, seconds):
    """The board's rate against Windows -- not against either WSL2 clock, both of which
    are worse than the thing being measured."""
    a = wall_pair(ser)
    if a is None:
        sys.exit('!! need both Windows interop and a board reply for --drift.')
    print(f'>> measuring for {seconds:.0f} s against windows ...')
    time.sleep(seconds)
    b = wall_pair(ser)
    if b is None:
        sys.exit('!! lost the board or Windows interop mid-measurement.')

    host = b[0] - a[0]
    board = b[1] - a[1]
    ppm = (board - host) / host * 1e6
    print(f'>> board {board:.3f} s vs windows {host:.3f} s: '
          f'{(board - host) * 1e3:+.1f} ms, {ppm:+.1f} ppm')
    print(f'   -> {abs(ppm) * 3600 / 1e6 * 1000:.0f} ms per hour of riding')


def best_of(ser, n):
    samples = [s for s in (ping(ser) for _ in range(n)) if s]
    if not samples:
        sys.exit('!! no reply to T. Is bluecan_clock flashed, and is this the right port?')
    samples.sort(key=lambda s: s[0])
    return samples[0], samples


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--samples', type=int, default=11,
                    help='T round trips to take; the fastest one is used (default 11)')
    ap.add_argument('--utc', action='store_true',
                    help='show UTC on the panel instead of this machine\'s local time')
    ap.add_argument('--check', action='store_true',
                    help='report the board against the host clock now; changes nothing')
    ap.add_argument('--drift', type=float, metavar='SECONDS', default=None,
                    help='measure the board\'s rate against CLOCK_MONOTONIC and exit')
    args = ap.parse_args()

    tz_offset = 0.0
    if not args.utc:
        off = datetime.datetime.now().astimezone().utcoffset()
        tz_offset = off.total_seconds() if off else 0.0

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    # The ESP32-S3 gates its USB CDC output on DTR. Without this the board looks dead.
    ser.dtr = True
    ser.rts = False
    ser.timeout = 1.0
    try:
        ser.open()
    except serial.SerialException as e:
        sys.exit(f'!! {e}\n   If the port is missing: ./tools/usb-attach.sh')

    # The board is silent until spoken to, so there is nothing to drain except
    # whatever a previous session left behind.
    time.sleep(0.2)
    ser.reset_input_buffer()

    if args.drift is not None:
        drift(ser, args.drift)
        ser.close()
        return

    (rtt, local_us, host_epoch, _), samples = best_of(ser, args.samples)
    spread = samples[-1][0] - samples[0][0]
    print(f'>> {len(samples)} round trips: best {rtt * 1e3:.2f} ms, '
          f'worst {samples[-1][0] * 1e3:.2f} ms, spread {spread * 1e3:.2f} ms')

    display_epoch = host_epoch + tz_offset

    if args.check:
        ser.write(b'?\n')
        ser.flush()
        print('  board :', ser.readline().decode('utf-8', 'replace').strip())
    else:
        cmd = f'S {local_us} {int(round(display_epoch * 1e6))}\n'
        ser.write(cmd.encode())
        ser.flush()
        ack = ser.readline().decode('utf-8', 'replace').strip()
        if not ack.startswith('s ok'):
            sys.exit(f'!! board rejected the seed: {ack or "(no reply)"}')
        print(f'>> {ack}')

    # Independent check, on its own round trip: ask the board where it thinks it is
    # and compare against this machine. After a seed this is the residual; with
    # --check on an already-seeded board it is the accumulated drift.
    again = ping(ser)
    if again:
        rtt2, local2, host2, _ = again
        ser.write(b'?\n')
        ser.flush()
        st = ser.readline().decode('utf-8', 'replace').strip()
        offset_us = None
        for tok in st.split():
            if tok.startswith('off='):
                offset_us = int(tok[4:])
        if offset_us is None and 'off' in st:
            parts = st.split()
            offset_us = int(parts[parts.index('off') + 1])
        if offset_us is not None:
            board_display = (local2 + offset_us) / 1e6
            residual = board_display - (host2 + tz_offset)
            print(f'>> residual {residual * 1e3:+.2f} ms '
                  f'(board minus host, checked over a {rtt2 * 1e3:.2f} ms trip)')
        print(f'  board : {st}')

    label = 'UTC' if args.utc else \
        f'local, UTC{tz_offset / 3600:+g} -- pass --display-utc-offset {tz_offset / 3600:g}'
    shown = datetime.datetime.fromtimestamp(
        display_epoch, datetime.timezone.utc).strftime('%H:%M:%S')
    print(f'>> panel shows {shown} ({label})')

    ser.close()


if __name__ == '__main__':
    main()
