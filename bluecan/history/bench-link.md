# The bench link: two nodes, one bus

> Moved here from `bluecan/bench-link.md` on 2026-10-14, unchanged but for its links. How
> to run the bench today is [../bench.md](../bench.md).

The first tests in which a CAN frame left one device and arrived at another. Everything
before this was a single node talking to itself — [bluecan_bringup](../firmware/bluecan_bringup/)
reaches `CAN READY` by asking the controller to receive its own transmission, which
proves the MCU, the toolchain and the KAmod's own driver-to-receiver path, and proves
nothing whatsoever about `CAN H` / `CAN L`.

Rig: the Feather's KAmod and the CANable 2.0 Pro joined by a twisted pair, CAN-H to
CAN-H and CAN-L to CAN-L, 120 Ω at each end — the KAmod's `120R` jumper and the
CANable's own terminator. See [hardware.md](hardware-2026-10-13.md).

## Why ACK is the measurement that matters

Frames arriving is the obvious thing to check and the weaker of the two signals. The
stronger one is the transmitter's error counter, because a CAN transmitter needs a
*second node* to pull the ACK slot dominant, and nothing it does by itself can fake
that. So:

- if the pair is wired, terminated and both ends agree on 500 kbps, every frame is
  ACKed and both nodes sit at TEC 0, ERROR-ACTIVE, indefinitely
- if anything about that is wrong, the transmitter climbs to TEC 128 and drops to
  ERROR-PASSIVE within a few milliseconds — one `cansend` is enough to see it

That makes a single `ip -details link show can0` a complete verdict on the physical
layer, which is why it appears in nearly every test below.

The baseline was taken deliberately, before the Feather was listening, to confirm the
failing case looks like what the theory says:

```
$ cansend can0 123#DEADBEEF          # nobody on the other end
can state ERROR-PASSIVE (berr-counter tx 128 rx 0)
```

## Results

Feather running [bluecan_bench](../firmware/bluecan_bench/), CANable at 500 kbps in normal
mode. Frame counts are the Feather's own, read off its serial status line.

| # | Test | Result |
|---|---|---|
| 0 | Baseline: transmit with no partner | can0 → ERROR-PASSIVE, TEC 128 — the failure signature, as expected |
| 1 | Feather → CANable, 1 Hz heartbeat | `candump` shows `100#000000xxBEEF0001`, sequence unbroken, TEC 0 both ends |
| 2 | CANable → Feather, 4 ids + 50 frames | 54 of 54 received, payloads byte-exact including 4- and 7-byte DLCs |
| 3 | 2000 frames at ~940/s | **854 received, 1200 dropped** — see below |
| 4 | Same, after the display fix | 2000 of 2000, drop 0 |
| 5 | Listen-only mode | Feather stops ACKing; can0 → ERROR-PASSIVE. Restored on switching back |
| 6 | 10000 frames, bus saturated | 10000 of 10000, drop 0, peak **4078 frames/s** |
| 7 | Both directions at once | 1000 of 1000 in, 20 of 20 bursts + 15 heartbeats out, TEC 0 throughout |
| 8 | 65 min at ~850/s, R9-shaped traffic | **3224404 of 3224404**, drop 0, no reset, heap flat — see [below](#the-soak) |

4078 frames/s is a saturated 500 kbps bus — around 4200 is the arithmetic ceiling for
8-byte frames once stuffing and interframe space are counted. [firmware.md](firmware.md)
quotes ~1050 msg/s for the R1-family bus, so the receive path has roughly 4× the
headroom the R9 will ask of it.

## What test 3 found, and why it is the useful result

At ~940 frames/s the Feather delivered 854 of 2000 and dropped 1200, while the
controller's error counters stayed flat at zero and the CANable logged no bus errors at
all. Nothing was wrong on the wire. The frames arrived, sat in the driver's RX queue,
and were overwritten before anything read them.

The instrumentation added to find out why answered it in one line:

```
up 33s | NORMAL RUNNING | rx 0 0/s | ... | loop 198001 us draw 197761 us
```

A single TFT repaint took **198 ms**. The RX queue is 64 frames deep, which at bus rate
is about 64 ms of slack, so every repaint threw away more than the queue could hold.
Confirmed by elimination rather than assumed: with the display switched off and nothing
else changed, the identical run delivered 2000 of 2000 with a worst-case loop of 450 µs.

[firmware.md](firmware.md) already stated the rule — *the TFT must never be updated from
the CAN path* — as a design constraint. This is the measurement behind it, and it is
worth having, because the failure is silent. Every counter that a person would think to
check reads healthy while the data quietly goes missing.

The fix was both halves of the obvious one:

- **Move it off the CAN core.** `draw_live()` runs in its own task pinned to core 0 at
  a priority below the Arduino loop, exactly as the upstream firmware splits BLE from
  CAN.
- **Make it cheaper anyway.** Adafruit_GFX draws text a character at a time over SPI,
  and the panel is ~250 characters, almost all of them unchanged between repaints. Each
  line now compares against what is already on the glass and skips itself if it matches.
  An idle repaint fell from 198 ms to ~160 µs; a repaint with counters moving costs
  20–78 ms.

Relocating alone would have been enough for CAN. Cutting the cost matters for what
comes next: core 0 is where the BLE stack goes, and a task that blocks it for 198 ms at
a stretch would just move the same problem onto the link to RaceChrono.

**Follow-on, from porting this into the real firmware** (patch 9 in
[firmware.md](firmware.md)): line-level detection stops being enough as soon as the panel
reports its own repaint cost, because that line changes on every pass — it was paying
24 ms to say it had paid 24 ms. Diffing *characters* instead of lines costs the same
comparison and redraws three digits instead of forty columns: an idle pass fell to 280 µs
and a busy one to 2–6 ms. **The 280 µs is stale** — it was measured on a smaller page, and
the panel as it stands costs ~420 µs an idle pass ([measured
2026-08-22](firmware.md#cost-measured-on-the-board)). The shape of the result is the
point, and it did not change. The per-character price is ~650 µs, which is the number that
explains all of the above — 250 characters is 198 ms, and there is no way to draw them
faster, only fewer of them.

## The soak

Everything above is a burst of a few seconds, which cannot tell *works* from *works
until it doesn't*. The failure modes that only appear over time — a heap leak, a
watchdog reset, a receive queue slowly falling behind, the gs_usb transmit path wedging
the way it [used to](gs-usb-stall.md) — are all invisible in a frame count taken after
two seconds.

[tools/soak.sh](../firmware/tools/soak.sh) runs the long version. The traffic profile
deliberately imitates the R9 instead of maximising load: sixteen ids at the rates
[pre-bike-mapping.md](../../analysis/background/pre-bike-mapping.md)'s [S4] gives — engine speed, gear, TPS/APS,
brake pressure and wheel speeds at 100 Hz, temperatures at 1 Hz — for ~850 frames/s in
total, which is what [firmware.md](firmware.md) expects of the real bus. A *saturated*
bus was already shown to survive a burst (test 6). A *realistic* bus over a long run was
the untested case, and the one the device will actually meet.

One hour, with the display running and per-frame serial logging off:

| | |
|---|---|
| Frames delivered | **3224404 of 3224404** — zero lost |
| Dropped by the driver | 0 |
| Bus error counters | TEC 0, REC 0, transmit failures 0, both ends ERROR-ACTIVE throughout |
| Resets | none; uptime monotonic across the whole run |
| Free heap | 299440 at boot → 299440 at the end, low-water mark identical |
| Display task stack | 2208 bytes still free at its worst |
| Worst `loop()` pass | 894 µs, against 75.6 ms of RX queue at that rate — 1.2% of the budget |

The heap number is the one worth dwelling on. Nothing in this firmware allocates on
purpose, but the Arduino `USBCDC` and the TWAI driver both do, and "not on purpose" is
not the same as "not at all". Three million frames later it has not moved by a single
byte.

### The harness cried wolf, and how it was caught

The first hour-long run reported **164268 frames missing** one way and 191 unaccounted
for the other. Both were artefacts, and the way they were dismissed is worth recording
because the same trap will recur.

The logger was given the run's duration, but the progress loop added up sixty
`sleep 60`s and drifted 3.4 minutes past it. So the logger exited while the load was
still running, and the last frame count in the log was 189 seconds older than the kernel
counter it was being compared against.

Two things gave it away before anything was changed:

- the numbers corroborate each other. 189 s at 847 frames/s is the 164268; the heartbeat
  is 1 Hz, so 191 extra frames arriving at the CANable is the same 191 seconds. A real
  loss would not produce two independent derivations of one gap.
- a "received more than sent" result is almost never duplication. It means the *sent*
  figure in the comparison is the stale one — the same shape of mistake as forgetting
  that can0's RX counter includes the local echo of its own transmissions.

Asked directly once the run was over, the board reported `rx 3224404` against the
CANable's 3224404 transmitted. Not one frame lost.

Fixed by scheduling the progress loop against an absolute deadline, giving the logger
five minutes of margin and stopping it explicitly *after* the counters are read, with
the bus quiet in between so nothing is in flight. [soak-report.py](../firmware/tools/soak-report.py)
now checks that the log spans the run before believing any frame count, and marks the
delivery checks `SKIP` rather than `FAIL` when it does not. A harness that cries wolf
over a clean result is worse than no harness.

## Listen-only, proven before the bike

[firmware.md](firmware.md)'s patch 2 is the one change that must land before this device
is connected to an R9: `TWAI_MODE_LISTEN_ONLY` makes the controller electrically
passive, so a wrong bit rate or a wiring mistake cannot put error frames onto a bus
shared with the ECU, ABS and IMU. The bench can demonstrate the property directly,
which means the bike never has to be the experiment:

| Feather mode | can0 after transmitting | reading |
|---|---|---|
| NORMAL | ERROR-ACTIVE, TEC 0 | the Feather is ACKing |
| LISTEN-ONLY | ERROR-PASSIVE, TEC 128 | the Feather is not ACKing at all |
| back to NORMAL | ERROR-ACTIVE, TEC 0 | and it recovers |

One consequence is bench-specific and worth not being surprised by. With the Feather in
listen-only there is nobody left to ACK, so the CANable retransmits automatically and
forever — the Feather's receive count jumped by **2018 frames in one second** after
three `cansend` calls. Those are retries of the same frame. On the R9 this does not
arise, because the bike's own nodes ACK each other and the reader is simply one more
silent listener; it only happens on a two-node bench where the listener is the only
possible ACK source. It did make an accidental stress test: 2018 frames/s, zero drops.

## Running it again

```bash
bluecan/firmware/tools/usb-attach.sh                   # after a Windows reboot
sudo bluecan/firmware/tools/can-up.sh                  # can0, 500 kbps, normal
cd bluecan/firmware
./tools/reflash.sh bluecan_bench
./tools/bench.py 10                                    # stream the status line
```

The sketch takes single-character commands on the same serial link that carries its
log, so a run is scriptable end to end instead of needing someone to press buttons:

| key | does |
|---|---|
| `v` / `V` | per-frame logging on / off |
| `d` / `D` | TFT repaints on / off |
| `h` / `H` | 1 Hz heartbeat frame on / off |
| `m` / `M` | NORMAL / LISTEN-ONLY |
| `b` | burst of 10 × `236#A0A1A2A3A4A5A6A7` |
| `z` | zero the counters |

The commands **set** rather than toggle — lower case on, upper case off. The buttons
toggle, which is right for a finger on a board you can see and wrong for a script,
which has no idea what state it inherited. That distinction is not pedantry: a soak run
that toggled per-frame logging *on* when it meant to turn it off measured a 25 ms
worst-case `loop()` instead of 700 µs, and nothing in the log said the two runs
differed.

```bash
./tools/bench.py 20 -c z --at 4:b --at 10:M    # zero, burst at 4 s, listen-only at 10 s
```

The buttons still do the same three things (D0 mode, D1 burst, D2 logging) for when the
board is not on a USB cable.

## Two traps this cost time on

**The Feather prints into the void unless DTR is asserted.** The ESP32-S3 Arduino core's
`USBCDC` gates output on the host's DTR line. With DTR low the sketch runs perfectly and
emits nothing, which is indistinguishable from a crashed sketch. Asserting DTR is safe
on *this* board specifically — USB is native to the SoC, there is no USB-serial bridge
whose modem lines are wired to EN, so unlike almost every other ESP32 board the host
cannot reset it that way. [tools/bench.py](../firmware/tools/bench.py) does it and says why.

**`cangen -g 0` reports `write: No buffer space available` after about twenty frames.**
That is SocketCAN's default `txqueuelen` of 10, not the adapter stalling — an easy
thing to misread as a return of the [gs_usb stall](gs-usb-stall.md).
[tools/can-up.sh](../firmware/tools/can-up.sh) now raises it to 1000.

## Still not proven

- **Anything at all about the R9.** The bench partner sends what it is told to. The ID
  census in the display is real but its contents are synthetic.
- **BLE.** No RaceChrono code exists yet in *this sketch*; core 0 has only the display on
  it here, so nothing on this page measures the radio. Bluecan is a different
  matter: it runs the BLE stack, the frame drain and the display task together on core 0,
  and a phone has connected, subscribed and recorded a session against it — see
  [the phone session](firmware.md#the-phone-session--run-and-it-works) and the ramp that
  followed it. What is still unproven here is this sketch, not the radio.
- **Thermal behaviour.** The hour-long soak ran at room temperature on a bare board on
  a desk. Nothing here measures die temperature, and a black box in the sun on a bike is
  a different problem.
- **Load beyond an hour**, and load while BLE is also running — which is the
  configuration that will actually ride.
- **Anything about isolation or 12 V power.** Bench power is USB.
