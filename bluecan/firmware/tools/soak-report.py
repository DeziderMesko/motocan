#!/usr/bin/env python3
"""Turn a soak log into a verdict.

Called by soak.sh. A 30-minute run is 1800 status lines; nobody reads those, and the
things worth catching are all trends or single anomalies buried in the middle. So the
questions get asked explicitly:

  did every frame arrive          Feather rx delta vs the CANable's own TX counter
  did the firmware stay up        uptime must increase monotonically for the whole run
  did anything leak               free heap, first vs last, and the low-water mark
  did the bus ever complain       tec/rec/drop, which must be flat zero throughout
  did any pass through loop()     max loop time vs the RX queue's depth in time
  ever get close to losing data
"""
import re
import sys

LINE = re.compile(
    r'up\s+(\d+)s \| (\S+)\s+(\S+)\s+\| rx (\d+)\s+(\d+)/s \| tx (\d+)\s+fail (\d+)\s+\| '
    r'tec (\d+)\s+rec (\d+)\s+drop (\d+)\s+\| ids (\d+) \| loop (\d+) us draw (\d+) us \| '
    r'heap (\d+) min (\d+) \| dstack (\d+)')


def main():
    log, can_tx, can_rx, elapsed, link = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]

    rows, notes = [], []
    for raw in open(log, encoding='utf-8', errors='replace'):
        m = LINE.search(raw)
        if m:
            rows.append([int(x) if x.isdigit() else x for x in m.groups()])
        elif raw.startswith('!!') or 'reset reason' in raw or raw.startswith('>>'):
            notes.append(raw.rstrip())

    if not rows:
        print('NO PARSEABLE STATUS LINES -- the board went quiet or never started')
        for n in notes[:20]:
            print('  ', n)
        sys.exit(1)

    # soak.sh zeroes the Feather's counters a couple of seconds in, so the first few
    # status lines still carry whatever the board had been counting beforehand.
    # Anchor on the zeroing rather than on the first line -- and complain if a counter
    # goes backwards anywhere else, because that is not a command, that is a bug.
    def went_back(i):
        return rows[i][3] < rows[i - 1][3] or rows[i][5] < rows[i - 1][5]

    breaks = [i for i in range(1, len(rows)) if went_back(i)]
    anchor, stray = 0, []
    if breaks:
        anchor = breaks[0] if breaks[0] <= 15 else 0
        stray = [i for i in breaks if i > 15]
    rows = rows[anchor:]

    up = [r[0] for r in rows]
    rx, tx = [r[3] for r in rows], [r[5] for r in rows]
    rate = [r[4] for r in rows]
    fail, tec, rec, drop = (max(r[i] for r in rows) for i in (6, 7, 8, 9))
    loop_max = max(r[11] for r in rows)
    heap = [r[13] for r in rows]
    heap_min = min(r[14] for r in rows)
    dstack = min(r[15] for r in rows)
    ids = max(r[10] for r in rows)

    resets = [(up[i - 1], up[i]) for i in range(1, len(up)) if up[i] < up[i - 1]]
    # Frames the Feather saw, minus the ones already counted before the load started.
    rx_delta = rx[-1] - rx[0]
    tx_delta = tx[-1] - tx[0]

    print('=' * 72)
    print(f'SOAK REPORT  --  {elapsed} s ({elapsed / 60:.1f} min), {len(rows)} status lines')
    print('=' * 72)

    def verdict(ok, label, detail):
        print(f'  [{"PASS" if ok else "FAIL"}]  {label:<26} {detail}')
        return ok

    ok = True

    # Before believing any frame count, check the log actually spans the run. If the
    # logger stopped early, the last count in it predates the counter it is about to be
    # compared against, and the difference is elapsed time, not lost data -- a mistake
    # worth making exactly once. When that happens the delivery checks are not reported
    # as failures, because they measure nothing.
    logged = up[-1] - up[0]
    covered = logged >= elapsed - 20
    verdict(covered, 'log covers the run',
            f'{logged}s logged of {elapsed}s elapsed'
            + ('' if covered else '  <-- logger stopped early; counts below are meaningless'))

    # A few frames of slack each way: the counters are sampled from two machines a
    # moment apart, so an exact match is luck, not correctness.
    slack = 30
    if not covered:
        print(f'  [SKIP]  {"CANable -> Feather":<26} {rx_delta} received / {can_tx} sent')
        print(f'  [SKIP]  {"Feather -> CANable":<26} {tx_delta} sent')
        ok = False
    else:
        ok &= verdict(abs(rx_delta - can_tx) <= slack, 'CANable -> Feather',
                      f'{rx_delta} received / {can_tx} sent  (miss {can_tx - rx_delta})')
    # can0's RX counter includes the local echo of its own transmissions, so what it
    # should have logged is everything it sent plus everything the Feather sent. This
    # is the same echo that makes loopback mode show every frame twice in candump --
    # see ../wsl-setup.md -- and forgetting it makes the arithmetic look catastrophic.
        expected_can_rx = can_tx + tx_delta
        ok &= verdict(abs(can_rx - expected_can_rx) <= slack, 'Feather -> CANable',
                      f'{tx_delta} sent / {can_rx - can_tx} received  '
                      f'(miss {expected_can_rx - can_rx})')
    ok &= verdict(drop == 0, 'frames dropped by driver', f'{drop}')
    ok &= verdict(not resets, 'firmware uptime', f'{up[0]}s -> {up[-1]}s, {len(resets)} resets')
    ok &= verdict(not stray, 'counters monotonic',
                  'clean' if not stray else f'went backwards at lines {stray} -- not the zeroing')
    ok &= verdict(tec == 0 and rec == 0, 'bus error counters', f'tec max {tec}, rec max {rec}')
    ok &= verdict(fail == 0, 'transmit failures', f'{fail}')
    ok &= verdict(heap[-1] >= heap[0] - 4096, 'heap',
                  f'{heap[0]} -> {heap[-1]} ({heap[-1] - heap[0]:+d}), low-water {heap_min}')
    ok &= verdict(dstack > 512, 'display task stack', f'{dstack} bytes free at worst')

    # 64 frames of RX queue; at the observed rate, how long is that in real time?
    busy = [r for r in rate if r > 0]
    avg = sum(busy) / len(busy) if busy else 0
    budget_us = 64 / avg * 1e6 if avg else float('inf')
    ok &= verdict(loop_max < budget_us * 0.5, 'worst loop() pass',
                  f'{loop_max} us vs {budget_us:.0f} us of RX queue at {avg:.0f} frames/s')

    print()
    print(f'  ids seen: {ids}   sustained rate: {avg:.0f}/s   peak: {max(rate)}/s')
    print(f'  can0 now: {link.strip()}')
    if notes:
        print('  board said:')
        for n in notes:
            print(f'    {n}')
    print()
    print('VERDICT:', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
