# RaceChrono's Monitor API as a clock, the bench test of 2026-10-20

Whether the phone's time can reach Bluecan through RaceChrono, so that a microSD log
can carry wall-clock time. Android, RaceChrono Pro. The answer is yes, to the second,
while RaceChrono records with a GPS fix, and only as the moment a new second arrives.

## How

The DIY API has a Monitor half besides CAN-Bus: the device indicates an equation on
`0x0005`, the app answers on the same characteristic, and then writes the equation's
value as a big-endian int32 to `0x0006` (aollin/racechrono-ble-diy-device). The
equation identifiers include `timestamp`. The bench sketch
[bluecan_monitor](../bluecan/firmware/bluecan_monitor/bluecan_monitor.ino), on the
Feather, offers `0x0001`/`0x0002` and `0x0005`/`0x0006`, starts no CAN controller, and
asks for:

| | Equation |
|---|---|
| m0 | `channel(device(gps), timestamp)` |
| m1 | `channel(device(gps), timestamp) / 1000` |
| m2 | `channel(device(gps), speed) * 100`, the control |
| m3 | `(channel(device(gps), timestamp) - base) * 1000`, added once m0 or m1 reads as an epoch in seconds; base is that second minus 2 |

The board shares Bluecan's Bluetooth address, so Bluecan was removed from RaceChrono and
`RC Monitor` added with CAN-Bus and Monitor ticked. The console was read with
`bluecan/firmware/tools/bench.py 600`; the times in it are the board's `esp_timer`, in
ms since its start.

## Run 1, sketch at 1641878

[racechrono-monitor-2026-10-20-run1.txt](racechrono-monitor-2026-10-20-run1.txt), as
pasted by the owner. Two connections, a test connection (`ALLOW all, 50 ms`) and a
recording (`DENY all`, then the six ids of the R9 profile at 50 ms). Both stopped at the
first indication with `status 7`, a timeout, and the phone ended each link 25.0 s and
14.3 s after subscribing to `0x0005`.

The timeout was the library, not the phone. In arduino-esp32 3.3.11 built with NimBLE,
`BLECharacteristic::indicate()` waits on a semaphore that only the Bluedroid path gives;
the confirmation arrives as `BLE_GAP_EVENT_NOTIFY_TX` and reaches `onStatus()`, but
`indicate()` always waits out its second (`indicationTimeout`, 1000 ms) and reports
`ERROR_INDICATE_TIMEOUT`. Commit c242041 indicates with `ble_gatts_indicate_custom()`
and waits for `onStatus()`, as Bluecan already notifies frames past the wrapper. Bluecan
will need the same if it indicates.

## Run 2, sketch at c242041

[racechrono-monitor-2026-10-20-run2.txt](racechrono-monitor-2026-10-20-run2.txt),
23:37–23:47 CEST. The first lines of the first connection are missing: they were
printed before `bench.py` had the port open after the flash. Four connections:

1. **Recording, no fix** (lines 1–9). Configured, 7 filter writes. Each of m0, m1, m2
   got one value, `0x7FFFFFFF`, the API's invalid. Ended by the phone (`0x213`).
2. **Test connection, no fix** (lines 10–34). Every indication confirmed in 44–61 ms.
   m0, m1, m2 answered `00 <id>`, accepted, so `timestamp` is a valid identifier. The
   remove-all command gets no answer. No value at all in the 17 s before the phone ended
   the link.
3. **Recording, phone at the window** (lines 35–53). The link dropped with a supervision
   timeout (`0x208`) during configuration; the owner puts it down to the Feather's
   missing antenna.
4. **Recording with a fix** (lines 54–132). The owner got a fix on the balcony and came
   back to the Feather. The allow-list as in any recording, and the subscription to
   `0x0001` on. Values for m0 from 21:41:39Z to 21:41:47Z, then the fix ended, and later
   the owner stopped.

What run 2 shows:

- **The unit is Unix seconds, UTC.** m0's first valid value, `1792532499`, is
  2026-10-20 21:41:39Z, 23:41:39 in Prague, at the time of the run.
- **The resolution is one second.** m3 read 4000, 5000 … 10000: whole seconds only.
  Whether `timestamp` is whole seconds or the phone's fixes fall on whole seconds, the
  test cannot tell; either way nothing finer reaches the board.
- **The int32 conversion rounds up at .5**: m1 for `1792532500 / 1000` was `1792533`.
  One value; rounding and ceiling are not told apart.
- **A value is written when it changes**, about once a second. The eight changes of m0,
  :40 to :47, came 990.6–1035.0 ms apart. Taken against the second each one carries,
  their arrival moved from −4.4 to +80.6 ms relative to the first over those 7 s, in
  steps of about 5 and 30 ms. The connection ran at 15 ms with latency 1.
- **Without a fix there is no time**: invalid in a recording, nothing in a test
  connection. The speed behaves the same, so it is the GPS device, not the channel.
- **When the fix ends, nothing says so.** No invalid came. After 17.8 s of silence the
  app wrote all four values once more, unchanged (m0 still 21:41:47Z), and nothing after
  that until the link ended 26.8 s later. A value that arrives is not the time now; only
  a change is.
- **CAN-Bus and Monitor together work**: with both ticked, RaceChrono subscribes to
  `0x0001` and writes its allow-list exactly as it does to Bluecan with CAN-Bus alone.
  Bluecan's current advice, untick Monitor, stands only because Bluecan lacks
  `0x0005`/`0x0006`.

Two faults of the sketch in this run, fixed in f4524ad: m3 was added twice (lines 95 and
106; `configure()` picked up the equation built while it ran, then `loop()` added it
again), and the spread of phone time against board time, −17160.3 .. 714.4 ms on line
128, counted the late repeat. It now counts changes only.

## What it means for Bluecan

The phone can give Bluecan the time to the second, during a recording with a fix. That
is enough to name a log and to find the RaceChrono session it belongs to. The log would
take a record pairing the board's µs with the Unix second at each change of m0, and the
whole file is converted afterwards, so frames from before the first fix are dated too.
Anything finer than a second, against RaceChrono's own export, is still a
cross-correlation of a channel both have.
