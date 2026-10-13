# Firmware

**This file is the history.** One section per patch: what was wrong, what changed, what it
measured, and what the measurement was worth. It is meant to be read backwards from a
question, not forwards.

Two files sit beside it and are not this one. [README.md](README.md) is what is true about
the device *now*; [numbers.md](numbers.md) holds the current value of every measured
figure, one home each, so that a number which moves is edited in one place and the copies
are found with [check-numbers.sh](firmware/tools/check-numbers.sh) rather than remembered.
A patch here keeps the figure it measured — that is the record — but when a later patch
supersedes it, say so in the older section instead of leaving two live claims.

## Choosing a base

Three RaceChrono DIY projects exist. They differ mostly in which microcontroller and
CAN hardware they assume.

| Project | Hardware | Fit here |
|---|---|---|
| [joeroback/racechrono-canbus](https://github.com/joeroback/racechrono-canbus) | ESP32 / ESP32-S3 Feather, built-in TWAI + bare transceiver | **base to fork** — the only one written for an ESP32 Feather and a bare transceiver |
| [aollin/racechrono-ble-diy-device](https://github.com/aollin/racechrono-ble-diy-device) | nRF52832 + MCP2515 | wrong hardware, but its README is the **authoritative spec** for the BLE service (UUID `0x1FF8`) and packet formats |
| [timurrrr/RaceChronoDiyBleDevice](https://github.com/timurrrr/RaceChronoDiyBleDevice) | nRF52840 + MCP2515 (ESP32 "experimental") | wrong hardware, but worth mining for tooling — see [PidHistogram](https://github.com/timurrrr/arduino-CAN/tree/master/examples/PidHistogram) |

Plan: fork joeroback, keep aollin's README open as the protocol reference, borrow
timurrrr's discovery tooling.

**Bluecan exists**: [DeziderMesko/racechrono-canbus](https://github.com/DeziderMesko/racechrono-canbus),
branch `trunk`, checked out beside this repository rather than inside it, since it is a
separate repository with its own upstream. Patches 1–13 below are applied there, one
commit each. It compiles on ESP32 core 3.3.11, and the CAN half of it is proven on the
bench: 359 frames/s received, all of them forwarded, with the controller in listen-only.
The BLE half is proven too, as of the session written up under
[The phone session](#the-phone-session--run-and-it-works): frames reach RaceChrono, and the link's
throughput ceiling has been measured — and then raised, in
[patch 12](#patch-12--the-drain-tasks-priority--applied).

What joeroback's firmware actually does: a CAN ISR on core 1 pushes frames into a
FreeRTOS queue; a task on core 0 pops them and writes `id + data` straight out over
BLE. That raw-forward path is exactly what's wanted here. The BMW-specific decoder in
the tree is a separate, optional feature.

## Toolchain

`arduino-cli` with the ESP32 Arduino core. Board:
**Adafruit Feather ESP32-S3 Reverse TFT**, FQBN
`esp32:esp32:adafruit_feather_esp32s3_reversetft`.

Already installed and verified — see [wsl-setup.md](wsl-setup.md) for what is where,
how flashing works from WSL2 without USB passthrough, and how to read the serial
console.

Required Tools settings:

| Setting | Value | Why |
|---|---|---|
| Partition Scheme | `tinyuf2_noota` | 2816 KB app, and it **preserves the UF2 bootloader** the WSL flashing workflow depends on. Not `huge_app` — that overwrites it. See [wsl-setup.md](wsl-setup.md) |
| Arduino Runs On | Core 1 | firmware asserts `xPortGetCoreID() == 1` in `setup()` |
| Events Run On | Core 1 | keeps core 0 free for BLE |
| USB CDC On Boot | Enabled | serial console for sniffing output |
| PSRAM | Enabled | 2 MB available on this board |

## Patch 1 — board detection — **applied**

`src/racechrono-canbus.hpp` has `#if` blocks for the Huzzah32, the plain S3 Feather and
a generic dev module. None matches this board, so `CAN_RX_PIN` / `CAN_TX_PIN` /
`BLE_PWR_LVL` end up undefined and the build fails. The board macro is
`ARDUINO_ADAFRUIT_FEATHER_ESP32S3_REVTFT` (from `build.board` in the core's
`boards.txt`). The pin numbers are the same as the plain S3 Feather block:

```c
// Adafruit Feather ESP32-S3 Reverse TFT
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_REVTFT)
#define CAN_RX_PIN GPIO_NUM_18   // A0
#define CAN_TX_PIN GPIO_NUM_17   // A1
#define BLE_PWR_LVL ESP_PWR_LVL_P12
#endif
```

## Patch 2 — listen-only mode — **applied**

**The most important change, and it must land before the device ever touches the
bike.** A controller in normal mode participates in the bus: it ACKs frames it receives
and transmits error frames when it disagrees with what it sees. A misconfigured bit rate
or a wiring mistake then doesn't just fail to read — it actively disrupts the R9's
network, with the ECU, ABS and IMU nodes all sharing that bus.

What upstream actually does turned out to be better than assumed here, and worse than it
looks. It *asks* for listen-only already — and it never connects the TX pin to the
peripheral at all, which is a second, physical layer of the same guarantee (the state of
the pad itself is a separate matter — see patch 6). But it asks
through `twai_ll_set_mode(dev, TWAI_MODE_LISTEN_ONLY)`, the two-argument form from core
2.x. Core 3.x replaced that with three booleans, so on the installed core the call does
not compile and the intent was never in force. The patch is therefore small and
uninteresting to read, and load-bearing anyway:

```c
twai_ll_set_mode(dev, true /* listen_only */, false /* no_ack */, false /* loopback */);
```

Listen-only makes the controller electrically passive: it never drives the bus, never
ACKs, never sends error frames. For pure reverse engineering and pure telemetry there is
no downside, since the device never needs to transmit.

This lives in the controller setup in `src/canbus/controller.cpp`.

**The behaviour is already validated on the bench**, independently of any of the
upstream code: [bluecan_bench](firmware/bluecan_bench/) toggles the mode at runtime, and
the CANable falls to ERROR-PASSIVE the instant the Feather stops ACKing, then recovers
when it starts again. See [bench-link.md](bench-link.md). So this patch is a port of a
known-good behaviour rather than a change to be tested on the bike.

**What the bike said about this patch, 2026-10-11.** The premise above — "the ECU, ABS and
IMU nodes all sharing that bus" — is not the R9's diagnostic connector. That connector is
a separate pair with only the ECU on it, and "no downside" turned out to have one: with
nobody acknowledging, the ECU resends a single frame about 3 800 times a second and two
ids of 23 arrive. The patch does what it says and stays as the safe default; for the R9 it
has to be followed by its opposite, a deliberate normal mode with the TX pin connected,
entered only after the bit rate is proved in listen-only. That is
[patch 18](#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench). The reasoning
and the CANable's version of the same step are in
[the README](README.md#what-the-r9-turned-out-to-send). The "~1050 msg/s" quoted for the
R9 throughout the patches below is the R1-family estimate; measured, the R9's connector
carries 506.

## Patch 3 — decoder and filtering — **applied**

- Disable `CONFIG_CANBUS_DECODER_BMWG8X` in `src/racechrono-canbus.hpp`. It decodes
  BMW G8x signals for RaceChrono's "monitor" feature; on the R9 it would report
  nonsense.
- The raw forwarding path is **not** independent of it, which is the one thing this
  section got wrong. `CANDEC` — the decoder — is what the define selects, and the ISR
  asks it `should_decode(id)` before queueing anything. The base class answers from a
  fixed, sorted ID table, so a build with the BMW decoder off has no `CANDEC` to link
  against, and a build with it on drops every frame whose ID is not a G8x one — on the
  R9, all of them. So the define cannot simply be switched off; something has to take
  its place.
- `src/canbus/decoder_yamahar9.cpp`, selected by `CONFIG_CANBUS_DECODER_YAMAHAR9`, is
  that replacement: 500 kbps timing, no hardware filter, no ID table, every standard
  frame forwarded raw. RaceChrono's allow/deny protocol is still honoured coarsely — a
  deny-all stops the stream, any allow request restarts it — which needed
  `should_decode()` and the three allow/deny hooks in the base class to become
  `virtual`. Forwarding IDs the app did not ask for costs nothing, since it ignores IDs
  it has no channel for.
- ~~Do **not** add a CAN ID allow-list~~ — **superseded by
  [patch 14](#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured).**
  The reasoning here was that nothing is known about the bus yet, so there is nothing to
  filter *to*, and that ~16 IDs at ~1050 msg/s fits through the device anyway. The first
  half still holds and is why there is no static ID table. The second half turned out to
  be false: the BLE link gives up around 590 msg/s, not 1050. What resolves it is that the
  allow-list does not have to be known in advance — RaceChrono sends it, one ID per
  channel the rider defined, and honouring that needs no 🟢 rows at all.

## Patch 4 — bit-rate prescaler — **applied**

Not planned. Found by flashing the other three and watching nothing happen.

The symptom was a controller that came up clean and received nothing at all: no frames,
no errors, RX message counter zero, both error counters zero, `STATUS` reporting neither
reception nor error. A blind receiver and a disconnected one look identical from there,
and the wire was fine — the CANable was transmitting into it the whole time.

Core 3.x changed what `TWAI_TIMING_CONFIG_500KBITS()` *means*. It no longer carries a
prescaler at all:

```c
{.clk_src = TWAI_CLK_SRC_DEFAULT, .quanta_resolution_hz = 8000000, .brp = 0,
 .tseg_1 = 11, .tseg_2 = 4, .sjw = 2, .triple_sampling = false}
```

`brp = 0`, and the bit rate is stated as a quanta resolution for the `twai_*` driver to
divide the peripheral clock down to. joeroback's controller does not use that driver — it
writes the timing registers itself — so it wrote `brp = 0`, which
`twai_ll_set_bus_timing()` turns into `(0 / 2) - 1`. The sample point then lands nowhere
near a bit.

The fix derives the prescaler from the quanta resolution and the APB clock. On this board
that is 80 MHz / 8 MHz = 10, giving `BTR0 = 0x4004` and `BTR1 = 0x3a` — 16 tq of 8 MHz,
500 kbps exactly, which is what the registers read back on the bench.

This is the shape of bug the "known risk" below was really about, and note that it did
not show up as a compile error. Anything else on this path that changed meaning rather
than signature would fail the same silent way.

## ESP32 core 2.x vs 3.x — **resolved, no rewrite needed**

The installed core is **3.3.11**, chosen over 2.x because it is what is maintained and
what the `driver/twai.h` API documentation now describes.

joeroback's controller talks to the TWAI peripheral through its own ISR and register
access rather than the official `driver/twai.h` API, and was written against ESP32
Arduino core 2.x. The risk recorded here was that it would not compile, and that the
fallback would be a rewrite of `src/canbus/controller.cpp` against the `twai_*` driver.

It did not come to that. The whole low-level layer — `twai_ll_*`, the `TWAI` device
struct, `tx_rx_buffer`, `periph_module_enable()`, `esp_intr_alloc()` — still builds and,
more to the point, still *works* on 3.3.11: the ISR takes 359 frames/s off the bus and
queues every one of them. Three things had moved, and only two of them were visible to
the compiler:

- `twai_ll_set_mode()` gained its three-flag form, which is patch 2
- `esp_mac.h` is no longer pulled in transitively, so `src/racechrono/device.cpp` has to
  include it for `esp_read_mac()` / `ESP_MAC_BT`
- the timing macros stopped carrying a prescaler, which is patch 4 — silent, and the one
  that actually broke the device

Result: 655 KB of program (22%), 67 KB of static RAM (20%). The rewrite against the
`twai_*` driver stays in the back pocket, but there is now no reason to reach for it.

## Patch 5 — frame queue depth — **applied**

Upstream queues **8 frames** between the CAN ISR on core 1 and the BLE task on core 0.
That is about 8 ms of R9 traffic. Worse, the loss was invisible: the ISR ignored what
`xQueueSendToBackFromISR()` returned and counted the frame as forwarded either way, so a
full queue reported nothing at all — the same failure mode as the display bug in
[bench-link.md](bench-link.md), where 60% of frames vanished with every error counter
reading zero.

Now **2000 frames**: ~1.9 s of the R9's ~1050 msg/s, ~0.5 s of a saturated 500 kbps bus,
costing 26 KB of DRAM out of the 260 KB that was free. Static RAM went from 67 KB to
93 KB (28%).

The ISR now also counts dropped frames, and tracks the **peak queue depth**, which is the
only number that says whether 2000 is the right size. Both are cumulative — one dropped
frame matters, and a per-second average would hide it.

That headroom is not theoretical. On the first saturated burst after boot the queue backed
up to **1261 frames** before core 0 caught up, with nothing dropped. Upstream's 8 would
have lost over a thousand frames there and reported none of them. Later bursts at
~356 frames/s never queued more than a moment's worth.

A backlog of 1261 frames is ~0.3 s of core 0 not draining, which is a lot. The likely
cause is that the draining task is created at `tskIDLE_PRIORITY` and shares core 0 with
the BLE stack, so anything the stack does preempts it. Worth revisiting once BLE carries
real traffic — but with 2000 frames of queue, a stall that size no longer costs anything.

**It was the priority**, confirmed later with a phone on the other end:
[patch 12](#patch-12--the-drain-tasks-priority--applied) raises it one level and the
high-water mark falls from 1261 frames to single digits, with nothing dropped at any rate
the link survives. The queue depth was still the right change — it is what kept those
1261 frames instead of losing them, and what made the backlog measurable at all.

## Patch 6 — TX pad held recessive — **applied**

This was open question 1, and it is worth recording why it moved, because the premise it
started from was wrong.

Patch 1 defines `CAN_TX_PIN` as GPIO17 and `controller.cpp` deliberately configures only
RX — "we aren't transmitting". Not routing TX through the GPIO matrix is genuinely
useful and stays: it means the controller *cannot* transmit, whatever the mode register
says. But the pad was then left in its power-on state — high impedance — feeding the
SN65HVD230's `D` input.

The fear was that a floating `D` settling **low** would make the transceiver drive the bus
dominant and jam it. For this transceiver it does not: TI's driver function table
(SLOS346K, table 1) lists `D` = **Open** as **recessive**, and the electrical table says
why — `I_IH` and `I_IL` are both specified as −30 µA max, current flowing *out* of the
pin, which is an internal pull-up. Figure 11 puts it near 8 µA at 0 V, so roughly 400 kΩ
equivalent. The bench had been saying the same thing all along: with Bluecan flashed and
TX floating, the CANable pushed 359 frames/s across a bus that a stuck-dominant `D` would
have killed outright.

Nor is it a hardware difference between this build and upstream's. joeroback's is the
[SK Pang CAN-Bus FeatherWing](https://www.skpang.co.uk/products/can-bus-featherwing-for-esp32),
which carries an **SN65HVD231** — same datasheet, same function table as the KAmod's
SN65HVD230. Upstream never thought about the pin; the transceiver covered it.

Nothing on the MCU side fights that pull-up either. GPIO17 is not a strapping pin, has no
boot function on the S3, and its output driver is off until something configures it; the
`DAC1 = 17` alias in the variant file is dead, since the S3 has no DAC.

What survives all that is the **noise margin**. 400 kΩ is a weak hold. On the bench, with
a 10 cm jumper, it is irrelevant. On a bike harness a few µA of leakage or one coupled
transient is enough to pull `D` below its 2 V `V_IH`, and a dominant glitch corrupts a
frame for every node on the bus, not just for the reader. Driving the pad replaces 400 kΩ
with tens of ohms, for three lines:

```c
esp_rom_gpio_pad_select_gpio(CAN_TX_PIN);
gpio_set_pull_mode(CAN_TX_PIN, GPIO_PULLUP_ONLY);
gpio_set_level(CAN_TX_PIN, 1);
gpio_set_direction(CAN_TX_PIN, GPIO_MODE_OUTPUT);
```

Level **before** direction, so enabling the output driver cannot emit a brief low from a
zeroed output register. Program and RAM figures are unchanged: 655 KB and 93 KB.

Two things this does not cover, both worth knowing before the bike:

- **The boot window.** From power-on until `install()` runs, the pad is high impedance
  again and only the transceiver's pull-up holds it. Firmware cannot close that. If it
  should be closed, it is an external **10 kΩ pull-up from the KAmod's `CAN TX` pin to
  `3V3`** — it does not fight the driven-high pad, and it also covers reset, brownout and
  reflashing.
- **Do not connect `TWAI_TX_IDX` to the pad.** Driving it as a plain GPIO is what keeps
  listen-only enforced physically as well as in the mode register.

To confirm on the board rather than infer: multimeter between the KAmod's `CAN TX` goldpin
and GND immediately after boot — it should read ~3.3 V.

## Patch 7 — count the discarded frame classes — **applied**

Also an open question until now. The ISR drops two classes of frame before anything is
queued: extended (29-bit) frames, and remote-transmission requests. Neither left a trace.

Every ID in [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md) is 11-bit (0x20A…0x2A0), so the
extended path is probably never taken on the R9 — but the census cannot say so. A sniffer
that discards a class of traffic without counting it can only report the absence of what
it chose to look at, which is not the same claim. `_ef_count` and `_rt_count` turn "no
extended frames on this bus" into a measurement.

RTR was not in the original question, but it is the same three lines two lines up in the
same loop, and the same argument applies word for word.

Both are cumulative, like the drop counter: a handful of frames is exactly the case worth
noticing, and a per-second average would bury it. They print from `stats()`, so they are
DEBUG-only — the same limitation the drop and peak counters have, and the same fix (the
display) once it lands.

Cost: 16 bytes of program, 8 of RAM.

## Patch 8 — one set of counters, two readers — **applied**

The ISR's counters were reset by whoever printed them: `stats()` exchanged each one to
zero every five seconds. That is correct for exactly one reader, and the display is a
second one — every `stats()` line would have silently subtracted five seconds of traffic
from what the panel showed, and nothing would have looked wrong.

They are cumulative and monotonic now. Each reader keeps its own previous sample and
subtracts it, `stats()` included, so the serial log prints exactly what it always did.

Two things the same patch fixes, both about what the numbers mean:

- **`CAN bus msg/s` counted interrupts, not frames.** One RI interrupt can carry several
  messages, so the line under-reported the bus precisely when it mattered — under load.
  It is counted per frame now, inside the loop that reads them out of the hardware.
  `Interrupts/s` is still printed next to it, and the gap between the two is the
  measurement of how much batching the controller is doing
- **`connected()` dereferenced the BLE server before it existed.** Harmless while the
  only caller was the BLE task itself; the display reads it from the moment it boots

## Patch 9 — the display — **applied**

The board's TFT and its three buttons were driving nothing, and the counters that matter
most on a bike — dropped frames, queue peak, the discarded frame classes — existed only
in `DEBUG` builds, printed to a serial console the release build does not have and nobody
has at 200 km/h. Full write-up in [Display layer](#display-layer) below.

## NimBLE, not Bluedroid — **found while instrumenting the BLE half**

The same shape of surprise as patch 4: the code compiled, and it compiled against
something other than what it was written for.

joeroback's BLE code talks to the Arduino `BLEDevice` classes, which in core 2.x were a
wrapper over Bluedroid and nothing else. In 3.3.11 they wrap *whichever* host the
board's sdkconfig selects, and for the ESP32-S3 that is NimBLE:

```
# CONFIG_BLUEDROID_ENABLED is not set
CONFIG_NIMBLE_ENABLED=y
```

Three consequences, none of which surface until something reaches for them:

- **The callbacks have different signatures per host.** `onConnect`, `onMtuChanged` and
  `onConnParamsUpdate` take Bluedroid's `esp_ble_gatts_cb_param_t*` in one build and
  NimBLE's `ble_gap_conn_desc*` in the other. Writing the Bluedroid form here fails with
  *marked `override`, but does not override* — which is the good case, because it is loud.
- **`addDescriptor()` with a 0x2902 is a no-op.** NimBLE creates the CCCD itself for any
  characteristic carrying `NOTIFY`, and the library's `addDescriptor()` returns early
  with a deprecation log. Upstream's `_2902_desc.setNotifications(true)` — which on
  Bluedroid pre-arms notifications so that a client which never writes the descriptor
  still receives frames — therefore does nothing on this build. NimBLE will not notify
  until it has seen a real subscribe. Stricter, better, and the reason *connected but
  silent* is a state worth a colour on the glass.
- **`notify()` fails differently.** Bluedroid ends in `esp_ble_gatts_send_indicate()`,
  NimBLE in `ble_gatts_notify_custom()`, which returns non-zero when the host runs out
  of mbufs. Either way the function returns `void` and logs the failure at a level the
  release build never prints.
- **`onStatus()` fires twice for every notification**, from two places that mean
  different things: synchronously at the end of `notify()`, where the host has accepted
  the frame into its queue, and again from the `BLE_GAP_EVENT_NOTIFY_TX` GAP event,
  where the controller has actually put it on the air. Both report `SUCCESS_NOTIFY` with
  code 0, so they cannot be told apart from inside the callback. This is what made the
  first version of patch 10 report 19 frames/s against a 9.6 frames/s bus.

The upside: NimBLE raises `onSubscribe()` and Bluedroid does not, so the subscription
question has a real answer on the build actually being flashed. The Bluedroid
declarations are kept behind their own guard so the file still builds for a board
configured that way.

## Patch 10 — the BLE half reports what it is doing — **applied**

Everything from the wire to the frame queue is measured. The BLE half was not, and it is
the half nothing has ever exercised.

`send()` counted a frame the moment it handed it to the stack:

```c
_canbus_frames->setValue(data, len);
_canbus_frames->notify();
_ble_count.fetch_add(1, std::memory_order_relaxed);
```

A refused notification then looked exactly like a delivered one — the same silent loss
as upstream's eight-entry queue in patch 5, one layer further out, and with the same
consequence: the number that would say the link cannot keep up is the number that
cannot move.

`notify()` returns `void`, so the outcome is only reachable through `onStatus()`.
Registering for it splits one optimistic counter into three honest ones:

| | |
|---|---|
| `frames()` | frames handed to the stack, one per frame off the queue |
| `notifies()` | outcomes reported. **Twice `frames()` on a healthy link** — see below |
| `lost()` | the host or the controller refused it, with `last_error()` carrying the code |
| `unsubscribed()` | there was nowhere to send it. Not congestion; the fix is on the phone |

`frames()` counts where the frame is *offered*, and that is a correction: the first
version of this patch counted `SUCCESS_NOTIFY` instead, on the reasonable-sounding
grounds that it was the only honest confirmation. It reported 19 frames/s against a
9.6 frames/s bus, because NimBLE raises that callback twice per notification — once when
the host queues the frame and once when the controller transmits it. So the counter that
was meant to end an ambiguity had introduced one.

The ratio survives as the diagnostic rather than being thrown away: **2:1 is a link that
queues and transmits, 1:1 is a link that queues and then does not.**

**Read that the other way round after [patch 13](#patch-13--the-frame-path-goes-straight-at-the-host--applied).**
Bypassing `notify()` removes the callback that reported the host *queueing* a frame, so
only the transmit half is left and a healthy link reads **1:1**. The counter is labelled
`transmitted:` now to say which half it is. A ratio of **0:1** is the one to be afraid of,
and it means what patch 13 found: notifications going out addressed to a handle that does
not exist.

The wrong turn on the way is worth recording too, because it is the mode of failure this
whole patch is meant to prevent. The first theory was two BLE connections, each
notifying — plausible, and it survived an app restart. Adding a peer count to test it
disproved it in one reading (`peers 1`), which is the entire argument for putting numbers
on the glass instead of reasoning about them.

Two more facts the panel could not previously show:

- **whether RaceChrono subscribed.** Connecting and subscribing are separate steps, only
  the first is visible on the phone, and a connection without the second receives
  nothing while looking entirely healthy from both ends. `onSubscribe()` is the only
  place it is observable — see the NimBLE section above for why the descriptor is not.
- **the negotiated connection interval.** The radio only speaks at connection events, so
  this is the ceiling on frames per second no matter what the bus or the firmware does.
  7.5 ms is as fast as BLE goes; 30 ms will not carry the R9's ~1050 msg/s.

On the glass, the header goes from two states to three — `adv`, `BLE`, `SUB` — with the
interval beside it, and the BUS page carries `lost` and the peer count next to the frame
count:

```
bluecan SUB 15.0ms
ble       12043  356/s lost 0 p1
```

`p` should never read anything but `1` while anything is connected, and the row turns red
if it does.

Cost: 592 bytes of program, 24 of RAM.

## Patch 11 — ask the phone for a faster connection interval — **applied**

Until now the firmware accepted whatever the phone chose on its own, which is typically
30–50 ms — a few hundred frames a second against the R9's ~1050.

A peripheral is allowed to ask. `requestConnParams()` on connect does, and
`onConnParamsUpdate()` reads the answer back rather than assuming it, because the phone
can refuse and there would otherwise be no way to tell.

The window is four defines in `src/racechrono-canbus.hpp` and it is currently
**7.5–12.5 ms**, near the floor BLE allows, because the phone this is being tested
against is an Android one.

**Change it back to `12`/`24` (15–30 ms) before pairing an iPhone.** iOS rejects a
connection-parameter request outright unless

- interval min ≥ 15 ms
- interval max ≥ interval min + 15 ms
- slave latency ≤ 30
- supervision timeout ≤ 6 s

and a rejected request leaves the phone's own choice in place — silently, with nothing
on either side to say it happened. 15–30 ms is legal on both platforms. The reason to want
a short interval is real enough — the radio only speaks at connection events, and 7.5 ms
offers twice as many of them as 15 ms — but no throughput figure here is derived from that
arithmetic, because the only interval this board has been measured at is 15.00 ms, where it
carried **~470 frames a second sustained and ~740 through the ramp** (patch 13). Whatever
the shorter interval is worth, it is a property of the interval and not of the window, which
is what the section below is about: asking for the one does not get you the other.

Either way the phone has the final say, which is why the negotiated value is read back
and put on the header rather than assumed.

Costs 4 bytes of program.

### The interval is assigned, not granted

The obvious follow-up to patch 12 was to push the interval down, since at 15 ms the radio
only gets 67 connection events a second. The window was narrowed and the phone was asked
again. Everything this file records as negotiated, on the same Android phone — including the
sessions run for other reasons, since those asked for a window too:

| Requested | Negotiated | Session |
|---|---|---|
| 7.5–15 ms | 12.50 ms, 15.00 ms, 18.75 ms — varied by reconnect | patch 11 |
| 7.5–12.5 ms | 15.00 ms | patch 11 |
| 7.5–7.5 ms | 15.00 ms | patch 11 |
| 7.5–15 ms | 15.00 ms before-run, 12.50 ms after-run | patch 12 |
| 7.5–12.5 ms | 15.00 ms on every connection of the session | patch 13 |
| 7.5–12.5 ms | 12.50 ms, then 11.25 ms | 2026-08-22 |

The patch 12 and patch 13 rows are the same measurements reported further down this page. They
belong here because the defines moved underneath them: patch 13 ran at 6/10 without anybody
recording it, which makes its whole session evidence about the 7.5–12.5 ms window.

The last row asks for the same window as rows two and five, which between them produced
nothing but 15.00 ms, and it lands **inside** that window twice — once at a value no earlier
session produced at all. Whatever rule was going to be written here has to explain that, and
nothing on this table does.

So no rule is written here. Half the rows above vary across their own samples, and none of
them is more than a handful. **Treat the negotiated interval as something the phone
assigns rather than something it grants**: asking changes what is legal for it to pick,
and does not visibly change what it picks.

What does follow is procedural, and it is the part that matters. The interval decides
what a throughput measurement means — the same firmware measures visibly better at
12.50 ms than at 15.00 — and the phone chooses afresh on every reconnect. So read the
negotiated value off the `DEBUG` stats line or the display header on **every** run, and
never compare two runs that disagree on it. That is what those readouts are for, and it
is the whole reason `onConnParamsUpdate()` reads the answer back instead of assuming it.

One piece of bookkeeping, recorded because it silently invalidated the paragraph that
used to stand here. The defines ask for **6/10 (7.5–12.5 ms)**. They were 6/12
(7.5–15 ms) until commit `887e41f` — which is patch 13, and had nothing to do with the
interval; the change rode along with it unrecorded, and this section went on claiming
6/12 until 2026-08-22. The current value is left alone rather than restored, since the
window does not appear to decide the outcome either way.

## Patch 12 — the drain task's priority — **applied**

Open question 1 asked whether the link's ~200 msg/s ceiling was Bluetooth LE's limit or
ours, and asked for a before-measurement rather than a raised priority on faith.
[tools/ble-load.py](firmware/tools/ble-load.py) was that measurement. The answer is that
the ceiling was ours, and most of it.

The core 0 task that drains the frame queue into `notify()` ran at `tskIDLE_PRIORITY`.
FreeRTOS round-robins equal priorities, so it took turns with core 0's idle task — about
half of whatever the BLE host left it — and the display task at `tskIDLE_PRIORITY + 1`
preempted it on top of that.

It now runs at `tskIDLE_PRIORITY + 1`: above the idle task, **level with the panel rather
than above it**. That is deliberate. Under saturation the drain loop stops exiting, and
the panel is the only instrument that still reports while that lasts. Putting the drain
above it would buy nothing measurable and cost the one reading that survives.

### The raise is not viable on its own

This is the part worth carrying forward, because it would have looked like a
mystery reset rather than a design error. The drain loop never blocks: `recv()` polled
the queue with a zero timeout, and the outer loop spun regardless. At `tskIDLE_PRIORITY`
that is harmless, because the idle task gets every other slice anyway. One priority level
higher it is fatal — the ESP32 core is built with

```
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_PANIC=y
```

so a task above idle priority that never blocks panics the board within five seconds, on
an idle bus as surely as on a busy one. Two changes make the raise safe:

- **`recv()` takes a wait.** The task blocks 20 ms on an empty queue instead of polling
  it. `xQueueReceive` returns immediately when there is something to take, so a busy
  queue still drains at full speed — the wait only ever applies to an empty one.
- **A queue that never empties never blocks either**, which is the same watchdog problem
  by a different route. The loop hands core 0 back for one tick every 128 frames. At the
  rates this link gives up at that is 0.2% of the core, and the queue holds 2000.

Breaking out of the drain loop at that same boundary fixes a second thing: **the board no
longer goes silent under load.** `stats()` sits after the loop, and the loop used to stop
exiting exactly when its numbers were worth reading — the `blocks` column in
`ble-load.py` exists to report that. It is gated on its own 5 s timer and compiled out of
release builds, so reaching it every 128 frames costs a comparison.

### What it bought

Same board, same phone, connected and subscribed throughout, 20 s per rate. `lost` is
frames the BLE host refused; `dropped` is frames the ISR could not fit in the queue.

| Bus offers | BLE `offered`/s | `lost` before | `lost` after | `dropped` before | `dropped` after |
|---|---|---|---|---|---|
| 196 msg/s | 195.7 | 0 | 0 | 0 | 0 |
| 321 msg/s | 320.3 | 118 (2.5%) | **0** | 0 | 0 |
| 476 msg/s | 475.5 | 600 (6.5%) | **0** | 355 | **0** |
| 589 msg/s | 588.2 | — | 14 (0.1%) | — | 0 |
| 718 msg/s | 716.9 | — | 84 (0.6%) | — | 0 |
| 840 msg/s | 839.0 | — | 1302 (7.8%) | — | 0 |
| 914 msg/s | 913.4 | — | 4072 (22%) | — | 0 |

**Clean to about 590 msg/s where it was about 200**, degrading from there and breaking
down between 840 and 914. Against the R9's ~1050 msg/s the shortfall went from a factor
of five to a factor of about 1.8 — or nothing at all, if 0.6% is acceptable.

Two secondary results say where the remaining limit is *not*:

- **the ISR queue no longer drops a frame at any rate**, and its high-water mark fell
  from 1261 frames to single digits at every rate up to 476. The queue was never the
  problem; the thing emptying it was.
- **the drain path is no longer the bottleneck at all.** Pushed at a saturated bus, the
  board still took **3399 frames/s** off the queue and offered every one of them to the
  BLE host, which refused 94% of them. Nothing was dropped even there, and the queue
  peaked at 160.

  (`ble-load.py` reported 13047 bus msg/s for that row, which a 500 kbps bus cannot
  carry — the ceiling is ~4200. The figure comes from `can0`'s TX packet counter, and
  nothing on this bench ACKs the CANable, so it is counting automatic retransmissions.
  The board's own count is the honest one.)

So what is left refusing frames is the BLE host itself — `BLE_HS_ENOMEM`, out of mbufs —
and the next suspect on the list is the one inside it: NimBLE copies every value into a
`String` on each `notify()`.

### Caveats on the number

- **The phone negotiated 12.50 ms in the after-run against 15.00 ms in the before-run.**
  Android decides that, and it was not asked to change. It is worth ~20% more connection
  events a second, which is not the 3× in the table, but it is not nothing either.
- **Sustained rather than burst:** 150 s at 476 msg/s after the change — no reset, no
  frame dropped, and not one refusal. That is the run the watchdog would have failed, and
  the reason it is here rather than in a footnote.
- The before-run reproduced the old table's first four rows exactly and then **the board
  reset** during the 321 msg/s burst, taking the USB link down with it. The after-run
  never did, at any rate. One reset is not a pattern and the cause was never isolated, so
  it is recorded rather than claimed.
- `ble-load.py` now takes fractional gaps (`-g 1.3`), which is how the rates between 476
  and 914 msg/s were reached at all — `cangen`'s gap is in whole milliseconds only if you
  hand it whole milliseconds.

## Patch 13 — the frame path goes straight at the host — **applied**

With the drain task fixed, what was still refusing frames was NimBLE itself, and the
suspect on the list was the copying inside `BLECharacteristic::notify()`. It is worth
spelling out, because it is three heap allocations to move twelve bytes that are already
flat in memory:

1. `setValue(data, len)` takes a FreeRTOS semaphore and assigns the bytes into a
   `String`.
2. `notify()` copies that `String` into a second, local one.
3. `ble_hs_mbuf_from_flat()` then copies *that* into the mbuf the host actually wanted.

The frame path now calls the host directly — `ble_hs_mbuf_from_flat()` on the frame's own
bytes, then `ble_gatts_notify_custom()` — and keeps the wrapper for everything it is
genuinely good at: defining the characteristic, tracking the subscription, and delivering
`onStatus()`. `notify_custom()` consumes the mbuf whatever the outcome, so there is
nothing to free on the failure path.

### The trap this cost an invalid measurement to find

`BLECharacteristic::getHandle()` does not return a usable handle until NimBLE has
registered the GATT table. The wrapper hands the host a *pointer* to its own `m_handle`
member (`pChr_a[i].val_handle = &pCharacteristic->m_handle`) and the host fills it in
during registration — which happens after `createCharacteristic()` and after
`BLEService::start()` have both returned. Read it in `start()`, as the first version of
this patch did, and you get `0xffff`, the wrapper's "not known yet".

What `0xffff` does is the dangerous part. `ble_gatts_notify_custom()` accepts it, returns
**0**, and puts a notification on the air addressed to an attribute that does not exist.
Every counter on the board reads healthy — frames offered, none refused, the host
congesting under load exactly as it should, because the packets are real and the radio is
really sending them — and the phone receives nothing at all. A whole ramp was measured
that way before anything noticed.

What noticed was the one counter that had no reason to be zero: `transmitted` read
**0.00 per frame** where the old path read 2.00. `BLE_GAP_EVENT_NOTIFY_TX` is dispatched
by looking the attribute handle up among the server's notify characteristics, so a
notification sent to `0xffff` finds nothing to report against. The counter that existed
to measure congestion turned out to be the only thing standing between a wrong number and
a written-down conclusion.

The handle is now resolved in `onSubscribe()` — the earliest moment it is certainly
registered, since a subscription cannot exist before the GATT table does — and it is
printed on the `DEBUG` stats line beside the interval and MTU, where `18` is a handle and
`65535` is the bug.

### Counting changed, and for the better

`transmitted` (`_ble_count`) used to read **twice** `offered`, because the wrapper reports
a success of its own when the host accepts a frame, and `NOTIFY_TX` reports another when
the controller transmits it. Going straight at the host leaves only the second, so a
healthy link now reads **1:1**, and the number means one thing: frames that reached the
air. The label changed from `notified:` to `transmitted:` to say so, and
[tools/ble-load.py](firmware/tools/ble-load.py) reads it as `tx/frame`.

### What it bought

Both runs on the same board, the same phone, one connection interval — **15.00 ms**,
which is what the phone happened to settle on for both — 20 s per rate. The switch that
makes this A/B possible is `RC_WRAPPER_NOTIFY` in `src/racechrono-canbus.hpp`: define it
and the frame path goes back through `notify()`. It exists because the phone renegotiates
the interval on every reconnect, and two firmware builds compared across two different
intervals compare nothing.

| Bus offers | Refused via `notify()` | Refused direct |
|---|---|---|
| 196 msg/s | 0 | 0 |
| 321 msg/s | 0 | 0 |
| ~473 msg/s | 20 (0.2%) | **0** |
| ~591 msg/s | 92 (0.8%) | **41 (0.35%)** |
| ~718 msg/s | 472 (3.3%) | **315 (2.2%)** |
| ~846 msg/s | 5902 (35%) | **2042 (12%)** |

**Worth about half the refusals in the middle of the ramp and two thirds of them at the
end**, and it moves the clean point up by roughly one step. It does not move the wall:
the link still starts refusing somewhere between 470 and 590 msg/s, and the error behind
every refusal is still `BLE_HS_ENOMEM`. What that says is that the copying was real but
secondary — the host runs out of mbufs because the radio cannot drain them at this
connection interval, and removing work upstream of a full queue helps only as much as it
delays filling it.

150 s at 476 msg/s afterwards, at the same 15.00 ms: **70400 offered, 70375 transmitted,
25 refused** — 0.035% — no reset, nothing dropped, queue peak 8.

And then the check the board cannot perform on itself: `tools/sweep.sh --hz 5` running
into the phone with the channel open, watched in RaceChrono. The PID moves, at the
sweep's own rate. That is the confirmation the handle bug proves is necessary — every
counter on this board read healthy while notifications went to an attribute that did not
exist, and only the app can tell those two apart.

### What is left

Nothing else on the board is between the frame and the radio. The remaining levers are
the connection interval, which the phone decides (see patch 11), and NimBLE's mbuf pool
— `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT` and friends, which are baked into the Arduino
core's precompiled libraries and cannot be changed without building ESP-IDF from source.
Packing several frames into one notification is not available: RaceChrono's protocol
defines one CAN frame per notification, and the MTU is 23 anyway.

## Patch 14 — honour the allow-list and the notify interval — **applied and measured**

Upstream's `decoder::onWrite()` parses RaceChrono's allow/deny protocol correctly and
then throws away both halves of what it parsed. `notifyIntervalMs` was read into a local
and never used, under a comment saying so; and `decoder_yamahar9::allow_id()` ignored its
`id` argument entirely, because [patch 3](#patch-3--decoder-and-filtering--applied) made
any allow request mean "forward everything". That was the right call when the device was
a sniffer and nothing about the bus was known. It is the wrong one now that
[the device is a telemetry link](README.md#sniffing-is-the-canables-job-not-this-one).

The point is not that the filter saves the radio work — it is that **it makes the radio's
ceiling stop mattering.** The link starts refusing frames somewhere around 590 msg/s and
the R9 broadcasts ~1050, a shortfall patches 12 and 13 narrowed but could not close, and
[the last lever left](#what-is-left) is inside a precompiled library. An allow-list of the
few ids a rider has channels for is tens of messages a second. The 1.8× shortfall applies
to traffic the app was never going to look at.

### What changed

- `decoder::allow_all()` and `decoder::allow_id()` take the notify interval the app asked
  for. The base class still ignores it, and now says why: a decoder with a static id
  table states its own per-id rates measured against that vehicle's bus, and those are
  better information than a phone's request. `decoder_bmwg8x` is unaffected — it
  overrides neither.
- `decoder_yamahar9` grows a **dynamic allow-list**: 32 slots of
  `{id, interval_us, last_us}`, filled in from the app's requests. RaceChrono sends a
  deny-all followed by one allow per id it has a channel for, which is exactly an
  append-only list with a reset, so that is what it is.
- Frames the app never asked for are dropped **in the CAN ISR**, before they cost a queue
  slot, a drain-loop iteration or an mbuf.

### A time gate, not a frame divider

The base class rate-limits by counting frames: forward one in every `rate`. That works for
the G8x, where the id table was built against a known bus and `rate` is a measured
divider. It cannot work here, because turning "every 100 ms" into a divider needs the
native frequency of the id — and which ids the R9 even broadcasts is the open question
this whole repository exists to answer.

So the R9 decoder gates on elapsed time instead: `esp_timer_get_time()`, and forward if
`now - last >= interval_us`. The app states its wish in milliseconds; it is honoured in
milliseconds. An interval of 0 means every frame.

`esp_timer_get_time()` is safe in the ISR — it is in IRAM and reads a counter register —
and the scan is at most 32 aligned comparisons, on a path that already makes a virtual
call.

### Three things deliberately not done

**The boot state is still "forward everything."** Nothing about the bench changes until an
app connects and says otherwise. That is what makes the board a usable sniffer with no
phone in the room, and it is what every measurement in [bench-link.md](bench-link.md) and
every run of `ble-load.py`, `sweep.sh` and `soak.sh` was taken against. A deny-by-default
would have quietly changed what those tools measure, which is a worse bug than the one
being fixed here because nothing would report it.

**An allow-all request is not rate-limited.** There is no slot in which to keep a
timestamp for an id nobody has named, and learning ids inside the ISR would mean writing
the table from two contexts at once. Allow-all is the sniffer case; its interval is read,
logged and ignored.

**The allow-list is not cleared when the phone disconnects.** `device::onDisconnect()`
resets the connection handle, the negotiated interval, the MTU and the subscription, and
leaves the decoder alone on purpose. Nothing has established that RaceChrono re-sends its
deny-all-then-allow list on a mid-ride reconnect, and a filter that reset itself to
allow-all every time the link dropped would put the R9's full ~1050 msg/s back onto a
radio that starts refusing near 590 — at the one moment the rider most needs the link
back. Riding is the case that wins.

The price is a bench trap, and it is worth stating plainly because nothing on the console
reports it: **"the boot state is still forward everything" holds only until the first
connection of a power cycle.** After a recording session the board keeps `_allow_all ==
false` and its filled slots until it is rebooted or reflashed, so a census, a
`ble-load.py` run or a soak taken afterwards silently measures a filtered bus. The panel's
`flt` field is the only instrument that says which state the board is in. **Reboot before
any bench measurement that follows a phone session** — also written down in
[next-without-the-bike.md](../journal/next-without-the-bike.md#bench-facts-that-cost-time-to-rediscover).

### Ordering, and what the panel gained

The list is written by the BLE task and read by the CAN ISR. A slot is filled completely
and *then* published by a release store to the count, which the ISR loads with acquire, so
it can never see an id whose interval is still whatever was in the slot before.
`last_us` is written only by the ISR. A deny-all is a single store of zero: an ISR racing
it either forwards one more frame or none, and both are correct answers to a deny that
arrived mid-frame. An allow request past the 32nd is counted, not merely logged — a
release build has no console, and an id the rider defined a channel for that silently
never arrives is the worst available failure.

The BUS page's `fwd` line now ends in `flt ALL` or `flt 3`, and in `flt 32!` in red if a
request ever overflowed. The IDS page already showed
*which* ids crossed the filter, since the census is fed after the send and sees only what
was forwarded — but it cannot distinguish "the phone asked for three ids" from "the phone
never sent its allow-list", and neither can any other counter on the panel. That is the
same class of trap as patch 13's attribute handle, and it costs one field to close.

### What has not happened yet

**Run 1 of 4 is done. The three that need a phone are not.**

1. ~~**Regression, no phone.**~~ **Passed.** Allow-all at boot, the 16-id soak profile:

   | | `CAN bus msg/s` | `RaceChrono msg/s` | dropped | errors | queue peak |
   |---|---|---|---|---|---|
   | block 1 | 691.00 | 691.00 | 0 | 0 | 5 |
   | block 2 | 712.00 | 712.00 | 0 | 0 | 5 |
   | lighter run | 250.40 | 250.40 | 0 | 0 | — |

   Exactly 1:1, which is the whole claim: the boot state still forwards everything, and
   nothing on the bench changed. The `flt ALL` field was not read off the glass — the
   console cannot see the panel.

2. **The filter.** Phone with three channels on three known ids. Serial must show
   `ID request DENY all` and three `ALLOW ID` lines; `rc/s` must fall to those ids' share
   of the bus; the IDS page must list exactly three; the panel must read `flt 3`.
3. **The interval.** One channel at 10 Hz against an id `cangen` is sending at 100 Hz.
   That id's contribution must be ~10/s, not ~100/s. Nothing in this repository has
   measured this before, because until now nothing honoured it.
4. **The payoff.** `ble-load.py` with a three-id allow-list and the bus driven past
   900 msg/s. Refusals must stay at zero, because a few tens of messages a second reach
   the radio. That number belongs in the README.

Runs 2–4 are blocked on a phone, and there is no way around it from the laptop: WSL has
no Bluetooth stack at all — `/sys/class/bluetooth` is empty and there is no host
controller to pass through — so a scripted BLE client that writes the allow/deny
characteristic and counts notifications cannot be built here. The app is the only thing
that can drive this protocol.

### Generating a multi-id profile against a listen-only board needs `one-shot on`

This is narrower than the first version of this section claimed, and
[bench-link.md](bench-link.md#listen-only-proven-before-the-bike) had already written down the mechanism: with the
Feather listen-only there is nobody to ACK, so the CANable **retransmits the same frame
automatically and forever** — 2018 frames in one second after three `cansend` calls, all
of them retries of one frame, all of them reaching the Feather.

So the bus is not quiet without one-shot. It is *flooded*, with duplicates. What stalls
is the supply of **distinct** frames: `cangen` wedges on the stopped TX queue and no new
id ever gets out. Measured over the same six seconds:

| | distinct frames the CANable reports transmitting |
|---|---|
| default (automatic retransmission) | **6** |
| `one-shot on` | **2484** |

That is the CANable's own TX counter, not what the Feather receives, and the difference
matters: the first column is a saturated bus carrying one id, the second is a bus
carrying the profile that was asked for. For the 16-id regression above, only the second
is usable.

```
sudo ./tools/can-up.sh --one-shot
```

`can-up.sh` carries the flag as of 2026-08-22 — and, control modes being sticky, it now
spells out `one-shot off` on every bring-up that does not ask for it, so the setting can no
longer be inherited from an earlier run. On the bike the question disappears, since the
ECU, ABS and IMU nodes acknowledge each other.

### The console drops out under usbip — **superseded by patch 15**

Kept as the record of how the numbers above were taken, and wrong about the cause. The
port was not dropping out: the board was **panicking and rebooting** on the USB CDC write,
which [patch 15](#patch-15--logging-without-interrupts-off--applied-and-measured) found
and fixed. A console can be held open indefinitely now — 600 s under `bluecan-soak.py` — so
the ten-second rule below applies to nothing that is still flashed. What survives it is
"re-attach after every flash", and that a dead port and a board receiving nothing look
identical.

Not this patch's doing — checked rather than assumed. With the working tree stashed and
`b8bd163` flashed as a `--debug` build, a held-open console died after **12.2 s / 35
lines**; with the patch restored, **5.6 s / 4 lines**. Both die, so the cause is upstream
of the change. It shaped how the numbers above were taken. The
Feather's CDC port survives about ten seconds of being held open and then vanishes from
WSL, needing `./tools/usb-attach.sh 1-3` before the next read. It survives 45 s of idle
untouched, so it is the open session that provokes it, not time or CAN load. Two things
follow: read in bursts of ten seconds, and **do not read a counter you have not just
re-attached for** — a dead port and a board receiving nothing look identical, and cost
this session three measurements that read as zero traffic before the cause was found.
Opening the port does *not* reset the board, which is what makes short reads usable at
all: the cumulative counters survive across sessions.

**One unknown, cheap to settle:** what RaceChrono actually puts in `notifyIntervalMs` —
whether it is the per-channel rate, and what it sends when the channel is set to maximum
rate (0 is the guess). Step 2's serial log answers it before anything depends on the
answer, since the `verboseln` was already there. Interval 0 forwards every frame.

### What the app sends depends on the session — **measured 2026-08-22**

The protocol is only visible from `cfb313e` onwards: the three lines that log it were
`verboseln`, and `setup()` ships `log_level::info`, so they had never printed. The bench
test this page listed as owed — *"serial must show `ID request DENY all` and three
`ALLOW ID` lines"* — could not have passed as written on any build at any rate.

With that fixed, RaceChrono Pro turns out to send **two different things**, and which one
depends on what the app is doing rather than on how the channels are set up. Three
CAN-Bus channels on `0x200`, `0x201`, `0x202` throughout:

| App state | What arrives on `0x0002` | Panel |
|---|---|---|
| Test connection / configuring | `CMD 1 LEN 3` — `ALLOW all INTERVAL 50 ms` | `flt ALL` |
| Recording a real session | `CMD 0 LEN 1` — `DENY all`, then `CMD 2 LEN 7` ×3 — `ALLOW ID 0x200/0x201/0x202 INTERVAL 50 ms` | `flt 3` |

Which makes sense the moment it is stated: while a channel is being configured the app
needs every id on the bus to populate its live-data row, and once it is recording it
wants only what it has channels for. **An earlier version of this section read one
allow-all off a test connection and concluded the app never sends a per-id list. That was
wrong, and so was what followed from it** — patch 14's premise is correct, and the
comment in `decoder_yamahar9.cpp` calling allow-all "the sniffer case" is right too. The
mistake was generalising from a single connection without varying what the app was doing.

### Patch 14, measured — the filter, the interval and the payoff

All three of the runs this page has owed, taken in one session against a recording
RaceChrono at a **15.00 ms** connection interval.

**The filter.** `cangen` on `0x100`, which has no channel, at ~470 msg/s, plus `0x200` at
~190 msg/s. `Census ids: 4` — the board sees all four — while `CAN bus msg/s: 635.80` and
`RaceChrono msg/s: 19.60`. Everything unasked-for is dropped in the CAN ISR, and the
panel reads `flt 3`.

**The interval.** This had never been measured at all. `0x200` arrives at ~190 msg/s and
leaves at **19.4–19.6 msg/s** against the 50 ms — 20 Hz — the app asked for. The gate is
accurate to within 3%.

**The payoff.** 635.8 msg/s on the bus, 19.6 msg/s over the radio, `Dropped frames: 0`,
queue peak unmoved at 32, and `Bluetooth LE lost` static at 51 across every sample of the
run — the 51 having accumulated earlier, unfiltered. **Zero refusals.** Which is the pass
condition as written, and it does what patch 14 said it would: the ~590 msg/s ceiling
becomes a number about traffic nobody wanted.

## Patch 15 — logging without interrupts off — **applied and measured**

[Open question 4](#open-questions-before-the-bike) asked whether an
unread console stalls core 0, and treated the console dying after 5–12 seconds as a
separate nuisance. They are one fault, the answer is worse than a stall, and the reason
nobody could see it is worth writing down first.

### The board was not losing the port. It was rebooting.

A panic on this board is invisible. `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` in the build's
`sdkconfig`, so ESP-IDF's own console — every `Guru Meditation Error`, every backtrace,
every `rst:` line — goes to **UART0**, on pins nothing is wired to. The secondary console
is `USB_SERIAL_JTAG`, which is not enumerated because this build runs **TinyUSB**
(`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=1`), so `Serial` is `USBSerial`, a
`USBCDC`. The panic goes out of the one port that is not being read, the board resets,
TinyUSB re-enumerates, and usbipd's attachment goes with it. From WSL that looks exactly
like a dropped USB link.

Proving which it was needs a counter that only grows while the board is up. Every
cumulative counter here qualifies, and the queue's high-water mark is the blunt one.

Two reproductions, `cangen` at 919 msg/s, console held open: the port went at **2.08 s
after 2 lines**, and on the second run at **4.58 s after 12 lines**, having printed
`Queue peak: 6 of 2000` at t=3.77. Re-attached straight afterwards, with the traffic
never stopped, the board reported

```
         Queue peak:    0 of 2000
       Display draw: 252388 us, worst 252388 us, 417 chars
         Census ids: 20, 303 over
```

A high-water mark of 6 does not become 0, and a census of 303 is a few seconds of
traffic that had by then been running for over a minute — the same traffic leaves 63457
on a board that stays up. The draw figure also equals the worst figure, which happens
only on the panel's **first** paint. It had rebooted, and gone on rebooting.

### The mechanism

`logger::log()` wrapped the write in a critical section:

```cpp
portENTER_CRITICAL(&_lock);
Serial.write(buf, len);
Serial.flush();   // when writing to Serial on different cores, flush() seems required
portEXIT_CRITICAL(&_lock);
```

`USBCDC::write()` takes a FreeRTOS semaphore, then loops on
`tud_cdc_n_write_available()` — spinning until the **host** reads — for up to
`tx_timeout_ms`, which the core defaults to **250 ms**. Run that with interrupts
disabled and the USB interrupt that would drain the buffer cannot fire, so the wait can
only end by timing out. `CONFIG_ESP_INT_WDT_TIMEOUT_MS` is **300**. One log line into a
console that is momentarily behind spends 250 of those 300 ms with interrupts off, and
two in a row do not fit. It also calls into TinyUSB from a context TinyUSB does not
expect, and freezes every other task on that core meanwhile — the display task included.

`portENTER_CRITICAL` takes a shared spinlock, so this is not confined to one core: a
`bootln` from core 1 waits on a `verboseln` from core 0.

### The 250 ms that was not the timeout

`Display draw: … worst 250951 us` looked like direct evidence: 250.951 ms against a
250 ms CDC timeout. It is a coincidence, and a costly one. `_draw_us` times `refresh()`
only — `stats()`, which is what logs, sits outside it. The number is the panel's first
full paint: **417 characters** at ~0.6 ms each, since `field()` draws every character
over its own background and the change-detection cache starts empty. It is ~251 ms on
every boot, it does not recur, and because it is re-established at nearly the same value
after a reset it is useless as a reboot detector — which is what made it look cumulative.

### What changed

Three changes, each replacing what it removes rather than deleting it.

- **A mutex, not a critical section.** A FreeRTOS mutex lets the holder be preempted,
  carries priority inheritance, and gives up after 50 ms rather than blocking a task
  indefinitely. It is statically allocated: the logger is a function-local static built
  by whichever task logs first, and that must not depend on the heap.
- **One `write()` per line**, newline in the same buffer, instead of `write()` +
  `println()`. This is what the `flush()` was actually buying — a line from one core
  could not be cut in half by a line from the other. Both `USBCDC` and `HardwareSerial`
  hold their own lock for the whole of a single `write()`, so a whole line goes out
  whole. `flush()` itself only waited for the host; `write()` has already handed the
  bytes to the USB stack.
- **`Serial.setTxTimeoutMs(20)`** in `setup()`. With interrupts on, a console that is
  actually reading never needs 20 ms; one that is not should cost a truncated line, not
  a queue backlog.

Fixed while in there: `size_t len = snprintf(buf, sizeof(buf), …)` took snprintf's
return value, which is what it *would* have written. Any message past 128 bytes made
`Serial.write(buf, len)` read off the end of a stack array, on a part with no MMU to
catch it. Now clamped, and a negative return drops the line.

### This was never `DEBUG`-only

The old open question closed with "the release build never writes to serial". It does:
`bootln`, `errorln` and `warnln` are compiled in unconditionally — only `info`,
`verbose` and `debug` become no-op macros. A release build prints ~25 boot lines, and
`warnln("Bluetooth LE connection parameter request refused locally")` fires during a
normal session. With no host reading, `tud_cdc_n_connected()` is false and every write
returns immediately, which is why this never showed on the bike. Plug a laptop into the
board in the pits and the same panic is armed in a release build.

### What it bought

`cangen` at **919 msg/s** into Bluecan over the bench pair, `DEBUG` build both times,
console held open by the same reader.

| | before | after |
|---|---|---|
| Console held open under traffic | dead at **2.08 s** and **4.58 s**, two runs | **600 s**, 2032 lines, no reboot |
| Frames dropped | not measurable — the board kept resetting | **0** |
| Queue peak | not measurable, same reason | **21** of 2000 |
| Census, cumulative | fell to 303 at every reset | **699043**, monotonic throughout |

There is no before column worth printing for the queue and the drop counters: both are
cumulative since boot, and the board was not staying booted. That is the real cost of
this bug — not the lost console, but that **every `DEBUG` number ever taken with a
console attached was taken on a board that had recently restarted.**

### What this puts in doubt

`ble-load.py` opens `/dev/ttyACM0` with DTR asserted before the first burst and drains
it continuously until the last one — its `pump()` docstring explains why it has to. A
standard ramp is three 20 s bursts plus settles, so the console is held open for well
over a minute. That is precisely the condition that killed the board.

This does not mean the ramp's numbers are wrong; it means nothing here establishes that
they are right. **~590 msg/s clean and ~718 at 0.6% loss are the figures most of this
project's reasoning rests on**, including the case for the allow-list, and they were
taken under a fault that resets cumulative counters without saying so. A ramp is cheap
and needs only the phone that
[item 1 of the without-the-bike list](../journal/next-without-the-bike.md) already requires, so
re-run it with patch 15 in before building anything further on the ceiling.

**Open question 4, answered.** With the fix in, the queue's high-water mark over a
minute with **no reader attached** is **21** at 919 msg/s, and holding the console open
for the following two minutes did not move it off 21 or drop a frame. The console costs
nothing measurable. The old asymmetry — 1307 frames with no reader, never more than ~50
with the console open — was not a measurement of the console's cost: with the console
open the board was rebooting every few seconds, and a high-water mark cannot climb past
the next reset.

## Patch 16 — the NeoPixel as the rider's status light — **applied**

In Bluecan at `trunk`: `src/led/led.{hpp,cpp}` gained the pixel, `src/display` gained
the ladder that drives it, and `CONFIG_STATUS_LED` selects it for the Reverse TFT board
alongside `CONFIG_DISPLAY_TFT`.

The panel answers every question this device can be asked, and on a moving bike none of
those answers are readable. It is 1-size text on a 240×135 screen, in a tank bag or under
a seat, and [D0 exists](#pages) precisely because the backlight is the most expensive
thing on the board to leave running. The on-board NeoPixel is the only instrument that
works at a glance, gloved, in daylight, from wherever the board ends up strapped.

**What it must not show is vehicle data.** No shift light, no lean-angle bar, no coolant
warning. This firmware forwards raw frames and never decodes a signal — decoding is
RaceChrono's job, app-side, which is exactly why an unfinished
[pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md) never blocks the firmware. An RPM light would
need a 🟢 row, a decoder in the firmware and a scale to trust, which is the sniffer role
that [was settled the other way](README.md#sniffing-is-the-canables-job-not-this-one).
The pixel's subject is the **chain** — bus → queue → radio → phone — and the chain is
something this firmware already knows everything about.

### Hardware, and where it is allowed to be written

Data is GPIO33, power is gated by GPIO21 ([hardware.md](hardware.md#pin-map)); both are
already spoken for by the pixel and free for nothing else. One WS2812 is 24 bits at
800 kHz — **~30 µs per update** through the core's `rgbLedWrite()`, which after its first
call is an `rmtInit()` that early-returns plus a blocking `rmtWrite()`. The first call
therefore allocates, so it happens in `display::run()` at start-up rather than lazily,
where an allocation would show up in a free-heap measurement taken later.

It is written **only from the display task**, on core 0, on that task's own 25 ms poll —
not on its repaint. That is rule 1 of the [display module](#display-layer) and it is the
same rule for the same reason: 30 µs next to a ~420 µs idle repaint is not measurable, and
the same 30 µs inside the CAN ISR would be. Nothing on the frame path gained a line. A
write whose colour matches what is already on the pixel is dropped, so an animation
between steps costs a comparison.

`LED.status()` takes full-scale colours and dims them to `CONFIG_STATUS_LED_LEVEL`,
**24/255**. Daylight sets that floor rather than night — a WS2812 at full brightness is a
torch in a mirror, and the third light position below turns it off outright.

### The ladder

One LED, so precedence is strict. Hue says where the chain is, motion says frames are
moving, red says something was lost. Every term is a counter the BUS page already reads;
nothing is measured for the light's sake.

| Condition | Colour | Pattern |
|---|---|---|
| `!CANCTLR.running()` | white | breathe, 2 s |
| status `BS` | red | solid |
| status `ES` | amber | blink, 1 Hz |
| `!RCDEV.subscribed()` | blue | breathe, 2 s |
| subscribed, `filter_size() < 0` — `flt ALL`, configuring | yellow | flow blips |
| subscribed, `filter_size() >= 0` — recording | green | flow blips |
| subscribed, `_fwd_rate == 0` for >2 s | its hue, flatlined | one dim pulse every 2 s |
| **overlay**: `dropped`, `filter_overflow()`, `peers > 1`, or `lost` growing under an active filter | red | 60 ms flash a second **over** the hue, latching |

**Flow blips**: a 60 ms flash on a dim floor at `clamp(_fwd_rate / 8, 1, 5)` Hz, so the
~20 msg/s per id a recording session produces reads as a visible 2 Hz heartbeat and a
saturated bench bus reads as a fast one. The floor is not black on purpose — the hue is
the state, and dropping it between flashes would make a slow link look like a dark board.
Note where the clamp lands: **anything above 40 msg/s is 5 Hz**, so a fast bench bus tells
you less about this light than 20 msg/s does, not more.

**The flatline rung is the one that earns its place.** Slow and stopped are the two
states no other indicator on this board can tell apart, and a diagnostic connector that
fell off the bike is the second one.

### The flatline was invisible, and that is the worst rung to lose

Reported from the other side of the desk on the day it was flashed: *"in idle the LED
does not light at all"*. It was lighting — the first version of the flatline was one
60 ms pulse at the blips' dim floor, which is a **3% duty cycle at a quarter of a
brightness already capped at 24/255**. On the bench that is a pixel that is simply off.

Which is the worst possible way for this particular rung to fail, because "subscribed,
and nothing is arriving" is what a diagnostic connector falling off the bike looks like.
It is now **two 90 ms flashes at full level, 240 ms apart, every two seconds** — two
rather than one because a single tick is hard to tell from a slow heartbeat, and telling
it from a slow heartbeat is the entire point of the rung.

Confirmed from the console rather than guessed at:
[`tools/led-state.py`](firmware/tools/led-state.py) read `yellow (subscribed, flt ALL --
configuring) FLATLINE -- no frames 2 s+` while the board sat there looking dark.

### The light reports itself

A colour is the one thing a shell cannot read, and the panel at least reports its own
repaint cost. So in a `DEBUG` build the light says what rung it is on and why — on the
5 s stats cadence, and **the moment the rung changes**, which is what makes it usable
while somebody is watching the pixel:

```
Status light: green          (subscribed, filtered -- recording) blips 2 Hz  + FAULT flash
Status light: blue breathe   (nothing subscribed)
Status light: off -- D0 is on the dark position
```

[`tools/led-state.py`](firmware/tools/led-state.py) holds the console open, filters to
those lines and can generate the traffic to move them:

```bash
./tools/led-state.py --rate 20
```

**Its rate is capped at 100 msg/s**, and that is not caution — it is that the blip clamp
pins at 5 Hz from 40 msg/s upward, so a faster bus makes the light *less* informative
while making the board work harder. `--rate 20` is one allowed id at RaceChrono's 50 ms,
which is the load the device will actually meet, and it reads as `blips 2 Hz`. Measured
on the bench: 99 msg/s → `blips 5 Hz`, 20 msg/s → `blips 2 Hz`, nothing → `FLATLINE`
within two seconds.

**The fault is an overlay, not a rung**, and that is the design decision here worth
arguing for. Made a rung it would replace the hue and hide whether frames were still
flowing at the moment that is most worth knowing; flashed over it, "recording, and
something was lost" is one glance from "recording". It latches, because `dropped` and
`lost` are cumulative and a rider is not watching; the BUS page names which counter moved.

**There is no cyan rung.** The design had one — connected but not subscribed, between
blue and yellow — and it is gone: it and "advertising, nobody there" differ in what the
rider would do about them not at all, and one fewer colour to learn is worth more than a
distinction only useful to someone already standing over the board, where the header on
the BUS page draws it anyway. Blue now covers both.

**The bus-error rungs are nearly unreachable and are kept anyway.** In listen-only this
node never transmits and its error counters are frozen, so `BS` cannot arise and
`!running` is, in practice, the second and a half between the display task starting and
`setup()` reaching `CANCTLR.start()` — a failure there restarts the board. Read the white
breathe as booting. Two comparisons is a fair price for the day that assumption is wrong.

### Refusals under an allow-all are not a fault — found by running it

The first version latched the overlay on `RCDEV.lost()` being non-zero at all. The bench
run that followed had a phone attached on a **test** connection, and it landed the point
by itself: `Bluetooth LE lost: 15001` in ninety seconds at 919 msg/s, with `flt ALL`.

That is not a fault, it is the documented behaviour of an unrated allow-all — no per-id
slot means no rate limit, so the whole bus is offered to a radio that gives up around
590 msg/s. And because `lost` is cumulative, latching on the total would have lit the
fault for **every session after the first time anyone configured a channel**. It now
latches on `lost` *growing* between two samples and only while the allow-list is in
force; `dropped`, `filter_overflow()` and a second peer still latch unconditionally,
since none of those has an expected case.

The fault terms are sampled on the 1 Hz rate sampler rather than the 25 ms tick — the
latch makes a second's delay free, and it keeps `RCDEV.peers()`, the one term that
reaches into the NimBLE server instead of reading an atomic, off the fast path.

### D0 is a three-way now

`TFT + pixel → pixel only → dark`, and `_lit` became `_light`. Pixel-only is the riding
position: the repaint and the backlight are the two things worth switching off, and the
status light is the one thing worth keeping. It also keeps D0 honest as
[the cheapest A/B on the board](README.md#picking-this-back-up) — the pixel costs 30 µs,
so the middle position isolates the panel's cost and nothing else. `Display page:` in the
`DEBUG` stats reports all three.

### What the bench says, and what it does not

Flashed and run against `cangen` at ~919 msg/s with a phone connected and subscribed,
60 s: **0 dropped, 0 reboots, queue peak 17 of 2000**, display task stack 1908 bytes
free, and the repaint cost unchanged in shape (9794 µs for 12 changed characters — the
same ~800 µs a character this panel has always cost under load). Nothing between
`field()` and the page renderers was touched, so the frame path and the paint path are
the same code they were.

**Two rows have been looked at since.** The fault flash works, confirmed by eye. The
flatline did not, and is fixed above. Everything else is still unseen — a colour cannot
be passed from a serial console, which is why the light now reports itself and why
`led-state.py` exists.

| Do this | Expect |
|---|---|
| `can-up.sh --one-shot` + `cangen`, no phone | blue breathe — frames are arriving and going nowhere, which is the honest reading |
| a phone subscribed, nothing on the bus | a double flash every 2 s, on the session's hue — **not** darkness |
| connect RaceChrono, configure a channel | yellow with blips, panel `flt ALL`, **no** fault flash |
| start recording with three channels | green with blips, panel `flt 3` |
| `ble-load.py` past the 600–620 knee **while recording** | red overlay appears as `lost` grows, and **stays** after the ramp stops |
| unplug the CAN pair mid-session | the hue flatlines within 2 s |
| walk the phone out of range | blue breathe |
| press D0 three times | TFT + pixel, pixel, dark, TFT + pixel |

Nothing above needs the bike, and only four rows need the phone.

## Patch 17 — the supply voltage on the glass — **applied, flashed and read**

In Bluecan at `trunk`: a new `src/power/{power.hpp,power.cpp}`, `CONFIG_FUEL_GAUGE`
selected for the Reverse TFT board alongside `CONFIG_DISPLAY_TFT` and
`CONFIG_STATUS_LED`, and twelve columns at the right-hand end of the BUS page's top row.

The board has a **MAX17048** fuel gauge on the I2C bus at **0x36** — it is on the
[product's feature list](https://www.adafruit.com/product/5691) and had been sitting
there unused for the whole project. It reports cell voltage, state of charge, and the
rate the charge is moving at.

### What it can see, and what it cannot

The gauge watches the **BAT rail**: the 2-pin JST jack and the charger's output, which
are the same node. So it reads the battery's voltage when one is fitted, and the
charger's own output when one is not — which is the case on the bike today, since
[power is solved as USB 3 A](hardware.md#power) and the cell is optional.

It cannot see the 5 V from the bike's USB outlet at all. This board brings VBUS out to a
pad and to nothing else: there is no divider onto an ADC pin, and no variant `#define`
for one, so no amount of firmware reaches that rail. Measuring it would mean soldering a
divider from `USB` onto one of the free analog pins (A2–A5) — a hardware change, and
nobody has needed it yet.

### The part that was wrong, and what the board said

The first version of this patch showed the state of charge and a one-character marker
for the sign of the charge rate — `+` fed by the outlet, `-` running on its own battery —
on the reasoning that the sign answers "is the bike still feeding this thing" without
being able to see VBUS. The reasoning is sound. **The premise is not**: it needs a cell,
and the gauge cannot tell whether one is fitted. There is no presence bit, and with the
jack empty it models the charger's rail as though it were a battery.

What the board reports, on USB with nothing in the jack:

```
Supply: 4.095 V, 98.6%, -26.6%/hr
```

98.6% charged and discharging at 26.6%/hr, for a rail that has not moved by more than
3 mV in a quarter of an hour. Taken at face value the marker would have read `-`,
"running on its own battery", while the board sat on USB — backwards, in exactly the
configuration the bike uses today.

Two runtime tests for a missing cell were tried against the board, and the board killed
both:

| Test | Why it looked good | What happened |
|---|---|---|
| charge reading over 100% | a rail is not a cell and the model overshoots it | 101.9% for the first seconds after power-up, then 99.8–100.0% — it detects nothing in the window anyone is actually reading the panel in |
| a jittering voltage | a cell is a large capacitor and a rail is not | the first run moved 30 mV a sample and looked like a tell; the second sat at 4.097–4.098 V throughout. That was the rail settling, not a property of an empty jack |

So nothing is inferred. `CONFIG_CELL_FITTED`, off by default, says whether there is a
cell in the jack, and the panel shows the percentage and the marker only when it is set.
The voltage is shown either way, because the voltage is true either way — and the
voltage is what was asked for.

### On the panel

The BUS page's other eight rows already run to the edge and the footer has three spare
columns, so the readout goes where there was room: the tail of the top row, which is the
"what state is the hardware in" line and now reads

```
CAN 500k LISTEN-ONLY RUN            4.09V
```

with the voltage right-aligned to column 39, under the values on the rows below it. With
`CONFIG_CELL_FITTED` set it becomes ` 4.06V  82%+` instead — twelve columns either way,
and the state word before it padded to seven, the width of `BUS-OFF`, so the readout
lands on the same column whatever the bus is doing. A moving anchor is what strands a
neighbouring slot's glyphs at the old column; see the column-change note on `field()`.

Colour is the voltage, on 1S LiPo thresholds: green ≥ 3.70 V, yellow below it, red below
3.50 V. A charger rail with no cell on it sits comfortably in the green band, which is
correct — it means fed. A gauge that does not answer draws `bat n/a` in dim grey rather
than yellow: that is the absence of a reading, not a warning about the supply, and yellow
would read as a low battery from across a garage.

`DEBUG` builds print the whole of it on the 5 s stats cadence, including the two numbers
the panel suppresses and why:

```
Supply: 4.095 V, 98.6%, -26.6%/hr  (no CONFIG_CELL_FITTED: charge and rate are the charger's rail)
```

The console is the instrument the panel's own rule gets checked against, so it reports
raw and lets the reader apply the rule.

### Cost, measured on the board

Display task, on the 1 Hz rate tick — the same rule as everything else on core 0. One
sample is three 2-byte register reads, ~150 µs at 400 kHz.

| | Before this patch | With the gauge |
|---|---|---|
| Idle repaint | 421 µs | 445 µs |
| Display task stack free | 1912 B of 4 KB | 1912 B of 4 KB |

Both measured on the board this session, the first by flashing `trunk` at `171b415` and
reading it. **The 280 µs in [the table below](#change-detection-one-character-at-a-time)
is stale** — it predates the rows the panel has gained since, and the honest idle figure
is now ~420 µs with or without this patch. This patch is inside the noise of it.

Three details that are not obvious and are load-bearing:

- **`Wire.setTimeOut(10)`.** The core's default is 50 ms, and a chip that has stopped
  answering would otherwise hold the display task for a quarter of the BUS page's
  repaint interval, once a second, forever.
- **Hibernate off** (`HIBRT` ← `0x0000`). Left on, the gauge drops to one conversion
  every 45 s whenever the cell is quiet — exactly the condition under which someone is
  standing over the board wondering why the voltage on the glass is frozen. It costs
  about 20 µA on a board drawing tens of milliamps. Confirmed working: the reading moves
  on every 5 s stats line, which it could not do at one conversion per 45 s.
- **No floats.** 78.125 µV per LSB is exactly 5/64 mV, so the conversion is integer with
  nothing rounded away, in a build that has no floating point anywhere else.

`begin()` is called from `display::run()`, after that task drives `TFT_I2C_POWER` high:
the gauge sits on the rail that pin gates, so a `Wire` transaction before it is a read of
an unpowered chip. Three consecutive failed samples, not one, drop the readout to
`bat n/a` — one NAK on a bus that anything can be hung off should not blank a good
reading.

The gauge is deliberately **not** wired to the fault latch or the status light. Those
watch the chain — bus, queue, radio, phone — and a low battery is not a fault in the
chain; adding it would make the one red overlay mean two unrelated things.

### What is left

Flashed, and the voltage half is proven: it answers, it tracks, it does not hibernate,
and it costs nothing measurable. What is untested is everything behind
`CONFIG_CELL_FITTED`, because there is no cell to test it with. When one is fitted:

| Do this | Expect |
|---|---|
| build with `CONFIG_CELL_FITTED`, USB connected | voltage and a percentage that agree with the cell, marker `+` within a sample or two |
| pull USB, run on the cell | marker flips to `-`, and the voltage now genuinely falls |
| plug USB back in | marker back to `+` |
| a board with the gauge unreachable (SDA shorted, or QT power off) | `bat n/a` in grey after three seconds, nothing else on the page disturbed |

## Patch 18 — acknowledge, after listening — **applied and measured on the bench**

In Bluecan at `trunk`, commit `f5ef901`: `CONFIG_CANBUS_ACK` in
`src/racechrono-canbus.hpp`, `controller::supervise()` and `set_ack()` in
`src/canbus/controller.cpp`, one call from `loop()`, and a word on the BUS page's top row.

[Patch 2](#patch-2--listen-only-mode--applied) made the controller listen-only and called
it the one change that had to land before the device touched an R9. The R9 then turned out
to need the opposite: its diagnostic connector is a pair with only the ECU on it, and with
nothing acknowledging, the ECU resends one frame about 3 800 times a second and 2 ids of 23
get through ([what the R9 turned out to send](README.md#what-the-r9-turned-out-to-send)).
This patch is that opposite, built the way the CANable's version was
([can-up-ack.sh](../analysis/tools/can-up-ack.sh)): listen first, acknowledge only on
evidence, never send a frame, and get off the bus when it looks wrong.

### What it does

- **Every boot still starts listen-only**, with the TX pad a plain GPIO held recessive —
  patches 2 and 6, unchanged. A build without `CONFIG_CANBUS_ACK` never leaves that state.
- **It starts acknowledging once the bit rate is proved**: 32 frames received intact, over
  at least 200 ms, with no error interrupt among them. At the wrong bit rate not one frame
  gets through its CRC, so the count is the proof and the time only keeps a single burst
  from being the whole of it. The switch is a pass through reset mode — normal mode, both
  error counters cleared, the pad routed to the peripheral — and leaving reset mode the
  controller waits for eleven recessive bits, so it cannot come up acknowledging the
  middle of a frame.
- **It stops, until the next boot**, on any of three things, looked for every 10 ms: REC
  at 32 or more, the controller reporting error-warning or bus-off, or TEC moving at all.
  Stopping is the same pass in reverse, and the pad goes back to a GPIO held high.
- **What reaches the wire** is the ACK bit and, over a frame received broken, an error
  flag. Nothing in the firmware writes the TX buffer or issues a transmit command, which
  is why a TEC other than zero is treated as a trip of its own: it only counts for a
  transmitter.

The trip latches because listen-only is what this bus has whenever nothing is plugged into
the connector. Falling back to it costs the rider the telemetry and the bike nothing;
rejoining a bus that has just looked wrong is the half of that trade not worth making.

### On the panel, the light and the console

The BUS page's top row reads `CAN 500k LISTEN-ONLY`, `CAN 500k ACK`, or `TRIP-STATE`,
`TRIP-REC`, `TRIP-TEC` in red. A trip also arms the status light's fault flash: on the R9
it takes the bus from 23 ids to 2, and the hue alone would go on saying frames are moving.
Both were confirmed by eye after the trip test below. A `--debug` build prints
`Bus mode:` with TEC, REC and the status and mode registers on the stats cadence, and one
line at each change of state.

### The error counter had been stopping at one

The ISR counted bus-error interrupts and never read the error code capture register, and
reading it is what re-arms that interrupt. So it fired once per boot and `err` on the
panel could not pass 1 — which is also why [patch 4](#patch-4--bit-rate-prescaler--applied)
describes a controller at the wrong bit rate raising "a single bus error". Survivable
while the counter was a readout; not once "no error since" is part of the proof. The ISR
reads the register now, and the counter counts.

### Measured, 2026-10-13

Bench: the CANable in normal mode with retransmission on — not `--one-shot` — and
`cangen can0 -g 5 -I 123 -L 8 -D i`, 200 frames a second.

| | |
|---|---|
| Cold boot, then traffic | `listen-only` → `acknowledging` after **720 frames** |
| Received while acknowledging | **199–202 msg/s**, 0 errors, 0 dropped, TEC 0, REC 0 |
| The CANable meanwhile | error-passive for the first fraction of a second, then back to ERROR-ACTIVE with both counters at 0 |
| Frames on the wire sent by the Feather | **none** of about 1 880 — every one in `candump -x` is the CANable's own |
| The CANable moved to 250 kbps, Feather acknowledging | **tripped on REC**, back in listen-only, mode register reading listen-only |

Three things the runs said that the design had not:

- **The proof time was 500 ms, and that lost a race with the bench.** Candlelight gives up
  an unacknowledged transmission after about 440 ms and reports `tx-timeout`. So the board
  proved the bit rate on the flood of retries, switched, and was acknowledging a bus that
  had just gone quiet — `acknowledging`, 0 msg/s, 0 errors, and a status register that
  said idle. 200 ms wins the race. The R9's ECU never gives up, so only the bench cares.
- **The REC limit does not bound a bus that turns bad at once.** Against the 250 kbps
  transmitter REC was already **128** when `supervise()` looked — at the 100 ms poll this
  started with and at 10 ms alike. What bounds that case is the controller, which stops
  sending dominant error flags once it is error-passive; the supervisor then takes it off
  the bus within a poll. The limit of 32 is for a bus that degrades slowly.
- **About 250 error interrupts arrive before the switch, none after.** The log line at the
  switch counts them: 246 before the 720 frames. They belong to the start of the
  unacknowledged flood and were not chased further.

### What changes on the bench

Bluecan acknowledges the CANable now, a fraction of a second into the first traffic after
a boot. Everything in this file and in [README.md](README.md) about nothing acknowledging
the CANable, the flood of duplicates, and `--one-shot` being needed to get distinct frames
out describes the board **before this patch** or in the listen-only moment before the
switch. After it, the bus is an ordinary acknowledged one and `--one-shot` is not needed.

One thing that cost three runs and is not this patch's doing: **a board that receives
nothing at all, with zero errors, while the CANable sits at TEC 128, is `CAN H` and
`CAN L` crossed** — here, a female pigtail built the other way up. The unpatched firmware
read the same zero, which is how the patch was cleared.

### What has not happened

It has not been on the R9. That is the next thing this patch is owed, and what it should
show is on the BUS page: `ACK`, the IDS page counting 23, and REC at 0.

## Patch 19 — the firmware decodes four frames, and shows them — **applied and measured on the bench**

In Bluecan at `trunk`, three commits: `bd0d51a` for the decoder (`src/canbus/r9.hpp`),
`826263e` for the latch (`decoder::observe()` and `latest()`, implemented in
`decoder_yamahar9.cpp`, fed from the ISR in `controller.cpp`), and `1a45b7d` for the page
(`page_live()` in `src/display/display.cpp`).

Until now this firmware forwarded raw frames and understood none of them, on purpose:
RaceChrono decodes app-side, so an unfinished mapping never blocked the device. That is
still how the phone gets its data and nothing here touches it. What changes is the panel.
With [three captures read](../analysis/r9-signals.md), the device can show what the bike
is sending in the bike's own terms, to be held up against the dash at a standstill —
which is the check a raw-frame page cannot give.

### The decoder

`r9.hpp` is ten pure functions over four frames, written by hand from
[r9.dbc](../analysis/r9.dbc): engine speed, gear, TCS and the mode field from `0x27C`; the
two temperatures from `0x512`; the grip and the measured throttle valve from `0x758`; the
commanded valve from `0x759`. Integer results in the units the panel prints — rpm, tenths
of a degree, tenths of a percent.

Four ids and not six. `0x751`'s neutral byte adds nothing, since gear 0 in `0x27C` is
neutral already, and `0x76E` is a copy of the mode field. The two bits that change with
the engine running are left out by decision; the fields with no name are left out by rule.

A second hand-written copy of the DBC's equations is a thing that drifts, and the panel
is where a wrong one would be believed. So
[tools/r9-decode-check.py](firmware/tools/r9-decode-check.py) compiles the firmware's own
header on the host and decodes every distinct payload in the captures both ways:

| Capture | Distinct payloads | Worst difference from the DBC |
|---|---|---|
| `session-2026-10-11_115626` | 124 | under one unit of the firmware's resolution |
| `session-2026-10-11_130905` | 230 | the same |
| `session-2026-10-11_200550` | 4 239 | the same |

The differences are the firmware truncating where the DBC gives a float. A copy of the
header with two equations deliberately broken fails on the first payloads it meets, which
is the only reason to believe the pass.

### The latch, and why it is ahead of the filter

The census and the LAST page are fed from the drain task, after the send, so they see what
was *forwarded*. Under a recording RaceChrono that is the allow-list's choice: every id
without a channel is dropped in the ISR and the rest are thinned to the app's 50 ms. A
decoded page fed the same way would show the temperature only to a rider who had defined a
temperature channel.

So the decoder is shown every standard data frame **before** it is asked whether to
forward it, and the R9 decoder keeps the latest payload of its four ids. Each sits behind
a sequence lock: the ISR on core 1 never waits, and the display task on core 0 reads again
if it caught a write in progress, so it cannot pair the two bytes of the engine speed from
different frames. It costs every other frame four comparisons, and the ISR's data copy now
happens before the filter instead of after it — up to eight register reads on a frame that
is then dropped.

Whether this costs the radio anything was asked before it was built. It should not: the
decoding itself happens in the display task, at the repaint rate, and the per-frame work
is in the ISR on core 1 while the BLE host lives on core 0. Switching it off with the
panel was considered and left for the day a measurement asks for it.

### Measured, 2026-10-13

There is no bike on the bench, but there is its traffic: twenty seconds of
[the third capture](../analysis/captures/session-2026-10-11_200550/README.md) — 9 861
frames, all 23 ids, ending just after the highest engine speed in it — replayed through
the CANable with `canplayer`. It is also the first time
[patch 18](#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench)
has acknowledged a bus shaped like the R9's.

| | |
|---|---|
| Received | **~524 msg/s**, 0 dropped, 0 errors once acknowledging, TEC 0, REC 0 |
| Held when the replay stopped | rpm 2151, gear 0, TCS 7, mode 3 · water 31.8 °C, air 21.8 °C · grip 0, valve 13.1 % · commanded 14.5 % |
| What the last frame of each id decodes to | **the same, every value** |
| Ages while it ran | 2–9 ms on `0x27C`, 10–37 ms on `0x758`/`0x759`, under a second on `0x512` — their 100, 20 and 1 Hz |
| Ages after it stopped | counting up from the stop, 3 s and then 8 s at the next two readings |

**Not checked: that it holds under a phone's allow-list.** No phone was connected, so the
filter stood at allow-all throughout. The latch is ahead of the filter in the code; the
run that shows it is a recording session with a channel missing.

One thing the replay showed that was not the latch's: **the census was too small for this
bike.** It had 20 slots and the R9 sends 23 ids, and the console said so —
`Census ids: 20, 120 over`. Fixed with the page, below.

### The LIVE page

The page the board boots into, and the first in **D1**'s order: it is the page about the
bike, and the other three are about the device.

```
bluecan advertising
Gear 3     RPM  8450
Grip  42%  Valve  38%
Water 76C  Air   17C
TCS 7      Mode 3
Valve commanded  14.5 %
```

Four lines at text size 2 and one at size 1. Labels are yellow, capitalised, at fixed
columns and drawn once; each value is white and padded to a fixed width. `Gear` reads `N`,
`1`–`6`, and `-` between gears. `Mode` is the number the bike sends and nothing more — it
follows the ride mode, and [what it is](../analysis/open-questions.md) is still open. The
two bits that change with the engine running are not on it, by decision.

**A value that has stopped arriving turns to grey dashes** — after half a second for
`0x27C`, `0x758` and `0x759`, three seconds for `0x512`, which comes once a second. A
number that has stopped arriving looks exactly like one that has stopped changing, and with
the key off that would leave the last engine speed standing on the glass.

The census grew from 20 ids to 32 with it, and the IDS page went from two columns of id,
total and rate to three of id and rate — 24 cells, one more than the R9 sends. The total
went because the count is what that page is read for now: 23 means the controller is
acknowledging, 2 means it is not.

### The page, measured and looked at

The same replay, with the page on the glass, and then a longer one in a loop: 150 s of
gear changes from [the first capture](../analysis/captures/session-2026-10-11_115626/README.md),
which holds `0x27C` and little else, followed by the 20 s above.

| | |
|---|---|
| A repaint of LIVE while values move | **2–4 ms**, 2–5 characters changing |
| A repaint with nothing changed | **135–180 µs** |
| First paint of LIVE | ~90 ms; the worst page change of the session, onto the BUS page, 254 ms |
| Dropped at ~524 msg/s with the page running | 0 |
| Census | 23 ids, none over |
| Display task stack free, after every page had been visited | **1824 B** of 4 KB, from 1912 |

Looked at on the board by the rider: readable, nothing overlapping, no stale characters
left behind, and all four pages cycle. The BUS page read `CAN 500k ACK`.

**The values vanished for a moment every half minute or so, and that was the bench.**
Gear, RPM, TCS and Mode all went to dashes and came back. The replay file has no gap in it
longer than 0.2 s. Timed on the host with a clock that does not step, the frames on `can0`
had five silences of **1.9 s** in 175 s — at gears 6, between-gears, N, 1 and 2, so nothing
about the data — and across each one the realtime clock had advanced 0.01 s. `canplayer`
paces itself on the realtime clock, WSL2 steps that clock back, and `canplayer` then waits
out the difference with nothing on the bus. The page was doing what it was built to do
with half a second of silence. candump's timestamps ride on the same clock, so a replay
through `canplayer` on this machine is good for values and no good for timing.

### What has not happened

It has not been on the R9, and it has not run under a phone's allow-list. The first is the
check the page exists for: every value on it against the dash.

## Open questions before the bike

Written down in the order they would hurt. The two that headed this list became patches
6 and 7; what is left is quality, and one thing that simply has not been tried yet.

1. ~~**The drain task runs at `tskIDLE_PRIORITY`**~~ — answered by patch 12, and it was
   the answer. Raised one level, with the loop made to block so the idle task still runs,
   the link is clean to ~590 msg/s instead of ~200 and the queue stops dropping frames
   entirely. The BLE stack was not starved in the trade: the NimBLE host task runs far
   above either of these priorities. What is left is inside it.

2. ~~**The drop, peak, extended and remote counters only exist in `DEBUG` builds**~~ —
   answered by patch 9. They are on the glass now, in a release build, where they are
   visible during a real session. `stats()` is still `DEBUG`-only and still the fuller
   view; the panel carries the numbers that would otherwise go unnoticed.

3. ~~**BLE is unproven end to end.**~~ Answered: see
   [The phone session](#the-phone-session--run-and-it-works). What replaced it was a harder question — the
   link was clean only to about 200 msg/s against the R9's ~1050, and whether that was
   Bluetooth LE's limit or ours was exactly open question 1 above. It was ours:
   [patch 12](#patch-12--the-drain-tasks-priority--applied) moved it to **~590 msg/s
   clean, ~718 at 0.6% loss**. What is left of the question is narrower and lives inside
   NimBLE — the mbuf pool, which is baked into the Arduino core's precompiled libraries
   and is [deliberately not being touched](../journal/next-without-the-bike.md#explicitly-not-doing):
   the allow-list made it moot.

4. ~~**A `DEBUG` build with nobody reading the console appears to stall core 0.**~~ —
   answered by [patch 15](#patch-15--logging-without-interrupts-off--applied-and-measured),
   and almost every part of the question was wrong. It was not the unread console that
   stalled the board, it was the read one; the stall was not a stall but a panic and a
   reboot, invisible because ESP-IDF's console is UART0 on this build while `Serial` is
   TinyUSB CDC; and it was not `DEBUG`-only, because `bootln`/`errorln`/`warnln` compile
   into a release build too. The 1307-vs-~50 asymmetry that raised the question was an
   artefact: the console-open board was resetting its own high-water mark every few
   seconds. With the logger fixed, 919 msg/s gives a queue peak of 21 whether anything
   is reading or not.

## Display layer

None of the upstream projects drive a display; this is new code, and it lives in Bluecan
as `src/display/`. It is a port of the display task from
[bluecan_bench](firmware/bluecan_bench/), which is where both of its rules were measured
rather than guessed.

Hard rule: **the TFT must never be updated from the CAN path.** SPI writes to the ST7789
take milliseconds; doing that on core 1 between frames drops CAN messages at 500 kbps.
The measurement behind the rule is worse than "milliseconds" suggests: a full repaint of
the bench panel took **198 ms**, and called from `loop()` on core 1 it destroyed 1200 of
2000 frames while every error counter on both nodes read zero. The frames arrived and
rotted in the RX queue while the CPU pushed pixels. Full write-up in
[bench-link.md](bench-link.md).

So the task sits on core 0, one priority above the frame-drain task, and the panel has
exactly one owner: nothing else in the firmware holds a reference to it. It repaints the
BUS page at 5 Hz, the two census pages at 1 Hz, and polls the buttons at 40 Hz — a button
sampled once per repaint would miss a quick press entirely.

### Change detection, one character at a time

The bench sketch compared whole lines against what was on the glass, which turned a
198 ms repaint into ~160 µs while idle. That is not enough here, for a reason that only
appears once the panel reports its own cost: **the line showing the repaint time changes
on every pass**, so line-level detection had it paying 24 ms to report that it had paid
24 ms.

Bluecan diffs characters and redraws only the runs that differ, positioned by cursor.
The same fix makes a ticking counter cost what it actually changed — three digits, not
forty columns. Values that would otherwise churn every pass (the repaint cost itself) are
sampled once a second, so the panel never repaints because it repainted.

Measured on the board, at 500 kbps with no frames dropped:

| | |
|---|---|
| Idle pass, nothing changed | **280 µs** — stale; ~420 µs on the panel as it stands, [measured 2026-08-22](#cost-measured-on-the-board) |
| One character redrawn | **~650 µs** |
| A pass while the counters tick | 2–6 ms |
| First paint after boot, or after a page change | **245 ms** |
| Display task stack | 4 KB, 1912 B still free when [measured 2026-08-22](#cost-measured-on-the-board); 1824 B with the LIVE page, [numbers.md](numbers.md#the-panel) |

650 µs per character is the cost of Adafruit_GFX rendering text a pixel at a time; it is
why the answer is to draw fewer characters rather than to draw them faster. The 245 ms
first paint is the same number the bench measured for a full panel, and it is why the two
census pages refresh at 1 Hz: nearly every character on them changes each time, so at
5 Hz the LAST page alone would spend half of core 0 on SPI to show a human something they
cannot read that fast.

### Pages

**D1** cycles them. LIVE is first and the default since
[patch 19](#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench);
the other three are the sniffer-oriented pages this section was written about, built while
the mapping was still unknown.

- **LIVE** — what the R9 is sending, decoded: gear, engine speed, grip, throttle valve,
  water and air temperature, TCS, the mode field, the commanded valve. Dashes for anything
  that has stopped arriving
- **BUS** — frames received and forwarded with their rates, BLE frames and rate, queue
  depth and peak against capacity, dropped frames, error interrupts, the discarded 29-bit
  and RTR classes, TEC/REC, free and minimum heap, display stack, repaint cost, uptime,
  and the supply voltage ([patch 17](#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read))
- **IDS** — the ID census, each id with its rate, three columns of eight. It was the first
  question the device existed to answer — which CAN family the R9 belongs to is read off
  the ID list — and it is now the wiring check: 23 ids is a controller that is
  acknowledging, 2 is one that is not
- **LAST** — the six most recent raw frames, ID and payload

The header carries the one thing asked most often — whether the phone is actually
connected — on every page.

**D2** freezes the glass without pausing anything behind it, so a frame can be read off
the LAST page while the counters keep counting. **D0** cycles three positions — panel and
pixel, pixel alone, dark ([why three](#d0-is-a-three-way-now)) — and the middle one kills
the backlight and the repaint with it, which makes it the cheapest experiment on the
board: everything else keeps running, so any difference in the frame counters afterwards
is the display's cost and nothing else. Note **D0 is inverted** relative to the other
two — as the BOOT strapping pin it is pulled HIGH and reads LOW when pressed. See
[hardware.md](hardware.md).

### What feeds it

Counters come from the controller and the BLE device through the accessors added in
patch 8. The ID census and the frame ring are fed from the core 0 drain task, after the
frame has gone to RaceChrono — the phone is the product, the census is the instrument,
and the instrument never delays the product.

One consequence worth knowing: the census counts what is *forwarded*, not what is
*received*. With the R9 decoder they are the same thing, since it has no allow-list, but
if RaceChrono ever sends a deny-all the census goes quiet while the BUS page's frame
counter keeps climbing. That difference is visible on the BUS page, which is why both
numbers are on it.

Boards without a panel compile the module to no-ops; only the Reverse TFT defines
`CONFIG_DISPLAY_TFT`. Verified by building Bluecan for the plain Feather ESP32-S3.

## RaceChrono app side

Channels are configured **in the app**, not in the firmware: CAN ID, bit offset,
length, byte order and a conversion equation are all entered per channel in
RaceChrono Pro. The firmware just streams raw frames.

The practical consequence for this project: the empty and 🟡 rows in
[pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md) do not block the device. Build it, capture
raw traffic, correlate afterwards, and enter equations in the app as they get
confirmed. Where the mapping lists two conflicting formulas for the same signal (RPM,
TPS), both can be set up as separate channels and compared against the dash reading
in a single session.

### The wire format, checked rather than assumed

Confirmed against [aollin's README](https://github.com/aollin/racechrono-ble-diy-device),
which is the authoritative description of the protocol, and it matches what Bluecan
sends byte for byte:

| | |
|---|---|
| Service | `0x1FF8` |
| `0x0001` — frames, notify | bytes 0–3 the 32-bit CAN id **little-endian**, bytes 4+ the payload |
| `0x0002` — filter, write | `0` deny all; `1` + 2-byte interval allow all; `2` + 2-byte interval + 4-byte id, **big-endian**, allow one |

The byte order genuinely differs between the two characteristics — the spec calls the
notification id out as "a little-endian integer, unlike other values in this API" — and
Bluecan has it right in both directions, by accident of ESP32 endianness in one and
explicit shifting in the other. Worth having verified before spending a session
wondering why a channel reads garbage.

`frame` is laid out so the notification is a pointer into it rather than a copy:
`info` first, then `id`, then the payload, packed. `&f.id` is therefore already the
notification's first byte, and `sizeof(uint32_t) + dlc` its length.

### The phone session — **run, and it works**

Ran on 2026-08-21 against an Android phone with RaceChrono Pro. Frames reach the app:
**8018 notifications, 0 lost, 0 errors**, with a channel in the app tracking the bench's
synthetic sweep from 0 to 1000 and back. That closes the last unproven link in the chain
— CANable → twisted pair → Feather → queue → BLE → phone → channel — and it is the first
time anything has connected to this device at all.

What the session actually cost was three surprises, all of them written up below: a
checkbox in the app that silently prevents subscription, a channel editor that works
nothing like this document assumed, and a frame counter of ours that read double.

Everything below is bench work — no bike, no pigtail — and it is worth doing in this
order, because each step removes an unknown the next one would otherwise inherit.

**What is needed:** RaceChrono **Pro** on Android or iOS. The DIY CAN reader is a paid
feature; the free build will not add the device. The board advertises as
`RaceChrono <last 3 bytes of the BLE MAC>` — on this board, **`RaceChrono 74:C5:D9`** —
and it is a plain BLE peripheral: no pairing, no PIN, and it should be added from inside
RaceChrono rather than from the phone's Bluetooth settings, where it will either not
appear or connect uselessly.

**Untick "Monitor" when adding the device.** RaceChrono asks which of three capability
groups the device provides — GPS (`0x0003`/`0x0004`), CAN-Bus (`0x0001`/`0x0002`) and
Monitor (`0x0005`/`0x0006`). This firmware provides **only CAN-Bus**. Monitor is the
reverse direction, where RaceChrono pushes decoded values back to the device to display;
it belongs to the feature the BMW decoder served, and patch 3 removed it.

Ticking Monitor is what cost this session its first half hour, and it fails in the worst
possible way: the app connects, shows no error, and never subscribes. On the board that
is `bluecan BLE` rather than `bluecan SUB`, with `nosub` climbing and `sent` frozen at
zero — which is exactly the reading patch 10 exists to produce, and the only thing that
distinguished it from a dead link.

1. **Connect.** In RaceChrono: add a device, choose the DIY/other CAN-Bus reader kind,
   scan, pick `RaceChrono XX:XX:XX`. Watch the board's header, not the phone:

   | Header | Meaning |
   |---|---|
   | `bluecan adv` | advertising, nothing connected |
   | `bluecan BLE 30.0ms` | connected and **not subscribed** — the app is attached and will receive nothing |
   | `bluecan SUB 15.0ms` | subscribed. Frames are flowing |

   The middle row is the failure this instrumentation exists for: it looks like success
   on the phone and on every other counter.

2. **Push traffic through it.** With `can0` up, run the sweep below or plain `cangen`,
   and read the BUS page. `ble` should track `fwd`. If `lost` climbs, the link is
   refusing frames — which is a real answer, and the interval on the header is the
   reason.

3. **Find the ceiling.** Raise the CANable's rate until `lost` starts moving, and record
   the rate and the interval it happened at. That number decides whether the R9's
   ~1050 msg/s needs a filter, a rate limit, or nothing at all. It is also the first
   thing that would justify honouring RaceChrono's per-id notify interval, which the
   firmware currently parses and ignores.

   **Measured.** [tools/ble-load.py](firmware/tools/ble-load.py) drives the ramp: it
   bounces `can0`, runs `cangen` at a given gap, and reads the board's cumulative
   counters before and after each burst. With the phone connected and subscribed:

   | Bus offers | BLE `offered`/s | `lost` | ISR `dropped` | Loss |
   |---|---|---|---|---|
   | 49 msg/s | 48.3 | 0 | 0 | — |
   | 99 msg/s | 97.8 | 0 | 0 | — |
   | 140 msg/s | 138.8 | 2 | 0 | 0.1% |
   | 196 msg/s | 195.3 | 0 | 0 | — |
   | 321 msg/s | 319.7 | 118 | 0 | 2.5% |
   | 476 msg/s | 458.3 | 600 | 355 | 6.5% + 3.9% |

   **Superseded by [patch 12](#patch-12--the-drain-tasks-priority--applied), which is
   what this measurement was taken to justify: the same ramp on the same phone is now
   clean to ~590 msg/s and loses nothing at 476. The table below is the *before*, and it
   is left as it was read.**

   **Clean to about 200 msg/s, and it degrades gracefully rather than falling off a
   cliff.** Refusals appear around 320 and grow; at 476 the host refuses 6.5% with
   `last_error` reading 6, `BLE_HS_ENOMEM` — out of mbufs, exactly the congestion the
   counter was added for — and the frame queue starts dropping on top of that. Delivered
   throughput is still climbing at that point: roughly 430 frames a second reach the
   phone.

   The R9's bus is **~1050 msg/s**. So forwarding every frame raw, which was the premise
   the sniffer-first design rested on, is short by a factor of about five if the
   requirement is losing nothing, or about two and a half if a few percent is acceptable.

   That premise is retired: sniffing belongs to the CANable and this device carries
   telemetry, so what has to fit down the link is an allow-list of a few ids rather than
   a whole bus. See [the bluecan README](README.md#sniffing-is-the-canables-job-not-this-one).

   Two things this does *not* say. First, it is not a verdict on Bluetooth LE: at a 15 ms
   interval there are 67 connection events a second, so even 460 notifications is seven
   per event, and the two suspects upstream of the radio are both already on the list —
   the drain task at `tskIDLE_PRIORITY` on core 0 beside the BLE stack (open question 1),
   and NimBLE's `notify()` copying the value into a `String` on every call. Neither has
   been isolated, and open question 1 asked for a before-measurement before touching the
   priority. This is it — and the first of the two suspects turned out to be worth most
   of the ceiling. See [patch 12](#patch-12--the-drain-tasks-priority--applied).

   Second, back-to-back bursts are worse than the table suggests. An earlier run that
   went straight from 196 to 476 msg/s without letting the queue drain filled it
   completely and dropped **912** frames rather than 355. Sustained overload compounds,
   which is what the 2000-frame queue was sized for and also why it is not a solution.

   A separate observation, and a useful one: **the serial counters stop printing
   entirely under saturation**. `RCDEV.stats()` is called after the inner drain loop, and
   with the queue permanently non-empty that loop never exits, so core 0 never gets back
   to reporting. `ble-load.py` reports it as the `blocks` column, which falls to zero.
   The panel keeps updating throughout, being on its own task on core 0 — which is the
   reading that survives exactly when it matters.

   **A correction worth recording, because it nearly became a finding.** The first pass
   at this measurement reported a hard ceiling of ~200 msg/s with the device apparently
   unable to go faster. That was the measuring instrument, not the device: the script
   slept through each burst without draining the serial port, so the OS buffer filled and
   the "after" read returned lines from *before* the burst. The deltas were tiny and the
   conclusion was believable, which is what made it dangerous. Drain the console
   continuously or read nothing.

   Two more readings worth carrying forward:

   - **the negotiated interval settled at 15.00 ms**, the top of the 7.5–15 ms window
     patch 11 asked for at the time, and it renegotiated during the session between
     12.5 ms with latency 1 and 18.75 ms with latency 0. Android takes the request as
     advice. 15 ms is a few hundred frames a second at best, against the R9's ~1050
   - **the MTU was never negotiated** — it reads 0, so the link is on the 23-byte
     default and a notification carries 20 bytes. A frame is at most 12, so nothing
     truncates, but there is no headroom for packing several frames per notification
     should that ever become the answer to the rate problem

4. **Validate a channel against a known signal.** `tools/sweep.sh` puts a triangle wave
   on `can0` whose true value the bench chose.

   **Where the channel lives:** in the *vehicle profile*, not in the device settings, and
   that screen has **two** `+ Add channel` buttons. The one under *OBD-II channels* is for
   ELM327 queries and is the wrong one; the one under *CAN-Bus channels*, at the bottom,
   is right. The same screen has a *CAN-Bus settings → Test connection* toggle, which is
   the fastest way to confirm the link without defining anything.

   **The channel editor has no offset, length or byte-order fields** — an earlier version
   of this document assumed it did, and it does not. There are four things: a **Channel**
   to map onto (via *Show more…*), a **PID**, which is the CAN arbitration id in hex, an
   **Equation**, and a **Source data** row that shows the live bytes for that PID when
   *Use live data* is ticked, with the equation's result under **Solution**.

   That live row is the useful part, and it is worth reaching before writing any
   equation: as soon as the PID matches, the zeros come alive with real bytes. Seeing
   them is what proves the chain end to end. Seeing them stay zero means the PID is
   wrong, not that nothing is arriving.

   Offset, length and byte order all live in the equation instead, via
   [RaceChrono's functions](https://racechrono.com/support/equations) — offsets are
   zero-based, and `raw` is the frame payload:

   ```
   ./tools/sweep.sh --hz 10
   ```

   | Bytes | Content |
   |---|---|
   | 0–1 | 16-bit **big-endian**, 0…1000 |
   | 2–3 | 16-bit **little-endian**, 0…1000 |
   | 4 | 8-bit, 0…250 |
   | 5 | 8-bit, 250…0 — falls while the rest rise |
   | 6–7 | `A5 5A`, constant — a value that must never move |

   | Equation | Reads | Expected |
   |---|---|---|
   | `bytesToUint(raw, 0, 2)` | bytes 0–1, big-endian | 0…1000 and back |
   | `bytesToUintLe(raw, 2, 2)` | bytes 2–3, little-endian | the same number |
   | `bytesToUint(raw, 6, 2)` | bytes 6–7 | `42330`, never moving |

   The three together are the whole test: the first two must agree with each other,
   which settles byte order, and the third must not move, which settles that the frame
   is not being read at an offset. `A5 5A` is in the payload for exactly that reason —
   it is the marker that says *this is my signal and I am reading it correctly*.

   Getting `bytesToUint(raw, 0, 2)` to sweep 0…1000 is the pass condition, and it was
   met. Afterwards, a wrong reading on the bike is the CAN mapping's fault and nothing
   else's — which is the whole point, since [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md)
   has no 🟢 rows to check against yet.

   The script bounces `can0` when it goes error-passive. That is not a fault: the
   Feather is listen-only, so nothing on this bench ACKs the CANable. See
   [bench-link.md](bench-link.md).

## Order of work

1. Flash a bare TWAI loopback/self-test sketch — proves the board, the controller and
   the transceiver wiring before any of the RaceChrono complexity is in play.
   **Done**: [firmware/bluecan_bringup](firmware/bluecan_bringup/) reaches `CAN READY`
   both over a bare jumper between A0 and A1, which isolates the MCU from any wiring
   mistake, and through the SN65HVD230. `Rs` needed no attention on this module —
   Kamami tied it down and does not expose the pin, so the usual floating-`Rs` standby
   trap cannot happen here.
2. Build the bench CAN node — **done**: the CANable is up as `can0` at 500 kbps and
   passes its acceptance test.
3. Join the two over the twisted pair and prove frames cross — **done**: both directions,
   mutual ACK, 10000 of 10000 frames at a saturated 4078/s, and listen-only demonstrated.
   See [bench-link.md](bench-link.md). [bluecan_bench](firmware/bluecan_bench/) is the
   sketch, and it is also the harness for everything below.
4. Fork joeroback, apply patches 1–13, confirm frames reach RaceChrono over BLE.
   **CAN half done**: Bluecan exists, patches 1–3 are in, patch 4 came out of running
   it, and the board is flashed with it. It receives — 359 frames/s off the CANable, all
   of them forwarded to the RaceChrono path — with the controller in listen-only and the
   CANable falling to ERROR-PASSIVE because nothing ACKs it. **The BLE half is done
   too**: frames reach RaceChrono on a phone, a channel tracks the bench's synthetic
   sweep, and the link's ceiling is measured. See
   [The phone session](#the-phone-session--run-and-it-works).

   Bench note for repeating this: the CANable is the only transmitter here, so nothing
   ACKs *it* either. It reaches ERROR-PASSIVE after ~1800 frames and the traffic stops,
   which is listen-only working rather than a fault. Bounce the interface
   (`ip link set can0 down`, then up) for another burst, and expect `cangen` to wedge on
   a stopped TX queue — kill it before the bounce. On the bike this cannot arise: the
   ECU, ABS and IMU nodes ACK each other.
5. Add the display task — **done**: ported from `bluecan_bench` as patch 9, with
   character-level change detection replacing the bench's line-level. Three pages, three
   buttons, ~420 µs an idle pass, no frames dropped. See [Display layer](#display-layer).
6. Define a channel in RaceChrono against a synthetic signal and sweep it, to validate
   the equation-entry workflow end to end. The signal exists now —
   [tools/sweep.sh](firmware/tools/sweep.sh) — and the procedure is written up under
   [The phone session](#the-phone-session--run-and-it-works).
7. *(blocked on pigtail)* Connect to the bike and work the priority list in
   [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md).

## Code review log

Moved to [journal/bluecan-code-review-log.md](../journal/bluecan-code-review-log.md).
