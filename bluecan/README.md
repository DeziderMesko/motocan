# bluecan

The hardware half of the project: a DIY Bluetooth LE CAN-bus reader for the
Yamaha R9, built on an Adafruit Feather, talking to the
[RaceChrono](https://racechrono.com/) mobile app over its DIY BLE protocol.

> **Status, 2026-10-13.** The device works on the bench: it receives a 500 kbps bus and
> streams it to RaceChrono on a phone. It has **not** read the R9 yet. The R9's diagnostic
> connector sends 2 ids of 23 unless the listening node acknowledges — see
> [What the R9 turned out to send](#what-the-r9-turned-out-to-send) — and the firmware
> does that now, proved on the bench and not yet on the bike
> ([patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench)).
> Much of this folder was written before
> the bike was reached and is sized against another Yamaha's bus; [numbers.md](numbers.md)
> has the measured figures side by side. The firmware source is its own repository,
> [DeziderMesko/racechrono-canbus](https://github.com/DeziderMesko/racechrono-canbus).

The repository root holds the *protocol research* — what data exists and how it is
encoded on the bus. This directory holds the *device* that reads it. Documentation
now, firmware source later.

## Why this exists

**Telemetry.** Feed live CAN data into RaceChrono Pro at a high refresh rate — an
independent replacement for Y-TRAC Rev. That is this device's job, and now its only one.

For the phone it reads the bus without understanding it: RaceChrono decodes channels
app-side (CAN id + offset + length + equation, entered in the app), so the firmware
forwards raw frames and unknown mappings never block it. For its own panel it does decode
four frames now — engine speed, gear, TCS, temperatures, grip and throttle valve — kept
ahead of the phone's filter so the glass does not depend on the app's channels, and
shown on the LIVE page it boots into
([patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench)).

### Sniffing is the CANable's job, not this one

Turning [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md)'s 🟡 rows into 🟢 ones needs raw traffic
off a real R9, and this device was originally meant to capture it too — built
sniffer-first, with no allow-list and a display showing bus activity. **That split is
settled the other way now:** capture is done with the **CANable on a laptop**, which is
already the tool the whole bench was built around, records everything `candump` can
write to disk, and has no radio in the way. The Feather carries telemetry to a phone.

Two things follow, and they are why the decision is worth writing down rather than
implying:

- **The rate problem stops being a problem.** The link gives up somewhere around
  590 msg/s against the R9's ~1050 (see [Status](#status)), which is fatal for forwarding
  a whole bus and irrelevant for telemetry — RaceChrono asks for the ids it has channels
  for, and that is **nine of them** for every item Y-TRAC Rev shows, ~170 msg/s at the
  50 ms per id the app actually asks for. The frames are counted in
  [how many frames this actually is](../analysis/background/pre-bike-mapping.md#how-many-frames-this-actually-is).
- **The firmware should honour the allow-list** rather than grow a second sniffer build
  or a mode switch on the glass. It parsed the app's requests and ignored them until
  [patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured),
  which is now written **and** measured: an unasked-for id never leaves the CAN ISR, and
  635.8 msg/s on the bus becomes 19.6 over the radio with zero refusals.

The panel keeps its ID census and raw-frame pages: knowing what is on the bus is useful
on a bike whether or not this device is what records it.

### What the R9 turned out to send

Everything above and below was built before anything had been read off an R9. The CANable
did that on 2026-10-11, three captures on the diagnostic connector, and
[r9-signals.md](../analysis/r9-signals.md) has them. For this device they change
three things, and leave what it can do exactly where it was measured.

- **The bus is smaller than it was sized for.** 23 ids at **506 frames a second**, not the
  ~1050 across 16 borrowed from the R1. That is under the ~590 msg/s the radio carries
  cleanly, so on this bike even an allow-all — the app configuring channels — fits. The
  ceiling, the queue depth and the soaks in [numbers.md](numbers.md) are what the device
  can take and none of them moved; the R9 simply asks for less.
- **Telemetry is four ids, not nine.** `0x27C` (engine speed, gear, TCS, the mode field) at
  100 Hz, `0x758` and `0x759` (grip and throttle valve) at 20 Hz, `0x512` (temperatures) at
  1 Hz: 141 frames a second on the bus and about 61 over the radio at the app's 50 ms. The
  wheel speeds are not found yet. None of the ids the mapping predicted — `0x20A`, `0x216`,
  `0x236`, `0x23E` — exist on this connector, so channel definitions written against them
  are for another bike.
- **Listen-only does not work on this connector, and the device will have to ACK.** The
  diagnostic connector is a pair with only the ECU on it —
  [R9_CANbus.md](../analysis/R9_CANbus.md) has the wiring from the service manual. With
  nothing acknowledging, the ECU resends one frame about 3 800 times a second and two ids
  of 23 get through, a couple of samples a second each. The CANable's sessions only became
  useful once it acknowledged. So patch 2, "the one change that must land before the
  device touches an R9", has to be turned round for this bike: the controller in normal
  mode, and the TX pin — which upstream never connects to the peripheral — connected.

  **That change is made, and proved on the bench** —
  [patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench).
  It copies what made it acceptable on the CANable: come up listen-only and prove the bit
  rate on real frames, only then start acknowledging, never send a frame, and go back to
  listen-only until the next boot if the error counters say the bus looks wrong. What it
  has not done is meet the R9. The Feather has no isolation and shares the bike's ground,
  which the CANable does not.

## Status

**This page is what is true now.** The history is in [firmware.md](firmware.md), one
section per patch; the values are in [numbers.md](numbers.md), one home per figure. When
those three disagree, the board is right and this page is the one to fix first.

The bench chain is joined and working end to end: frames cross the twisted pair in both
directions, both nodes ACK each other, and the Feather takes a saturated 500 kbps bus
without dropping a frame. Full results and method in [bench-link.md](bench-link.md).

| Part | State |
|---|---|
| Adafruit ESP32-S3 Reverse TFT (#5691) | working — flashed, running, display and buttons verified |
| KAmod CAN SN65HVD230 | working — wired to the Feather, and the CAN self-test reaches `CAN READY` through it |
| MKS CANable 2.0 Pro | working — reflashed to ElmüSoft Candlelight 2.5, acceptance test passes, see [gs-usb-stall.md](gs-usb-stall.md) |
| Bench link (twisted pair) | working — both directions, 10000 of 10000 at 4078/s, and 3.2 M frames over an hour with none lost, see [bench-link.md](bench-link.md) |
| Diagnostic pigtail | in hand since 2026-09-21, pins metered and marked, used for three captures with the CANable on 2026-10-11 |
| Toolchain | arduino-cli 1.5.2 + ESP32 core 3.3.11, installed and verified |
| USB into WSL | working — usbipd bind/attach done, `/dev/ttyACM0` live |
| Firmware — bench | two sketches: `bluecan_bringup` (self-test) and `bluecan_bench` (continuous sniffer, the one to run now) |
| Firmware — real | Bluecan: forked from upstream, patched, flashed, **receiving and streaming to a phone**: [DeziderMesko/racechrono-canbus](https://github.com/DeziderMesko/racechrono-canbus) |
| RaceChrono link | working — 8018 frames to an Android phone, 0 lost, with a channel tracking the bench sweep; clean to ~590 msg/s after [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |
| Status light | the NeoPixel shows the chain — hue for where it is, blips for frames moving, a latching red flash for a loss. Fault flash confirmed by eye, the flatline was invisible and is fixed, the rest unseen. [Patch 16](firmware.md#patch-16--the-neopixel-as-the-riders-status-light--applied) |
| Supply readout | the BUS page shows the BAT rail's voltage off the board's MAX17048; 4.09 V on the bench, no measurable cost. Not the outlet's 5 V, which nothing on this board can read, and state of charge is behind `CONFIG_CELL_FITTED` until there is a cell to test with. [Patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read) |
| Acknowledging | **done and measured on the bench** — listen-only at boot, acknowledging once 32 frames have proved the bit rate, back to listen-only if the bus looks wrong. 199–202 msg/s received with both nodes' error counters at 0 and no frame of its own on the wire. Not yet on the R9. [Patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench) |
| Decoded page | **done and measured on the bench** — LIVE, the page it boots into: gear, engine speed, grip, throttle valve, temperatures, TCS and the mode field, decoded from four frames kept ahead of the phone's filter, dashes for anything that stops arriving. Checked against a replay of the R9's own captures; not yet against the dash. [Patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench) |
| Per-id allow-list | **done and measured** — an unasked-for id is dropped in the CAN ISR, an allowed one leaves at 19.5 msg/s against the 50 ms the app asked for, and 635.8 msg/s on the bus becomes 19.6 over the radio with zero refusals. [Patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured) |

### What is proven

Everything from the source file to the CAN controller, end to end:

- compile → `.uf2` **and** serial upload from inside WSL2, no Windows-side tooling
- USB passthrough into WSL, including the ESP32-S3's mid-upload identity switch
- serial capture from WSL, plus a heartbeat so the board is observable at any time
  rather than only during `setup()`
- the TFT, the three buttons, and the Arduino task pinned to core 1
- the TWAI controller at 500 kbps, twice over: first with A0 and A1 jumpered, which
  isolates the MCU, and then through the KAmod. Both reach `CAN READY` and hold it, so
  the transceiver, its bus driver and its receiver all handle real 500 kbps signalling
  — not merely DC levels
- **the bus itself**: the Feather and the CANable joined over `CAN H`/`CAN L`, exchanging
  frames in both directions and ACKing each other. The receive path handles a saturated
  500 kbps bus — 10000 of 10000 frames at a peak of 4078/s, against the ~1050 msg/s
  [firmware.md](firmware.md) quotes for the R1-family bus
- **an hour of it without losing a frame**: 3224404 of 3224404 delivered at ~850/s
  across sixteen ids shaped like the R9's own traffic, with the display running, no
  reset, and free heap identical at the end to what it was at boot
- **listen-only mode**, the one change that must land before the device touches an R9:
  demonstrated on the bench by watching the CANable fall to ERROR-PASSIVE the moment the
  Feather stops ACKing. *The mode works as proven; it is the R9 that cannot use it* — see
  [what the R9 turned out to send](#what-the-r9-turned-out-to-send)
- **Bluecan itself**, now that it is flashed: 359 frames/s off the
  bus, every one of them forwarded to the RaceChrono path, controller in listen-only,
  BTR0/BTR1 reading back as 500 kbps exactly. That covers the ESP32 core 3.x risk the
  upstream project carried — joeroback's register-level ISR works on 3.3.11, once its bit-rate
  prescaler is derived rather than taken from a macro that stopped carrying one. See
  patch 4 in [firmware.md](firmware.md)
- **1261 frames of queue backlog absorbed** on a saturated burst with none dropped, after
  the frame queue went from upstream's 8 entries to 2000 — and the ISR now reports drops
  and peak depth instead of counting a dropped frame as a delivered one. See patch 5
- **that this builds against NimBLE, not Bluedroid.** Core 3.3.11 ships
  `CONFIG_NIMBLE_ENABLED` for the ESP32-S3, and the Arduino `BLEDevice` classes are a
  wrapper over whichever host is configured. Different callback signatures, a 0x2902
  descriptor that is a silent no-op, and a different failure path out of `notify()`.
  The same shape of surprise as patch 4, found the same way — by reaching for something
  and watching it not be there. See [firmware.md](firmware.md#nimble-not-bluedroid--found-while-instrumenting-the-ble-half)
- **the sniffer UI, in the real firmware**: three pages on the TFT — bus health, the ID
  census, the last raw frames, with a fourth, LIVE, in front of them since
  [patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench) — on their own task on core 0, repainting a character at a
  time. An idle pass costs ~420 µs and a busy one 2–6 ms, against 245 ms for the full
  panel, and the frame counters do not move when it runs. That puts dropped frames, queue
  peak and the discarded 29-bit/RTR classes in front of the rider in a release build,
  where until now they existed only in a `DEBUG` serial log. See patch 9

- **the whole chain, to a phone.** CANable → twisted pair → Feather → queue → BLE →
  RaceChrono Pro on Android → a channel on the screen tracking the bench's synthetic
  sweep from 0 to 1000 and back. **8018 frames, 0 lost, 0 errors**, one connected peer,
  subscribed, at a negotiated 15 ms connection interval. That was the last unproven link
  in the device, and it is no longer unproven. Full write-up, including the two app-side
  traps that cost the session most of its time, in
  [The phone session](firmware.md#the-phone-session--run-and-it-works)

So the hardware is done arguing, the sniffer UI is built, and the RaceChrono link works.

**And it carries most of the bus.** Clean to **~590 msg/s**, 0.6% refused at 718, against
the R9's ~1050 — where the link was clean only to ~200 before the drain task came off
`tskIDLE_PRIORITY` ([patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied))
and the frame path was pointed straight at the NimBLE host
([patch 13](firmware.md#patch-13--the-frame-path-goes-straight-at-the-host--applied)).
Neither moved the wall itself: every refusal is `BLE_HS_ENOMEM`, the host out of mbufs
because the radio cannot drain them at the interval the phone chose. The frame queue stops
dropping anything at any rate, and at a saturated bus the board still takes 3399 frames/s
off it and offers every one to the radio.

**With the allow-list on, the wall stops mattering.** 635.8 msg/s on the bus becomes
19.6 msg/s over the radio, zero dropped and zero refused
([patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured)).
The ceiling is now a number about traffic nobody asked for.

Every figure above, and every other measured number about this device, has its canonical
value in **[numbers.md](numbers.md)**. How each one was arrived at — the ramps, the
caveats, the two measurements that were worthless until a trap was found — is in the patch
it names. This page says what is true; those say how it got that way.

### Picking this back up

The board currently carries **Bluecan** — the real firmware — not
`bluecan_bench`. It is silent on serial unless built with `DEBUG` and only ever talks
over BLE, so a board that looks dead on the serial console is the normal case, not a
fault.

```bash
bluecan/firmware/tools/usb-attach.sh          # after a Windows reboot
sudo bluecan/firmware/tools/can-up.sh --one-shot   # can0, 500 kbps, no retransmits
cd bluecan/firmware
./tools/reflash-bluecan.sh --debug               # the real firmware, with 5 s stats
./tools/bench.py 10                           # read the serial console
./tools/sweep.sh --hz 10                      # a signal whose true value is known
sudo -v && ./tools/ble-load.py                # where the BLE link stops keeping up
```

`./tools/reflash.sh bluecan_bench` puts the bench sketch back whenever the two-node bench
is what is wanted; `./tools/reflash-bluecan.sh` (no flag) returns to the silent release
build. Both handle the USB re-attach dance.

The board is no longer silent to look at, whichever build it carries: it boots into LIVE,
the R9's own values decoded, and **D1** cycles LIVE, BUS, IDS and LAST, **D2** freezes the glass without pausing the counting, and
**D0** cycles panel + status pixel, pixel alone, dark. The middle position kills the
backlight and the repaint with it — the cheapest A/B on the board, since everything else
keeps running — and leaves the light that is actually readable from a moving bike on. In
a `--debug` build the display also reports its own repaint cost, stack and supply voltage
alongside the CAN and BLE stats.

Generating traffic to receive, and the one trap in it:

```bash
sudo bluecan/firmware/tools/can-up.sh --one-shot
timeout 8 cangen can0 -g 2 -I 123 -L 8 -D i
```

**Since [patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench)
Bluecan acknowledges the CANable** a fraction of a second into the first traffic after a
boot, so what follows describes only that first moment, a build without
`CONFIG_CANBUS_ACK`, or a board that has tripped back to listen-only. `--one-shot` is no
longer needed against it.

The CANable is the only transmitter on this bench, and until it switches Bluecan is
listen-only in hardware, so **nothing ACKs the CANable** — that is listen-only working,
not a fault.
This section used to say the traffic reaches ERROR-PASSIVE after ~1800 frames and then
stops, which reads as a quiet bus and is not what happens.
[bench-link.md](bench-link.md) has the mechanism: the CANable **retransmits the same
frame forever**, 2018 of them in one second, every one reaching the Feather. The bus is
flooded, not silent — with duplicates of one id.

What actually stalls is the supply of *distinct* frames: `cangen` wedges on the stopped TX
queue and no new id gets out, which makes a multi-id profile impossible. That is what
`one-shot on` fixes, and why it is in the line above — no retransmission, so each frame is
attempted once and the queue keeps moving. Over six seconds the CANable reports **2484
distinct frames transmitted instead of 6**. Use it whenever the board under test is running
Bluecan; `kill -9` on a wedged `cangen` is still needed, since `timeout` will not get it out.
On the bike this cannot arise, since the ECU, ABS and IMU nodes ACK each other.

One more thing that will otherwise waste an afternoon: **re-attach after every flash**
with `./tools/usb-attach.sh 1-3`, because a dead port looks exactly like a board
receiving nothing. What used to belong here as well — that the port dies after about ten
seconds of being held open, so keep reads short — is gone with
[patch 15](firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured):
the board was panicking and rebooting on the write, not losing the port, and a console
can now be held open indefinitely (600 s under `bluecan-soak.py`). Opening the port does not
reset the board, so the cumulative counters survive across sessions and before/after
readings still work.

`bluecan_bench` keeps the controller running continuously and takes single-character
commands over the same serial link that carries its log, so bench runs are scriptable
rather than performed by hand — see [bench-link.md](bench-link.md).

### What to look at next

**Nothing that used to be on this list.** Every bullet it carried has been answered and
has become a patch — `CAN_TX` never configured (6), 29-bit frames discarded uncounted (7),
the drop and peak counters existing only in `DEBUG` builds (9), the drain task's priority
(12), the `String` copies in `notify()` (13), and the `DEBUG` build that looked like it
stalled core 0 (15).

They are kept in [firmware.md](firmware.md#open-questions-before-the-bike) rather than
here, and they are worth reading past the headline: two of them came out the opposite way
round from the question. The console was not being lost, the board was rebooting on the
write; and the board that looked healthy was the one being killed.

What is genuinely still open is below.

### Next steps

**Only what is still open.** Everything finished has its record in the patch that closed
it and in [next-without-the-bike.md](../journal/next-without-the-bike.md), which is the level
above this list — the BLE link itself, a channel against a synthetic signal, the ramp, the
per-id allow-list and the supply readout are all done and measured.

1. **A soak with a phone connected.** 150 s at 476 msg/s is still the longest this link
   has held under load, against an hour for the CAN half on its own. It needs a phone and
   no supervision, and it is the only open thing that needs the phone at all. See
   [item 3](../journal/next-without-the-bike.md).

2. **The UI in daylight** — whether the pages are the right ones now that the device
   is a telemetry link rather than a sniffer (a fourth, LIVE, went in front of the three
   with [patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench)), and whether any of it is readable at a
   glance at speed. Only a bike can answer that. Half of it is answered without one: it is
   not readable, which is what the **NeoPixel status light** is for —
   [patch 16](firmware.md#patch-16--the-neopixel-as-the-riders-status-light--applied),
   written and flashed, costing 0 dropped frames and 30 µs an update. What is left of it
   is eyes: every acceptance row is a colour.

3. **The state of charge**, whenever a LiPo is fitted. The voltage is on the glass and
   costs nothing; the gauge cannot tell whether a cell is present, so the percentage and
   the charge/discharge marker sit behind `CONFIG_CELL_FITTED` until there is a battery to
   test them against — [patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read).

4. **Acknowledging on the R9's diagnostic pair.** The bench half is done —
   [patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench):
   the listen-only-then-ACK start, the trip back, and no frame of its own leaving the
   board. What is left is the bike: the BUS page reading `ACK`, the IDS page counting 23,
   REC at 0. The steps are in [at-the-bike.md](at-the-bike.md).

**Not being done:** the NimBLE mbuf pool. `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT` is baked
into the Arduino core's precompiled libraries, so raising it means building ESP-IDF from
source — a change of toolchain rather than of firmware — and the allow-list made it moot.
Also declined: link-quality/RSSI instrumentation. Both are in
[explicitly not doing](../journal/next-without-the-bike.md#explicitly-not-doing).

## Files

- [numbers.md](numbers.md) — **every measured figure, with one home each.** Quote a
  number from here or link to it; do not restate it in a third file. The pattern list at
  the end is what `check-numbers.sh` searches with.
- [hardware.md](hardware.md) — board choice and why, pin map, wiring, termination,
  power, bench test rig.
- [firmware.md](firmware.md) — which upstream project Bluecan was forked from and why, toolchain,
  the patches needed, listen-only safety, display layer, RaceChrono app setup, and the
  [code review log](../journal/bluecan-code-review-log.md) — what has been reviewed, and the range
  the next review starts from.
- [at-the-bike.md](at-the-bike.md) — **the checklist for the first time this device meets
  the R9**: what to check at home, plugging in, key on, engine running, and with the phone.
- [racechrono-channels.md](racechrono-channels.md) — the nine channels to enter into
  RaceChrono for the R9: PID in decimal and the equation, each one as the app accepted it.
- [wsl-setup.md](wsl-setup.md) — the Windows/WSL2 development environment: what is
  installed, and how flashing and serial monitoring work without USB passthrough.
- [bench-link.md](bench-link.md) — the two-node bench link: what was tested, the numbers,
  the 60%-frame-loss bug the display caused, and the listen-only proof.
- [gs-usb-stall.md](gs-usb-stall.md) — standalone write-up of the CANable stall: three
  frames per channel open and then silence, traced to the adapter's firmware and fixed
  by a reflash.
- [firmware/](firmware/) — sketches and build tooling.
  - [bluecan_bringup/](firmware/bluecan_bringup/) — toolchain/board/TWAI self-test.
  - [bluecan_bench/](firmware/bluecan_bench/) — continuous sniffer for two-node bench
    work: counts, ID census, error counters, listen-only toggle, serial commands.
  - [build.sh](firmware/build.sh) — compile and produce a `.uf2`, optionally flash it.
  - [tools/reflash.sh](firmware/tools/reflash.sh) — compile and upload, re-attaching
    around the board's mid-upload USB identity switch.
  - [tools/bench.py](firmware/tools/bench.py) — serial console that drives the bench
    sketch, including timed commands.
  - [tools/sweep.sh](firmware/tools/sweep.sh) — puts a triangle wave of known value on
    `can0`, in four byte orders and widths at once, so a RaceChrono channel can be
    validated before any real signal is.
  - [tools/ble-load.py](firmware/tools/ble-load.py) — ramps the bus rate against a
    connected phone and reports where the BLE link starts refusing frames. Reads the
    board's counters before and after each burst, which is what made it trustworthy back
    when a saturated board stopped printing them. `-g` takes fractional milliseconds, so
    the rates between 476 and 914 msg/s are reachable.
  - [tools/bluecan-soak.py](firmware/tools/bluecan-soak.py) — holds Bluecan's console open
    under load for an arbitrary length of time and says whether the board stayed up.
    `soak.sh` cannot do this: it drives `bluecan_bench` over serial commands Bluecan
    does not have. It infers restarts from the cumulative counters, because a panic on
    this board goes out of UART0 and is never seen, and because
    `Display draw: worst` looks continuous across one.
  - [tools/led-state.py](firmware/tools/led-state.py) — says in words what the status
    pixel is doing, on a `--debug` build, and can generate the traffic to move it. The
    rate is capped at 100 msg/s because the blip clamp pins at 5 Hz from 40 upward, so a
    faster bus tells you less about the light rather than more; `--rate 20` is one
    allowed id at RaceChrono's 50 ms and is the realistic thing to watch.
  - [tools/r9-decode-check.py](firmware/tools/r9-decode-check.py) — holds the decoder
    Bluecan uses for its panel (`src/canbus/r9.hpp`) to [r9.dbc](../analysis/r9.dbc): it
    compiles the firmware's header on the host and runs every distinct payload in the
    captures through both. Run it after touching either. Needs `analysis/.venv`.
  - [tools/serial-monitor.sh](firmware/tools/serial-monitor.sh) — serial console via
    Windows interop.
  - [tools/check-numbers.sh](firmware/tools/check-numbers.sh) — prints every place in
    both repositories that quotes a canonical figure. Run it after any patch that moves
    one: it is the difference between remembering the four files that need editing and
    reading them off the screen.

## Related root documents

- [riding-data-source.md](../analysis/background/riding-data-source.md) — what data Y-TRAC Rev exposes.
- [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md) — per-signal CAN mapping and confidence
  status. The mapping's "Priorities for whenever a logging session becomes possible"
  list is the test plan for those sessions — run with the CANable and a laptop, not with
  this device.
