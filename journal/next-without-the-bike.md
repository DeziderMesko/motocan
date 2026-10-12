# What can be done before the pigtail

> **This list is from before the bike.** The pigtail arrived on 2026-09-21 and the R9 was
> read on 2026-10-11 — three captures, in
> [r9-signals.md](../analysis/r9-signals.md). Two things below were overtaken
> by them and are left as written: the bus rate everything is sized against (~1050 msg/s,
> the R1's; the R9's connector carries 506 across 23 ids), and listen-only as the mode for
> the bike (the connector needs an ACK, for the CANable and for Bluecan alike — see
> [bluecan/README.md](../bluecan/README.md#what-the-r9-turned-out-to-send)). The bench facts
> are as good as they were.

Hardware logging on the R9 is blocked on one part — a diagnostic-connector pigtail — and
nothing below needs it. This is the working list, kept here rather than in a chat so it
survives a cleared context. Status as of 2026-08-23.

**Scope narrowed on 2026-08-23: preparing the logging session is the only work.** Asked
what was worth doing before the pigtail, the answer was to stop measuring the device and
start preparing for the bike. Two open items are therefore **stood down, not finished** —
[item 3](#3-a-longer-run-than-150-s--the-phone-half-is-what-is-owed), the soak with a
phone connected, and [item 8](#8-the-supply-voltage-on-the-panel--done-and-the-gauge-taught-it-a-lesson)'s
state-of-charge half. They are kept below exactly as they were, because what they cost
to work out is worth not rediscovering and because standing down is not the same as
deciding against: either can be picked up unchanged if it is ever wanted. Nothing has
been measured that unmade them.

The per-device next steps live in [bluecan/README.md](../bluecan/README.md#next-steps) and the
mapping test plan lives at the end of [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md); this file is
the level above both — what is worth doing next, and why.

**The repository has two halves as of 2026-08-23**, and this file spans them.
[analysis/](../analysis/README.md) holds everything about working out what the bytes mean —
the mapping, the sources, the method, the numbered workflow and the vendored CSS chain.
[bluecan/](../bluecan/README.md) holds the device that streams them to RaceChrono. They meet
at the CAN bus and nowhere else. `analysis/` is self-contained as of 2026-08-24 — it
links and shells out to nothing outside itself, so a session spent on the analysis reads
only that folder; what it deliberately does not borrow from the bench is named in
[analysis/README.md](../analysis/README.md#what-is-deliberately-not-here).

## What is actually left

Items 1, 2, 4 and 6 are done and are kept below because what they cost is worth not
rediscovering. Everything still open, in the order the constraints decide rather than the
order it is numbered:

| | Needs | State |
|---|---|---|
| **[5](#5-tooling-for-the-first-logging-session)** — logging-session tooling | **nothing** — no phone, no bike | capture, tagging and analysis all **done and exercised**; only the live gauge is left |
| **[7](#7-the-neopixel-status-light--flashed-and-two-rows-looked-at)** — the NeoPixel status light | **eyes** — the remaining rows are colours | flashed; fault flash confirmed, flatline was invisible and is fixed |
| **[3](#3-a-longer-run-than-150-s--the-phone-half-is-what-is-owed)** — soak with a phone connected | a connected phone, no supervision | **stood down 2026-08-23**, unfinished; not a decision against it |
| **[8](#8-the-supply-voltage-on-the-panel--done-and-the-gauge-taught-it-a-lesson)** — the supply voltage on the panel | a **LiPo**, for the half that is left | **stood down 2026-08-23**; the voltage half is flashed and reading either way |

Also closed by item 1: the NimBLE mbuf pool under
[explicitly not doing](#explicitly-not-doing) was parked on the grounds that the
allow-list would make it moot. It did.

## 1. The allow-list — **done, and it works**

Code is in Bluecan at `racechrono-canbus` `trunk` (`74af3d7`), written up as
[patch 14](../bluecan/firmware.md). `decoder_yamahar9` keeps a 32-slot allow-list filled from
RaceChrono's own requests and gates each id on the interval the app asked for.

All three owed runs were taken on 2026-08-22, against a recording RaceChrono at a
**15.00 ms** connection interval, and all three pass:

- **The filter.** `0x100`, which has no channel, at ~470 msg/s alongside `0x200` at
  ~190 msg/s. `Census ids: 4`, `CAN bus msg/s: 635.80`, `RaceChrono msg/s: 19.60` — the
  unasked-for id is dropped in the CAN ISR, and the panel reads `flt 3`.
- **The interval.** Never measured before at all. `0x200` in at ~190 msg/s, out at
  **19.4–19.6 msg/s** against the 50 ms the app asked for. Accurate to 3%.
- **The payoff.** 635.8 msg/s on the bus, 19.6 over the radio, 0 dropped, queue peak
  unmoved, **and refusals static at 51 for the whole run** — the 51 dating from the
  unfiltered phase before it. Zero refusals under the filter, which is the pass condition.

**One correction, because it was committed and pushed before it was checked.** An earlier
version of this item said RaceChrono never sends a per-id list and that the ceiling was
therefore self-inflicted. That came from reading a single connection. What the app sends
depends on what it is doing: a **test connection** asks for `ALLOW all` because it needs
every id to populate the channel editor's live-data row; a **recording session** sends
`DENY all` and then one `ALLOW ID` per channel. Patch 14's premise was right the whole
time. See
[what the app sends](../bluecan/firmware.md#what-the-app-sends-depends-on-the-session--measured-2026-08-22).

None of this was visible until `cfb313e`: the lines that log the protocol were
`verboseln` against a shipped level of `info`, so the test as written — *"serial must show
`ID request DENY all`"* — could never have passed.

The BLE ramp was also re-run on the fixed firmware: clean to ~583 msg/s, knee at 600–620,
`dropped` 0 and `q peak` unmoved, so the loss is BLE refusals and not the queue. The
ceiling is real and where it was said to be. There is no reason to re-run it again.

## 2. The `DEBUG` build — **done**, and it was worse than it looked

Both symptoms were one cause, and the cause is fixed:
[patch 15](../bluecan/firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured),
in Bluecan at `trunk` (`a89ebc4`).

The suspect named here was the right line. `logger::log()` wrapped `Serial.write()` and
`Serial.flush()` in `portENTER_CRITICAL`, and on this board `Serial` is USB CDC: the
write spins until the *host* reads, for up to 250 ms, with the USB interrupt that would
end the wait switched off. `CONFIG_ESP_INT_WDT_TIMEOUT_MS` is 300.

What this file got wrong is what happened next. **The board was not losing the port. It
was panicking and rebooting** — invisibly, because `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`
sends every Guru Meditation to UART0 on pins nothing is wired to, while `Serial` is
TinyUSB CDC. Which means the second sentence to withdraw is this one: *"Both symptoms
are `DEBUG`-only — the release build never writes to serial."* It does.
`bootln`/`errorln`/`warnln` compile in unconditionally; only `info`, `verbose` and
`debug` become no-ops. A laptop attached in the pits arms the same panic on the bike.

Measured, `cangen` at 919 msg/s, console held open by the same reader: **before, dead at
2.08 s and 4.58 s over two runs; after, no reboot, 0 dropped, queue peak 21 of 2000.**

**Open question 4 is answered too, and it was the wrong question.** The queue's
high-water mark over a minute with nothing reading serial is 21; holding the console
open for the next two minutes did not move it. The console costs nothing measurable. The
1307-vs-~50 asymmetry that raised the question was not a measurement of the console's
cost — the console-open board was resetting its own high-water mark every few seconds.

The one thing left from this is not a bug. The panel's first paint after boot really
does take ~251 ms — 417 characters, each drawn over its own background into an empty
change-detection cache — and it happens once, before there is any traffic to miss. Its
near-identical value to the 250 ms CDC timeout is a coincidence, and it cost time.

## 3. A longer run than 150 s — the phone half is what is owed

The CAN and console half is done. `soak.sh` drives **`bluecan_bench`**, not Bluecan — it
sends the sketch serial commands (`-c 'zVd'`) Bluecan does not have — so a Bluecan soak
needed its own harness, and now has one:
[`tools/bluecan-soak.py`](../bluecan/firmware/tools/bluecan-soak.py). It holds the console open for
as long as asked and infers restarts from the cumulative counters, since a panic here is
never seen and `Display draw: worst` looks continuous across one.

Measured with it, no phone, `cangen` at 919 msg/s: **600 s, 2032 lines, no reboot, 0
dropped, queue peak 21 of 2000, census monotonic to 699043.** A second run of 420 s
survived a phone disconnect and reconnect and a session-type change with 0 reboots.

**What is owed is a long run with a phone connected.** 150 s is still the longest either
patch 12 or 13 has held under load with one, against an hour for the CAN half alone. It
needs no new code and no supervision, and it is the only thing that finds a leak or a
watchdog reset twenty minutes in.

One thing changed about how it should be run, and it is the reason this is worth doing
properly rather than repeating an old ramp. Now that [item 1](#1-the-allow-list--done-and-it-works)
is measured, a **recording** RaceChrono means the radio carries ~20 msg/s per allowed id,
not the ~900 every previous soak pushed at it. For the R9 that is **nine ids and
~170 msg/s** — the whole Y-TRAC data set, counted in
[how many frames this actually is](../analysis/background/pre-bike-mapping.md#how-many-frames-this-actually-is) —
so three ids at 50 ms is a fair stand-in for the load, and the number to aim the soak at. That is the load the device will actually
see on the bike, and nothing has ever been soaked under it. Run it with the app recording
and the panel reading `flt 3`, not with a test connection.

## 4. Desk research — **done, and the walled forums were not where it was**

This item said the only way forward was a human pasting fz09.org and r7forums.com past
their bot walls. That was wrong, and the premise is withdrawn: the useful source was a
different *class* of source, and it took about twenty minutes to find.

**What was found**, written up as [S6] and [S7] in
[pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md#sources):

- **AiM's logger protocol for the FZ-09/MT-09**, as DBC — every signal with an ID, a byte
  offset, a scale and an AiM channel number. AiM ships that one protocol for MT-09/FZ-09
  2014–2020 *and* 2021+ Euro5, MT-07/FZ-07 2021+ and the YZF-R7 from 2022, which is the
  R9's platform across the Euro5 cut. Copies are in
  [analysis/sources/aim-yamaha/](../analysis/sources/aim-yamaha/).
- **The same vendor's protocol for the 2015+ R1**, which carries the IMU frames, the brake
  pressures and the rider-aid settings — the ones [S4] knew existed but never located an ID
  for.
- **AiM's own user guides**, which name the connector and pin functions for both eras. The
  2021+ Euro5 bikes use the standard red 6-pin under the seat, not the older 4-pin
  Sumitomo in the tail — which is the part to match when the pigtail is finally sourced.

**What it changed in the mapping.** Two 🟡 rows now have an independent vendor source
landing on the *identical* equation (both temperatures), TPS and APS gained 16-bit
positions that match [S4]'s hand reverse-engineering exactly, the accelerometer and gyro
rows finally have frame IDs, eleven 🔴 rows became 🟡 candidates, and the "which family is the
R9" question turned into a specific profile to confirm or break in one key-on capture.
Enough conflicts came with it — RPM one byte apart, three rival gear masks, `0x205` vs
`0x264` for wheel speed — that the first session now has a list of things to settle rather
than a blank sheet.

**What is left of this item: nothing worth scheduling.** The two walled threads are still
walled, and r7forums has got worse — it redirects machine traffic to a host that answers
HTTP 402, so it is not patience that gets past it. Their remaining value is also lower now
that the same platform is covered from the vendor side. The one thread still worth a
human's copy/paste is fz09.org's "Specs for DIY Euro5 OBD cable", and only for the
connector. Everything else here is answered better by the bike.

## 5. Tooling for the first logging session

The priorities (a)–(d) at the end of [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md) are a paragraph
of prose. On the day the pigtail arrives you want a script, not a reading exercise: capture
with markers ("now in 2nd gear"), then correlate offline.

**The method for it is written down** in [analysis-tips.md](../analysis/background/analysis-tips.md) — gathered
from two outside sources rather than invented here. Two things in it bear directly on what
gets built: a capture made only of discrete steps makes the correlation degenerate, so a
signal has to be swept to be identified and held to be calibrated; and the analysis half
may not need writing at all, since CSS Electronics' MIT-licensed skill reads a plain CSV
and only its capture scripts are tied to the vendor's hardware.

### The analysis half — **done, and proved rather than assumed**

That last claim was the load-bearing one: it is why no `correlate.py` of our own is being
written. It was marked *not verified* on 2026-08-22, and it is now verified end to end on
2026-08-23. Two tools were added, both in `analysis/tools/`:

- **[`candump-to-webcan.py`](../analysis/tools/3-candump-to-webcan.py)** — turns a
  `candump` log into the twelve-column semicolon CSV the CSS scripts read. It is as small
  as [analysis-tips.md](../analysis/background/analysis-tips.md) guessed, and the whole of the difficulty is in
  three columns rather than in the volume: `Dir` must be 0 or `load_trace()` silently
  returns nothing, an id's *field width* rather than its value is what says whether a
  frame is extended, and error frames arrive wearing `CAN_ERR_FLAG` and read as a phantom
  extended id whose payload is the controller's error state. They are dropped and counted.
- **[`analysis-selftest.sh`](../analysis/tools/1-selftest.sh)** — the proof,
  and it is re-runnable. It builds a **virtual** SocketCAN interface, so it needs no
  CANable, no Feather, no phone and no bike; puts a signal on it whose answer is chosen
  here rather than discovered; and runs the real path — `candump` → converter → `survey`
  → `correlate` → `bitsearch` → `build_dbc` → `calibrate` → `verify` — asserting eleven
  things about the result.

Measured, on the fixture described below: **correlate 1.0 against 0.93 for the runner-up,
bitsearch 1.000 against 0.106, build_dbc scale 1 offset 0 R² 1.0000, calibrate scale 1
offset 0 from the held anchors alone, verify PASS on both halves** — parked n=346 and
moving n=704, both Spearman 1.000. `sweep.sh` also grew
[`--sidecar`](../bluecan/firmware/tools/sweep.sh), which writes the reference series the
analysis correlates against; on the bike that column is a human or OCR of the dash, here
it is exact.

**Four things this found, none of which would have been visible from reading:**

1. **The vendor's own dependency note is wrong, and it stops every script.** Their
   `requirements.txt` says python-can and python-can-cansub are "not needed for the
   offline-log workflow". They are: `scripts/common.py` imports `python_can_cansub` at
   module level, so every offline script dies on import without it. Both install from
   PyPI with no hardware attached, so the cost is knowing — which on the day the pigtail
   arrives is the whole cost.
2. **`sweep.sh`'s frame is the wrong fixture for this, and it is not a fault in it.** It
   carries the same ramp four ways so a RaceChrono channel's byte order and width can be
   settled by eye in one pass, which is exactly what it was built for. Fed to correlate,
   the top **nine** candidates tie at 0.9998: bytes 0-1 BE, 2-3 LE, byte 4 and the
   inverted byte 5 are monotone images of each other, and no rank-based scorer separates a
   value from its own affine image. The right answer came out on top by tie-break order.
   The selftest therefore builds its own frame — ramp in one field, and decoys that move
   without tracking it (a rolling counter, a constant, an unrelated sawtooth, noise). The
   counter is the adversary [analysis-tips.md](../analysis/background/analysis-tips.md) names, and `survey.py`
   flags it and `correlate.py` skips it, which is now asserted rather than hoped.
3. **A symmetric triangle makes the reference lag unidentifiable.** The up and down halves
   shift in opposite directions, so the lag search finds a shallow optimum and reports
   noise. On the symmetric sweep correlate returned −0.4 s for a reference that is exact,
   and feeding that to build_dbc took R² from 0.9976 to 0.9494. The fixture's ramp is
   asymmetric — 60 steps up, 140 down — and the reported lag is then 0.0.
4. **The reference row must be written before its own frame, not after.** The analysis
   samples the reference with a forward sample-and-hold, so a row written afterwards pairs
   every frame with the *previous* step: a systematic offset of exactly one step, which
   here turned a fit that should read scale 1 offset 0 into offset −10. Both `sweep.sh`
   and the selftest now write first.

### The rider-facing half — **the session script is done**

[`logging-session.sh`](../analysis/tools/2-session.sh) is the whole session in
one command, which is the point: the alternative is remembering six of them with a helmet
on. It brings the interface up **listen-only**, proves the bus is alive before burning a
capture on it, starts `candump` on a PID it owns, and then shows a menu whose keys write
the sidecar. At the end it stops the capture cleanly, warns if the log ends before the
last mark, and converts.

**The menu is the list of open conflicts in
[pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md#mapping-table), not a list of vehicle signals.** The
first version was a generic gear/rpm/throttle menu invented for the occasion, and it was
the wrong shape: the session is not sniffing, it is answering eight specific
disagreements between sources, each of which one deliberate action settles. So each task
prints the id and the rival readings while you do it — *`0x236` byte 0, three rival masks*;
*`0x20A`, bytes 2–3 or 3–4, scale 0.391 or 0.435*; *only the rear wheel turning, so the
field that moves is the rear one*. Ordered by the priorities (a)–(d) at the end of the
mapping, with the rider aids called out because
[nine of the 27 Y-TRAC items are one frame](../analysis/background/pre-bike-mapping.md#how-many-frames-this-actually-is)
and cycling them is the best value of anything in the session.

What a human can produce is narrower than what the format allows. **Holds become `anchor`
rows**, and the prompt says *settle first, then press*, because `calibrate.py`'s window is
the quiet stretch behind the tag rather than ahead of it. **Sweeps get a bracketing
`event` pair**, and the prompt says *up quickly, down slowly*, which is
[what the symmetric-triangle finding cost](#5-tooling-for-the-first-logging-session)
stated as an instruction. Undoing half a bracket puts the other half back in the matching
state, since an unclosed bracket is a stretch of capture the analysis cannot bound. There
is deliberately **no `value` kind**: that is a densely sampled continuous reference and a
rider cannot produce one. The path that can is OCR of the dash, which needs a video
sharing a clock with the capture.

**The census records first and counts afterwards**, which is a correction of the same
kind. The first version probed for 3 s into a temp file, printed a count and deleted it —
throwing away the one result that needs no rider action at all, and under-answering the
question it was for: 3 s tells you the bus is alive, but a 1 Hz frame gives three samples
and an event-driven one gives none, so a rare id reads as absent. Now `candump` starts
first, 3 s is only the go/no-go, and the census is counted over the real capture across a
window that defaults to 30 s and takes `--census`. It then says outright how many of the
five predicted FZ09MT09 ids are present, and names the R1-style set to work instead if
they are not. `survey.py` can redo the whole thing offline over any window.

`x` is the key [analysis-tips.md](../analysis/background/analysis-tips.md) argues for — mark a botched cycle and
repeat it rather than letting it quietly poison the log. It drops the last mark from the
sidecar and leaves a `void` row in its place: no CSS script reads that kind, so it is
inert to the analysis and still legible afterwards. The mark counter goes back down, which
is the feedback that tells you it worked.

Exercised end to end against a virtual bus with scripted keystrokes: the census finds 5
of 5 planted ids and says so, anchors and sweep brackets land in the sidecar, `x` removes
the mark and reopens the bracket it half-undid, and an open sweep refuses to let either a
task or the session end.

**What is left: the live confirmation gauge** — [T1]'s most transferable habit, and the
natural place to show rival candidates side by side while blipping the throttle. The three
candidate gear masks are the case it exists for. Needs neither phone nor Bluecan,
and unlike everything above it is worth writing only once there is a real capture to point
it at.

Bluecan's Rust `candump-parse/` is no longer the starting point it was named as: the CSS
chain does what it was going to be grown into, and better.

## 6. `can-up.sh` one-shot — **done**

[`can-up.sh`](../bluecan/firmware/tools/can-up.sh) takes `--one-shot`, so the line that used
to be typed by hand before every Bluecan bench run is now:

```bash
sudo ./tools/can-up.sh --one-shot
```

It is a flag and not a fourth mode: one-shot is independent of loopback and listen-only in
SocketCAN, so it composes with `--loopback`, `--listen-only` and `--normal` rather than
replacing them. Like every other control mode it is now spelled out in full on every
bring-up, which means a run that does not ask for it turns it **off**. That is deliberate,
and it is the trap named here read the other way round: one-shot used to survive any
bounce that did not name it, so the interface's state depended on what had been run before
it. Now it does not.

The verification block at the bottom checks all three flags in both directions — it warns
for a mode that was asked for and did not come back, and for one that came back without
being asked. It reads the control-mode group off the `can <...>` line, which is absent
entirely in plain normal mode.

Exercised against the attached CANable on 2026-08-22: plain bring-up clears a sticky
`ONE-SHOT`; `--one-shot` reports `can <ONE-SHOT>`; `--listen-only --one-shot` reports
`can <LISTEN-ONLY,ONE-SHOT>`; `--loopback` clears both; an unknown flag still exits 2; and
a loopback `cansend` still round-trips afterwards, each frame twice as this bench always
shows it.

## 7. The NeoPixel status light — **flashed, and two rows looked at**

In Bluecan at `trunk`, written up as
[patch 16](../bluecan/firmware.md#patch-16--the-neopixel-as-the-riders-status-light--applied).

It shows the **chain**, never vehicle data: hue for where the chain is (booting /
advertising / configuring / recording), a blip rate for frames actually moving, a
flatline when they stop, and a latching red flash overlaid when something is lost. D0
grew a third position — TFT + pixel, pixel only, dark — so the panel can be off while the
light stays on.

Flashed and run at ~919 msg/s with a phone subscribed: 0 dropped, 0 reboots, queue peak
17, repaint cost unchanged.

**Two rows have been looked at.** The fault flash works. The idle state read as a pixel
that was simply off — and it was the flatline rung, one 60 ms pulse at a quarter of
24/255, which is the worst rung to lose because it is what a connector falling off the
bike looks like. Two 90 ms flashes at full level now. The light also reports its own rung
on the console, and [`tools/led-state.py`](../bluecan/firmware/tools/led-state.py) reads it
while you watch the pixel:

```bash
./tools/led-state.py --rate 20
```

The rest is still eyes — a colour cannot be passed from a serial console.

Running it also corrected the design. Latching the fault on `lost` at all would have lit
it permanently for anyone who had ever configured a channel — an allow-all is not
rate-limited, so a test connection left 15001 refusals behind in ninety seconds, and they
are expected rather than wrong. It now latches on growth, and only while the allow-list is
in force.

## 8. The supply voltage on the panel — **done, and the gauge taught it a lesson**

In Bluecan at `trunk`, written up as
[patch 17](../bluecan/firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read).
The board has had a **MAX17048 fuel gauge** at I2C `0x36` the whole time and nothing was
reading it. The BUS page's top row now ends with the BAT rail's voltage:

```
CAN 500k LISTEN-ONLY RUN            4.09V
```

Flashed and read on the bench: 4.09 V, stable, tracking, no hibernate stall, idle repaint
445 µs against 421 µs for the same firmware without it, display stack unchanged at 1912
bytes free. Green above 3.70 V, yellow below, red below 3.50, `bat n/a` in grey if the
chip stops answering.

**Two things it cannot do, and the second one cost a design.** The 5 V from the bike's
outlet is not readable by anything on this board — VBUS goes to a pad and to no ADC. And
the gauge cannot tell whether a cell is fitted: with the jack empty it reports a
confident state of charge and discharge rate for the charger's own rail. On the bench,
98.6% and −26.6%/hr for a rail that had not moved 3 mV. The first version of this showed
that rate's sign as "is the bike still feeding the device", which would have read
"running on battery" while the board sat on USB.

Two runtime tests for a missing cell were tried and the board killed both — over-100%
charge (only true for the first few seconds after power-up) and a jittering voltage (the
rail settling, not a property of an empty jack). So there is no guess: the percentage and
the marker sit behind `CONFIG_CELL_FITTED`, off by default, and the voltage — the part
that is true in every configuration, and the part that was asked for — is shown always.

What is left needs **a LiPo in the jack**, and nothing else: build with
`CONFIG_CELL_FITTED`, confirm the percentage agrees with the cell and the marker reads
`+`, then pull USB and confirm it flips to `-` with a voltage that genuinely falls.

## Explicitly not doing

- **The NimBLE mbuf pool.** Every BLE refusal is `BLE_HS_ENOMEM` and
  `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT` is baked into the Arduino core's precompiled
  libraries. Raising it means building ESP-IDF from source — a change of toolchain, not of
  firmware — and item 1 makes it moot.
- **Link-quality / RSSI instrumentation.** Declined; out of scope.

## Bench facts that cost time to rediscover

- **Use `--one-shot` against Bluecan**: `sudo ./tools/can-up.sh --one-shot`, a flag on
  the script since [item 6](#6-can-upsh-one-shot--done) rather than a line to type. The
  Feather is listen-only in hardware, nothing ACKs the CANable, and the default retransmits
  one frame forever: the bus floods with duplicates while `cangen` wedges and no new id
  gets out.
- **`soak.sh` and `bench.py -c` are for `bluecan_bench`**, which ACKs. That is why the
  3.2 M-frame hour worked and a Bluecan run does not behave the same way.
- **A measured number has one home**, [bluecan/numbers.md](../bluecan/numbers.md), and the
  copies are found rather than remembered: `bluecan/firmware/tools/check-numbers.sh`
  prints every place in both repositories that quotes a canonical figure. Run it after any
  patch that moves one. This exists because six documents were found disagreeing with each
  other about the same board on 2026-08-22 — the same figures had been written out in four
  files each, and the edit reached two of them.
- **Capture with `candump -L iface > file`, not `candump -l`.** With the redirect ours the
  PID is ours too, and stopping the capture is one signal to one known process. `-l` names
  its own file, which invites wrapping it in a subshell to place that file — and killing
  the subshell never reaches candump, its child. It kept capturing while the log was being
  read, and the read got a file ending **mid-frame**, 35 frames and 1.7 s short. Nothing
  failed and nothing warned: the analysis just ran on a capture whose tail was missing.
  The only symptom was one calibration anchor fitting 6 samples where its neighbours fitted
  41. At the bike that is the end of a run vanishing silently, and the end of a run is
  where the interesting thing usually is. The selftest now asserts the capture outlives the
  excitation, which is the check that would have caught it.
- **A code review leaves no trace in either repository.** Findings are reported in the
  session and the fixes land as an ordinary commit, so "review everything since the last
  review" has nothing to read unless it is written down. It now is, in the
  [code review log](bluecan-code-review-log.md): what was reviewed, what came
  out of it, and the range the next one starts from.
- **Re-attach after every flash** (`./tools/usb-attach.sh 1-3`). The ten-second limit on
  reads that used to belong here is gone with
  [patch 15](../bluecan/firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured):
  a console can be held open indefinitely now. Opening the port does not reset the board,
  so cumulative counters survive across reads — but note that a *reboot* leaves
  `Display draw: worst` at ~251 ms either way, so that field cannot be used to tell one
  from the other. `Census ids: N, M over` can: under traffic it only ever grows.
- **Reboot the board before any bench run that follows a phone session.** The allow-list
  survives a disconnect: `onDisconnect()` clears the connection state and deliberately
  leaves the decoder filtered, so after a recording session the board stays on the app's
  three ids until it is power-cycled or reflashed. Boot state is forward-everything only
  until the *first* connection of a power cycle. Nothing on the console says which state
  the board is in — the panel's `flt ALL` / `flt 3` field is the only instrument that
  does — so a census or a `ble-load.py` run taken afterwards measures a filtered bus and
  reads as a quiet one. Why it is left that way, and what it would cost to change, is in
  [patch 14](../bluecan/firmware.md#three-things-deliberately-not-done).
