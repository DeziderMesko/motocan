#!/usr/bin/env python3
"""Find where the BLE link to RaceChrono stops keeping up with the bus.

    sudo -v && ./tools/ble-load.py                 # the standard ramp: 10, 200, 500 msg/s
    sudo -v && ./tools/ble-load.py -g 5 -g 2 -g 1  # custom cangen gaps, in ms
    ./tools/ble-load.py --seconds 30               # longer bursts

Needs a DEBUG build on the board (./tools/reflash-bluecan.sh --debug), can0 up, and a
phone connected and *subscribed* -- the header on the glass must read SUB, not BLE.
It refuses to run otherwise, because a link nobody is listening to reports no
congestion and the whole measurement would read as a clean pass.

Why a script rather than cangen and a serial console side by side: the board stops
printing its counters exactly when the answer gets interesting. `stats()` is called
after the drain loop, and once the queue is permanently non-empty that loop never
exits, so a saturated device goes quiet. Everything here is therefore read as a
*total* before and after the burst, never sampled during it.

Counters are cumulative and there is no way to zero them without rebooting, so the
queue high-water mark is only meaningful for the highest rate in an ascending ramp.
Dropped frames and BLE refusals are plain counters and are exact per run.
"""
import argparse
import re
import subprocess
import sys
import time

import serial

STATS = {
    'offered':  re.compile(r'Bluetooth LE offered:\s*(\d+)\s+transmitted:\s*(\d+)'),
    'lost':     re.compile(r'Bluetooth LE lost:\s*(\d+)\s+nosub:\s*(\d+)\s+err:\s*(-?\d+)'),
    'link':     re.compile(r'Bluetooth LE link:\s*(\w+)\s+peers\s+(\d+)\s+sub\s+(\w+)'),
    'dropped':  re.compile(r'Dropped frames:\s*(\d+)'),
    'peak':     re.compile(r'Queue peak:\s*(\d+)'),
}


def pump(ser, duration, state):
    """Read the console for `duration` seconds, keeping `state` at the newest values.

    The serial port must be drained continuously, not sampled at the ends: the board
    prints every 5 s whether anyone is listening, and a burst spent not reading fills
    the OS buffer, so the read afterwards returns lines from *before* the burst. That
    reads as a device doing almost nothing under load, which is a believable enough
    answer to be dangerous.

    Returns how many complete stats blocks were seen, which is itself a measurement:
    a saturated board stops printing, because stats() is called after a drain loop
    that never exits while the queue stays non-empty.
    """
    end = time.time() + duration
    blocks = 0
    seen = set()
    while time.time() < end:
        line = ser.readline().decode('utf-8', 'replace')
        if not line:
            continue
        for name, rx in STATS.items():
            m = rx.search(line)
            if m:
                state[name] = m.groups()
                seen.add(name)
                if len(seen) == len(STATS):
                    blocks += 1
                    seen.clear()
    return blocks


def settle(ser, state, timeout=14.0):
    """Pump until at least one complete block has landed, or give up."""
    end = time.time() + timeout
    while time.time() < end:
        if pump(ser, 1.0, state) and len(state) == len(STATS):
            return
    if len(state) < len(STATS):
        missing = ', '.join(sorted(set(STATS) - set(state)))
        raise RuntimeError(f'no complete stats block in {timeout:.0f} s (missing: {missing}). '
                           'Is this a --debug build?')


def tx_packets(iface):
    out = subprocess.run(['ip', '-s', 'link', 'show', iface],
                         capture_output=True, text=True).stdout
    # the TX block is the second "bytes packets" table
    nums = re.findall(r'^\s*(\d+)\s+(\d+)\s+\d+\s+\d+\s+\d+\s+\d+\s*$', out, re.M)
    return int(nums[1][1]) if len(nums) > 1 else 0


def bounce(iface):
    subprocess.run(['sudo', 'ip', 'link', 'set', iface, 'down'], check=False)
    subprocess.run(['sudo', 'ip', 'link', 'set', iface, 'up', 'type', 'can',
                    'bitrate', '500000'], check=True)
    time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-g', '--gap', action='append', type=float, default=[],
                    help='cangen inter-frame gap in ms, fractional allowed; repeatable. '
                         'Default: 100, 5, 2')
    ap.add_argument('-p', '--port', default='/dev/ttyACM0')
    ap.add_argument('-i', '--iface', default='can0')
    ap.add_argument('--id', default='200', help='CAN id to generate, hex')
    ap.add_argument('--seconds', type=float, default=20.0, help='burst length')
    args = ap.parse_args()

    gaps = args.gap or [100, 5, 2]

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.dtr = True          # the S3's USB CDC gates output on it; see bench.py
    ser.rts = False
    ser.timeout = 0.5
    ser.open()

    print('>> waiting for a stats block')
    state = {}
    settle(ser, state)
    up, peers, sub = state['link']
    if up != 'up' or sub != 'yes':
        ser.close()
        sys.exit(f'!! link is "{up}", subscribed "{sub}" -- connect the phone in RaceChrono '
                 'and check the header reads SUB before measuring')
    if peers != '1':
        print(f'!! peers is {peers}, expected 1: every frame goes out once per subscriber')

    print(f'>> link up, {peers} peer, subscribed. Bursts of {args.seconds:.0f} s.\n')
    print(f'{"bus msg/s":>10} {"ble msg/s":>10} {"lost":>7} {"dropped":>8} '
          f'{"tx/frame":>12} {"q peak":>7} {"blocks":>7}')
    print('-' * 70)

    for gap in gaps:
        bounce(args.iface)
        ser.reset_input_buffer()
        settle(ser, state)
        before = dict(state)
        tx0 = tx_packets(args.iface)

        gen = subprocess.Popen(['cangen', args.iface, '-g', f'{gap:g}',
                                '-I', args.id, '-L', '8', '-D', 'i'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        blocks = pump(ser, args.seconds, state)
        gen.kill()          # -9: it wedges on a stopped TX queue, see ../../README.md
        gen.wait()
        elapsed = time.time() - t0
        tx1 = tx_packets(args.iface)

        # Nothing is transmitting now, so the counters are static: whatever block lands
        # next carries the burst's totals however long it takes to arrive.
        pump(ser, 2.0, state)       # drain the queue
        settle(ser, state)
        after = dict(state)

        d_off = int(after['offered'][0]) - int(before['offered'][0])
        d_not = int(after['offered'][1]) - int(before['offered'][1])
        d_lost = int(after['lost'][0]) - int(before['lost'][0])
        d_drop = int(after['dropped'][0]) - int(before['dropped'][0])
        peak = int(after['peak'][0])

        ratio = (d_not / d_off) if d_off else 0.0
        print(f'{(tx1 - tx0) / elapsed:10.1f} {d_off / elapsed:10.1f} {d_lost:7d} '
              f'{d_drop:8d} {ratio:12.2f} {peak:7d} {blocks:7d}')

    print('\n>> tx/frame should read 1.00: it counts BLE_GAP_EVENT_NOTIFY_TX, the frame')
    print('>> going onto the air, against the frames handed to the host. It read 2.00')
    print('>> while the frame path went through BLECharacteristic::notify(), which')
    print('>> reports the host accepting a frame as a success of its own.')
    print('>> q peak is a high-water mark since boot, so only the last row is its own.')
    print('>> blocks counts stats prints during the burst -- it falls to zero when the')
    print('>> board saturates, because stats() sits after a drain loop that stops exiting.')
    ser.close()


if __name__ == '__main__':
    main()
