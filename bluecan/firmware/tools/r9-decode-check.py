#!/usr/bin/env python3
"""Hold Bluecan's R9 decoder to r9.dbc, over every frame the captures hold.

    ../../analysis/.venv/bin/python tools/r9-decode-check.py
    ../../analysis/.venv/bin/python tools/r9-decode-check.py path/to/capture.log.xz ...

Bluecan decodes a handful of R9 signals for its panel, in src/canbus/r9.hpp, written by
hand from analysis/r9.dbc. Two hand-written copies of the same equations drift, and the
panel is the one place a wrong one would be believed: it is what gets held up against the
dash. So this compiles the firmware's own header on the host, runs the captures through
it, and decodes the same payloads again with cantools from the DBC.

The DBC cannot join the grip, the valve or the wheel speeds -- each is split over fields
that are not adjacent -- so its comments carry those equations in words, and they are written out
again below. That is a third copy, and it is the reason this file is short enough to
read against the DBC in one sitting.

Needs cantools, which analysis/tools/0-setup.sh puts in analysis/.venv, and g++.
"""
import lzma
import subprocess
import sys
from pathlib import Path

try:
    import cantools
except ImportError:
    sys.exit('cantools not found -- run this with analysis/.venv/bin/python')

HERE = Path(__file__).resolve().parent
MOTOCAN = HERE.parents[2]
BLUECAN = HERE.parent / 'bluecan'
DBC = MOTOCAN / 'analysis' / 'r9.dbc'
BUILD = HERE.parent / 'build'

# The firmware works in integers and truncates; the DBC gives floats. One unit of the
# firmware's own resolution is the most they may differ by.
TOLERANCE = 1.0


def expected(db, can_id, payload):
    """What r9.dbc says this payload holds, in the firmware's units."""
    s = db.decode_message(can_id, payload, decode_choices=False)
    if can_id == 0x27C:
        return {'rpm': s['EngineSpeed'], 'gear': s['Gear'], 'tcs': s['TCS'], 'pwr': s['PWR']}
    if can_id == 0x756:
        return {'rear': s['RearSpeed_Hi'] * 256 + s['RearSpeed_Mid'] * 16 + s['RearSpeed_Lo']}
    if can_id == 0x765:
        return {'front': s['FrontSpeed_Hi'] * 256 + s['FrontSpeed_Mid'] * 16 + s['FrontSpeed_Lo']}
    if can_id == 0x76E:
        return {'track': 1 if s['Display'] == 16 else 0}
    if can_id == 0x512:
        return {'water': s['WaterTemp'] * 10, 'air': s['AirTemp'] * 10}
    if can_id == 0x758:
        grip = s['Grip_Hi'] * 4096 + s['Grip_Lo12']
        valve = s['ValveMeasured_Hi'] * 256 + s['ValveMeasured_Lo']
        return {'grip': grip / 31672 * 1000, 'valve': valve / 16 / 144.4 * 1000}
    if can_id == 0x759:
        valve = s['ValveCommanded_Hi'] * 16 + s['ValveCommanded_Lo']
        return {'valve_cmd': valve / 144.4 * 1000}
    raise ValueError(f'{can_id:03X} is not an id the decoder knows')


def main():
    captures = [Path(a) for a in sys.argv[1:]] or sorted(
        (MOTOCAN / 'analysis' / 'captures').glob('session-*/capture.log.xz'))
    if not captures:
        sys.exit('no captures found')

    BUILD.mkdir(exist_ok=True)
    dump = BUILD / 'r9-decode-dump'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', str(BLUECAN / 'src'),
                    '-o', str(dump), str(HERE / 'r9-decode-dump.cpp')], check=True)

    db = cantools.database.load_file(str(DBC))
    bad = 0

    for cap in captures:
        # Read here and handed over as bytes: an LZMAFile passed as stdin gives the
        # child the compressed file underneath it.
        with lzma.open(cap, 'rb') as f:
            out = subprocess.run([str(dump)], input=f.read(), capture_output=True, check=True).stdout.decode()

        payloads = {}
        worst = {}
        for line in out.splitlines():
            can_id, hexdata, *fields = line.split()
            can_id = int(can_id, 16)
            # the DBC's own length, zero-padded, as the firmware pads its copy
            length = db.get_message_by_frame_id(can_id).length
            want = expected(db, can_id, bytes.fromhex(hexdata).ljust(length, b'\0')[:length])
            payloads[can_id] = payloads.get(can_id, 0) + 1

            for field in fields:
                name, got = field.split('=')
                diff = abs(float(got) - want[name])
                worst[name] = max(worst.get(name, 0.0), diff)
                if diff > TOLERANCE:
                    bad += 1
                    if bad <= 10:
                        print(f'!! {can_id:03X} {hexdata} {name}: firmware {got}, DBC {want[name]:.2f}')

        print(f'{cap.parent.name}')
        print('   distinct payloads: ' + ', '.join(f'{i:03X} {n}' for i, n in sorted(payloads.items())))
        print('   worst difference:  ' + ', '.join(f'{k} {v:.2f}' for k, v in sorted(worst.items())))

    if bad:
        sys.exit(f'FAILED: {bad} values differ from the DBC by more than {TOLERANCE}')
    print('ok: the firmware decodes every payload as the DBC does')


if __name__ == '__main__':
    main()
