#!/usr/bin/env python3
"""Watch what the status pixel is doing, in words, while you look at it.

    ./tools/led-state.py                    # just watch
    ./tools/led-state.py --rate 100         # ...and generate 100 msg/s to watch it with
    ./tools/led-state.py --rate 20 --secs 60

The pixel is the one instrument on this board that a shell cannot read: the panel at
least reports its own repaint cost over serial, and a colour reports nothing at all.
So the firmware says what colour it is on -- see report_status_led() in
src/display/display.cpp -- both on the 5 s stats cadence and, more usefully, the moment
the rung changes. This holds the console open and prints those lines with a clock, so
"what the firmware thinks the light is" and "what the light looks like" can be compared
by one person at one desk.

**Needs a --debug build.** A release build prints nothing at all; reflash with
./tools/reflash-bluecan.sh --debug first, or this will sit there showing nothing and look
like a fault.

The rate is capped at 100 msg/s on purpose. Nothing here is a throughput measurement --
ble-load.py is for that -- and every rung above 40 msg/s pins the blip rate to its 5 Hz
clamp anyway, so a faster bus tells you strictly less about the light while making the
board work harder. 20 msg/s is what one allowed id at RaceChrono's 50 ms actually looks
like, and is the most realistic thing to watch.

Traffic goes out on can0, which must already be up in one-shot mode:

    sudo ./tools/can-up.sh --one-shot

because Bluecan is listen-only in hardware and nothing on this bench ACKs the CANable.
"""
import argparse
import subprocess
import sys
import time

import serial

MAX_RATE = 100

# lines worth showing. Everything else the DEBUG build prints is about throughput, and
# this tool is not about throughput.
KEEP = (
    'Status light:',
    'Bluetooth LE link:',
    'ID request',
    'RaceChrono msg/s:',
    'Dropped frames:',
    'Bluetooth LE lost:',
)


def can_is_up() -> bool:
    try:
        out = subprocess.run(['ip', '-brief', 'link', 'show', 'can0'],
                             capture_output=True, text=True, timeout=5).stdout
    except Exception:
        return False
    return 'UP' in out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--rate', type=int, default=0,
                    help=f'frames/s to generate on can0, 0 for none (max {MAX_RATE})')
    ap.add_argument('--secs', type=float, default=0.0,
                    help='stop after this long; 0 runs until Ctrl-C')
    ap.add_argument('--id', default='200', help='arbitration id to generate, hex')
    ap.add_argument('--all', action='store_true',
                    help='print every line the board prints, not just the light ones')
    args = ap.parse_args()

    if args.rate > MAX_RATE:
        sys.exit(f'--rate is capped at {MAX_RATE} msg/s; see the docstring for why')

    gen = None
    if args.rate > 0:
        if not can_is_up():
            sys.exit('can0 is not up: sudo ./tools/can-up.sh --one-shot')
        gap = max(1, round(1000 / args.rate))
        gen = subprocess.Popen(
            ['cangen', 'can0', '-g', str(gap), '-I', args.id, '-L', '8', '-D', 'i'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f'>> cangen on can0: id 0x{args.id}, gap {gap} ms (~{1000 // gap} msg/s)')

    s = serial.Serial()
    s.port = args.port
    s.baudrate = 115200
    # The ESP32-S3 gates its USB CDC output on DTR. Without this the board looks dead.
    s.dtr = True
    s.rts = False
    s.timeout = 0.2
    s.open()

    print('>> watching. Connect or disconnect the phone, press D0, pull the CAN pair --')
    print('>> the light reports itself the moment it changes rung. Ctrl-C to stop.')

    t0 = time.time()
    end = t0 + args.secs if args.secs else None

    try:
        while end is None or time.time() < end:
            line = s.readline().decode('utf-8', 'replace').strip()
            if not line:
                continue
            if args.all or any(k in line for k in KEEP):
                print(f'[{time.time() - t0:7.1f}] {line.strip()}')
                sys.stdout.flush()
    except KeyboardInterrupt:
        print()
    finally:
        s.close()
        if gen is not None:
            gen.terminate()
            gen.wait(timeout=5)


if __name__ == '__main__':
    main()
