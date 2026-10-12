#!/usr/bin/env python3
"""Drive and log the bluecan_bench sketch over USB CDC.

    ./tools/bench.py 10                 # stream 10 s of output
    ./tools/bench.py 10 -c vd           # send 'v' then 'd' first, then stream
    ./tools/bench.py 10 -c z --after 2  # zero counters 2 s in, so the log has a baseline
    ./tools/bench.py 20 --at 3:m --at 9:m   # switch mode at t=3 s and back at t=9 s

--at exists so a whole bench run is one command. Half of these tests need something
to happen on the board at a known moment relative to something happening on the
CANable, and juggling two shells against a wall clock is how you get a log nobody
can interpret afterwards.

Two things about this board make a plain `cat /dev/ttyACM0` the wrong tool:

DTR must be asserted. The ESP32-S3 Arduino core's USBCDC gates output on the host's
DTR line -- with DTR low the sketch runs happily and prints into the void, which looks
exactly like a crashed sketch. Nothing else distinguishes the two, so this cost an
hour once. Asserting it is safe here specifically because USB is native to the SoC:
there is no USB-serial bridge whose DTR/RTS can be wired to EN, so unlike almost every
other ESP32 board, the host cannot reset this one by touching the modem lines. See
../../wsl-setup.md.

RTS is left low for the same reason in reverse: it costs nothing, and it keeps the
one line esptool does drive during a reset out of the picture.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seconds', type=float, nargs='?', default=10.0)
    ap.add_argument('-p', '--port', default='/dev/ttyACM0')
    ap.add_argument('-c', '--commands', default='',
                    help="single-character sketch commands, e.g. 'vd'; send '?' for the list")
    ap.add_argument('--after', type=float, default=0.0,
                    help='delay in seconds before sending the commands')
    ap.add_argument('--at', action='append', default=[], metavar='SECONDS:CMD',
                    help='send CMD at SECONDS into the run; repeatable')
    args = ap.parse_args()

    s = serial.Serial()
    s.port = args.port
    s.baudrate = 115200
    s.dtr = True
    s.rts = False
    s.timeout = 0.2
    s.open()

    t0 = time.time()
    end = t0 + args.seconds
    sent = not args.commands
    send_at = t0 + args.after

    timed = []
    for spec in args.at:
        when, _, cmd = spec.partition(':')
        timed.append((t0 + float(when), cmd))
    timed.sort()

    while time.time() < end:
        if not sent and time.time() >= send_at:
            for c in args.commands:
                s.write(c.encode())
                s.flush()
                time.sleep(0.05)
            sent = True
        while timed and time.time() >= timed[0][0]:
            _, cmd = timed.pop(0)
            for c in cmd:
                s.write(c.encode())
                s.flush()
                time.sleep(0.05)

        line = s.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', 'replace'))
            sys.stdout.flush()

    s.close()


if __name__ == '__main__':
    main()
