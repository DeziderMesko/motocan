#!/usr/bin/env python3
"""Hold Bluecan's console open under load for as long as you like, and say what broke.

    ./tools/bluecan-soak.py 600                  # 10 minutes, traffic already running
    ./tools/bluecan-soak.py 600 --gap 1          # generate the traffic too, 1 ms gap
    ./tools/bluecan-soak.py 60 --gap 1 --quiet   # summary only

Why this exists next to soak.sh: that one drives `bluecan_bench`, which ACKs the CANable
and takes single-character commands over serial. Bluecan does neither -- it is
listen-only in hardware and has no command interface -- so a Bluecan soak is exactly
this:
open the port, keep reading, and afterwards say whether the board stayed up.

Two things make "did it stay up" harder to answer than it sounds.

A panic is invisible on this board. ESP-IDF's console is UART0 here
(CONFIG_ESP_CONSOLE_UART_DEFAULT), on pins nothing is wired to, while Serial is TinyUSB
CDC. Every Guru Meditation and every backtrace goes out of the port nobody is reading;
what the laptop sees is the CDC device disappearing, which is indistinguishable from a
USB fault. So a reboot has to be inferred from the counters instead.

And the obvious counter lies. `Display draw: worst` is re-established at ~251 ms on every
boot -- that is the panel's first full paint, 417 characters into an empty change-
detection cache -- so it looks continuous across a reset. `Census ids: N, M over` does
not: under traffic M only ever grows, so any fall in it is a restart. That is what this
watches, along with the queue peak and the drop and offered totals, which are cumulative
for the same reason and fall the same way.

See ../firmware.md, patch 15, for the fault this was written to prove was gone.
"""
import argparse
import re
import subprocess
import sys
import time

import serial

# Cumulative since boot, every one of them: a fall means the board restarted.
CUMULATIVE = {
    'census':  re.compile(r'Census ids:\s*\d+,\s*(\d+) over'),
    'peak':    re.compile(r'Queue peak:\s*(\d+)'),
    'dropped': re.compile(r'Dropped frames:\s*(\d+)'),
    'offered': re.compile(r'offered:\s*(\d+)'),
}
RATE = re.compile(r'CAN bus msg/s:\s*([\d.]+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seconds', type=float, nargs='?', default=600.0)
    ap.add_argument('-p', '--port', default='/dev/ttyACM0')
    ap.add_argument('-i', '--iface', default='can0')
    ap.add_argument('-g', '--gap', type=float,
                    help='start cangen at this inter-frame gap in ms and stop it at the end')
    ap.add_argument('-q', '--quiet', action='store_true', help='summary only')
    args = ap.parse_args()

    gen = None
    if args.gap is not None:
        gen = subprocess.Popen(
            ['cangen', args.iface, '-g', str(args.gap), '-I', 'i', '-L', '8'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.dtr = True          # the S3's USB CDC gates output on it; see bench.py
    ser.rts = False
    ser.timeout = 0.5

    t0 = time.time()
    lines = 0
    reboots = 0
    last = {}
    rates = []
    died = None

    try:
        ser.open()
        while time.time() - t0 < args.seconds:
            raw = ser.readline()
            if not raw:
                continue
            t = time.time() - t0
            txt = raw.decode('utf-8', 'replace').rstrip()
            lines += 1

            for name, rx in CUMULATIVE.items():
                m = rx.search(txt)
                if m:
                    v = int(m.group(1))
                    if name in last and v < last[name]:
                        reboots += 1
                        print(f'[{t:7.2f}] !! REBOOT: {name} fell {last[name]} -> {v}')
                    last[name] = v

            m = RATE.search(txt)
            if m:
                rates.append(float(m.group(1)))

            if 'Starting up on core' in txt:
                reboots += 1
                print(f'[{t:7.2f}] !! REBOOT: boot banner')

            if not args.quiet:
                print(f'[{t:7.2f}] {txt}')
    except Exception as e:
        died = (time.time() - t0, f'{type(e).__name__}: {e}')
    finally:
        if gen:
            gen.terminate()
        try:
            ser.close()
        except Exception:
            pass

    print('---')
    if died:
        print(f'DIED at {died[0]:.2f} s after {lines} lines: {died[1]}')
        print('The port going away is the board rebooting, not a USB fault. See patch 15.')
    else:
        print(f'survived {args.seconds:.0f} s, {lines} lines')
    if rates:
        print(f'CAN bus msg/s: {min(rates):.0f} min, {sum(rates)/len(rates):.0f} mean, '
              f'{max(rates):.0f} max, over {len(rates)} samples')
    for name in ('peak', 'dropped', 'census', 'offered'):
        if name in last:
            print(f'{name}: {last[name]}')
    print(f'reboots detected: {reboots}')

    sys.exit(1 if died or reboots else 0)


if __name__ == '__main__':
    main()
