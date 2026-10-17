#!/usr/bin/env python3
"""Evaluate the equations in a RaceChrono vehicle export against r9.dbc, on the captures.

    .venv/bin/python tools/7-check-racechrono-export.py \\
        ../bluecan/racechrono/vehicle_2026_yamaha_r9.rcz r9.dbc captures

Why this exists: an equation typed into the app on a phone is not the one on the page until
someone has compared them, and a typo such as bytesToUint for bitsToUintLe is accepted by
the app without a word. The export (.rcz) is a zip holding vehicleProfile.json; this takes
each custom CAN channel from it, evaluates its equation on every distinct payload of its id
in every capture.log.xz under the captures folder, and compares the result with the same
quantity built from the DBC's signals.

What it assumes about the app, from bluecan/racechrono-channels.md:

  bytesToUint is big-endian over whole bytes, bytesToUintLe little-endian, and
             bitsToUintLe numbers bits the way a DBC numbers a little-endian signal.
  an integer divided by an integer is rounded down, so a missing ".0" shows as a
             difference. Engine speed is let off by under 1 rpm, so that whole rpm pass.
  speeds are in m/s, so a wheel speed's reference is 0.01 km/h counts / 360.

Each channel is told apart by RaceChrono's channelId, which is the app's channel plus its
postfix; one this script does not know is printed with no reference.
"""
import glob
import json
import lzma
import re
import sys
import zipfile

import cantools


class I(int):
    """An integer that divides the way the app does: by an integer, rounded down."""

    def __truediv__(a, b):
        if isinstance(b, int):
            return I(int(a) // int(b))
        return float(a) / b

    def _keep(op):
        def f(a, b):
            r = op(int(a), b if isinstance(b, float) else int(b))
            return I(r) if isinstance(r, int) else r
        return f

    __add__ = _keep(lambda a, b: a + b)
    __radd__ = __add__
    __sub__ = _keep(lambda a, b: a - b)
    __mul__ = _keep(lambda a, b: a * b)
    __rmul__ = __mul__
    __and__ = _keep(lambda a, b: a & b)
    __rshift__ = _keep(lambda a, b: a >> b)


def bytesToUint(raw, offset, length):
    return I(int.from_bytes(raw[offset:offset + length], 'big')) if offset + length <= len(raw) else None


def bytesToUintLe(raw, offset, length):
    return I(int.from_bytes(raw[offset:offset + length], 'little')) if offset + length <= len(raw) else None


def bitsToUintLe(raw, offset, length):
    return I((int.from_bytes(raw, 'little') >> offset) & ((1 << length) - 1))


def nibbles(s, top, hi, mid, lo):
    return s[top] << 12 | s[hi] << 8 | s[mid] << 4 | s[lo]


REFERENCE = {
    10024: ('Engine RPM', lambda s: s['EngineSpeed']),
    1004: ('Gear', lambda s: s['Gear']),
    1053577: ('Digital 1, TCS', lambda s: s['TCS']),
    2102153: ('Digital 2, PWR', lambda s: s['PWR']),
    10026: ('Coolant temperature', lambda s: s['WaterTemp']),
    10031: ('Air temperature', lambda s: s['AirTemp']),
    33783: ('Wheel speed rear', lambda s: nibbles(s, 'RearSpeed_Top', 'RearSpeed_Hi', 'RearSpeed_Mid', 'RearSpeed_Lo') / 360),
    66551: ('Wheel speed front', lambda s: nibbles(s, 'FrontSpeed_Top', 'FrontSpeed_Hi', 'FrontSpeed_Mid', 'FrontSpeed_Lo') / 360),
    1058601: ('Throttle position 1, grip', lambda s: (s['Grip_Hi'] << 12 | s['Grip_Lo12']) * 100 / 31672),
    2107177: ('Throttle position 2, valve measured', lambda s: (s['ValveMeasured_Hi'] << 8 | s['ValveMeasured_Lo']) * 100 / 2310.4),
    3155753: ('Throttle position 3, valve commanded', lambda s: (s['ValveCommanded_Hi'] << 4 | s['ValveCommanded_Lo']) * 100 / 144.4),
}


def main():
    rcz, dbc, capdir = sys.argv[1:4]
    profile = json.loads(zipfile.ZipFile(rcz).read('vehicleProfile.json'))
    db = cantools.database.load_file(dbc)

    payloads = {}
    for f in sorted(glob.glob(capdir + '/*/capture.log.xz')):
        for line in lzma.open(f, 'rt'):
            m = re.search(r'\s([0-9A-F]{3})#([0-9A-F]*)', line)
            if m:
                payloads.setdefault(int(m[1], 16), set()).add(bytes.fromhex(m[2]))

    for c in profile['customChannels']:
        pid, eq = c['pid'], c['equation']
        name, ref = REFERENCE.get(c['channelId'], ('?', None))
        msg = db.get_message_by_frame_id(pid)
        n = differ = 0
        worst = 0.0
        for raw in payloads.get(pid, ()):
            try:
                got = eval(eq, {'bytesToUint': bytesToUint, 'bytesToUintLe': bytesToUintLe,
                                'bitsToUintLe': bitsToUintLe, 'raw': raw})
            except TypeError:
                got = None  # a byte read past the end of the payload, then arithmetic on it
            n += 1
            if ref is None:
                continue
            want = ref(msg.decode(raw, decode_choices=False))
            d = abs(got - want) if got is not None else float('inf')
            worst = max(worst, d)
            differ += d >= (1 if c['channelId'] == 10024 else 1e-6)
        result = 'no reference' if ref is None else f'differ {differ:5}  max diff {worst:.6g}'
        print(f'{pid:5}  {name:37} distinct {n:6}  {result}\n       {eq}')


if __name__ == '__main__':
    main()
