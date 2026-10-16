# Bluecan at the bike — the checklist

The first time Bluecan meets the R9. Everything it does has been shown on the bench
against a replay of the bike's own captures
([patch 18](history/firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench),
[patch 19](history/firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench));
this is the list of what only the bike can show. At a standstill throughout. Riding with
it is a separate decision and is not on this page.

It is not the capture checklist — that is the CANable's, in
[analysis/at-the-bike.md](../analysis/at-the-bike.md) and
[analysis/stand-session.md](../analysis/stand-session.md).

**What is different from the bench, and why it deserves care:** this board acknowledges
frames on the bike's bus, it is not isolated, and it shares the bike's ground. The CANable
did the first of those three and neither of the others.

## A. At home

| | Check | Passes when |
|---|---|---|
| 1 | The release build is on the board | it boots to the LIVE page with grey dashes, header `bluecan advertising`. A `--debug` build works too, but pauses 5 s at boot |
| 2 | **The KAmod's `120R` jumper is on** | a meter across `CAN H`/`CAN L` on the board, connected to nothing, reads about 120 Ω. The board acknowledges, so it is the bus's other end ([hardware.md](hardware.md#termination)) |
| 3 | The pigtail is wired `CAN H` to pin 2, `CAN L` to pin 5 | continuity from the KAmod's `CAN H` to the **middle cavity of the latch row**, and from `CAN L` to the middle cavity of the other row. The female half is the mirror image of the male; the middle cavities are the same on both, the outer ones are not |
| 4 | The +12 V wire, pin 4, goes nowhere | cut back or insulated. The board is fed from USB |
| 5 | The channels are in RaceChrono | [racechrono-channels.md](racechrono-channels.md); eight are entered, the commanded valve on PID 1881 is not. **Replace the TCS and PWR equations** with the ones corrected on 2026-10-16: the old TCS one reads 16 too high in gear with the engine running. The wheel speeds and the Track display are optional; each adds a PID |

**Bring:** a multimeter, the phone, the RaceBox, a **data** USB cable — a charge-only one
lights the board and cost three round trips on the bench. The laptop and the CANable if
there is room: they are the way to look at the bus if this goes wrong.

**Ground is a decision to make before leaving, not a measurement.** Fed from the bike's USB
outlet, the board shares the bike's ground through the outlet. Fed from a power bank it
shares nothing, and the transceiver then has no reference but the bus itself; in that case
wire pin 3 to the board's `GND`. Nothing in this repository has tried either on the bike.

## B. Plugging in — key off

| | Do | Passes when |
|---|---|---|
| 1 | Key off. Plug the pigtail into the red connector | |
| 2 | Meter across the pair with the board attached, before powering it | **about 60 Ω**: the bike's 120 Ω, measured with the pigtail alone on 2026-10-10, beside the board's. About 120 Ω means the `120R` jumper is off. Open means those are not the CAN wires |
| 3 | Power the board | LIVE page, dashes. **D1** to the BUS page: `CAN 500k LISTEN-ONLY RUN`, `rx 0` |

Whether the R9's USB outlet is live with the key off is not written down anywhere here.
If it is not, the board boots at step C1 instead, a second and a half after the key.

## C. Key on, engine off

No phone connected yet — the IDS count only means what it should without one.

| | Look at | Passes when |
|---|---|---|
| 1 | BUS page, top row | **`CAN 500k ACK RUN`**, green, within a second of the frames starting |
| 2 | BUS page, `rx` | about **506/s** |
| 3 | BUS page, `tec` and `rec` | both **0** and staying there |
| 4 | BUS page, `drop` and `err` | `drop 0`. `err` may have counted during the first moment, before the switch; it must not be climbing afterwards |
| 5 | IDS page | **`ids seen 23`** |
| 6 | The status light | breathing blue — nothing subscribed. **No red flash** |
| 7 | **The bike's own dash** | no warning light, no fault shown, that was not there before the board was plugged in |

Then the LIVE page against the dash, which is what the page was built for:

| | Do | LIVE shows |
|---|---|---|
| 8 | Neutral, then up through the gears and back | `Gear` reads `N`, `1`–`6`, with `-` between gears, matching the dash each time |
| 9 | Change the TCS setting | `TCS` follows the number on the dash |
| 10 | Go through the ride modes | `PWR` follows the PWR number on the dash in each mode. On the Track display `Trk` appears beside it |
| 11 | Compare the temperatures | `Water` within 2 °C of the dash; `Air` a degree or two above it |
| 12 | Grip closed, then to the stop | `Grip` 0 %, then about 100 %. It reads 0 for the first twentieth of the travel |
| 13 | Bottom row, small | `Valve cmd` shows a number, not dashes |
| 14 | `Rear` and `Front` | `0` with the wheels still. On a stand, a wheel turned by hand shows a few km/h on its own field |

## D. Engine running

Outdoors or with the door open. This is the first time the board has acknowledged next to
a running engine, so the BUS page comes first.

| | Look at | Passes when |
|---|---|---|
| 1 | BUS page | still `ACK`, `rec 0`, `drop 0`, `err` not climbing |
| 2 | LIVE `RPM` against the tachometer | agrees at idle and at a steady 3 000–4 000 |
| 3 | LIVE `Valve` at a warm idle | about 8 % — the zero is provisional, and this is the figure it was set from |
| 4 | LIVE `Water` as it warms | follows the dash |
| 5 | Stop the engine, key off | every value on LIVE turns to grey dashes within half a second, the temperatures within three |
| 6 | Key on again | the values come back; BUS reads `ACK` |

## E. With the phone

| | Do | Passes when |
|---|---|---|
| 1 | Connect RaceChrono | header `connected`, then `SUB` with the interval; the light goes yellow |
| 2 | Start recording, with the RaceBox locked | BUS reads **`flt 3`** — one for each PID with a channel, so more if the wheel speeds or the Track display were added — and `lost 0`; the light goes green and blips |
| 3 | Compare the app with LIVE | the same values. `Digital 1` is TCS, `Digital 2` PWR |
| 4 | **Look at the Time tile and the `Hz` figures** | both filled in, now that the GPS has a lock — they were empty on the bench without one, and the reason was never confirmed. About 20 Hz for the channels on PIDs 636 and 1880, 1 Hz for the temperatures |
| 5 | Stop, and open the session | the CAN channels are in it, with data |

## Write down

The BUS page's `err`, `rec`, `peak` and `drop` at the end. Anything on LIVE that disagreed
with the dash, and by how much. What the Time
tile and the rates did. Whether the outlet is live with the key off.

## When something is wrong

| What it looks like | What it probably is | Do |
|---|---|---|
| BUS stays `LISTEN-ONLY`, `rx 0`, `err 0`, key on | the pair is crossed, or it is not the pair. This is exactly what a female pigtail built the other way up looked like on the bench | key off, redo B2, swap `CAN H` and `CAN L` |
| BUS stays `LISTEN-ONLY`, `rx` in the thousands, IDS shows 2 | frames arrive and the proof never completes — `err` will be climbing | key off. Note `err`. This has not been seen and wants the CANable on the bus to look at |
| `TRIP-REC`, `TRIP-STATE` or `TRIP-TEC` in red, red flash on the light | the board took itself off the bus and is listening only. It stays that way until it is restarted | note which one and the `rec` figure. Restart the board once. If it trips again, stop |
| A warning on the bike's dash | whatever it is, it was not there before | key off, unplug the board, key on and see whether it clears. Do not plug the board back in that day |
| The app shows nothing and the header says `connected`, not `SUB` | connected without subscribing — the phone, not the bike | disconnect and reconnect in the app |
| LIVE shows dashes for a moment and recovers | on the bench that was the replay stalling. The ECU does not stall, so on the bike it is a finding | note when, and what the BUS page's `rx` did |
