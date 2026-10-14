# The mapping as it stood before the bike, and the research behind it

> **Background, not the current state.** This is the part of the old `canbus-mapping.md`
> that was written before anything had been read off an R9: the Y-TRAC item table, the
> predictions from other Yamahas and from AiM's logger protocols, how far to trust them,
> the frame budget for the Bluetooth link, and the source list (`[S1]`–`[S7]`). The
> predicted ids did not appear on the diagnostic connector. What did is in
> [r9-signals.md](../r9-signals.md); the connector itself is in
> [connector.md](../connector.md). The text below is left as it was written, with its
> later annotations.

Goal: for every item in [riding-data-source.md](riding-data-source.md), determine how to
read it directly off the motorcycle's CAN bus — which frame (arbitration ID) it lives in,
which bytes/bits within that frame, the byte order, and the equation to convert the raw
bytes into the physical value shown in Y-TRAC Rev.

**None of the data below has been gathered from an actual R9.** Every 🟡 row comes from
older/other Yamaha models — Ténéré 700, MT-07, 2015+ R1, and since 2026-08-22 the AiM
logger protocols for the FZ-09/MT-09 platform ([S6]/[S7]) — that likely share ECU/CAN
architecture with the R9, but constants (and possibly frame IDs) may differ on the 2026 R9.
Nothing is 🟢 Confirmed yet.

**That stopped being wholly true on 2026-10-11.** The first capture off the R9 is in
[`captures/session-2026-10-11_115626/`](../captures/session-2026-10-11_115626/), and what it
showed — two IDs, neither of them predicted, and one confirmed row — is under
[The first capture](../captures/session-2026-10-11_115626/README.md#the-first-capture-2026-10-11) below. The paragraphs between here and
there were written before it and are left as they were. An hour later
[the second capture](../captures/session-2026-10-11_130905/README.md#the-second-capture-2026-10-11-the-adapter-acknowledging), with the
adapter acknowledging frames, turned the two ids into 23. In the evening
[the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running) ran the same 23 with
the engine started, against a recording of its sound and a video of a lean-angle app.

Two CAN "families" show up in the sources: the Ténéré 700 / MT-07 pair (8-bit signals,
IDs like `0x20A`/`0x216`/`0x236`) vs. the 2015+ R1 (16-bit signals). The working hypothesis
was that these are closer than that framing suggests — same rough ID space, just wider
fields on the more instrumented bikes.

**[S6]/[S7] confirm the hypothesis and say which side the R9 is on.** AiM's own protocol
definition for the FZ-09/MT-09 uses the *Ténéré/MT-07 IDs* (`0x20A`, `0x216`, `0x236`,
`0x23A`, `0x23E`) carrying *R1-style 16-bit fields* — and its TPS and APS scales
(×0.15 and ×0.145) are **identical** to the ones AiM uses for the 2015+ R1 at `0x215`.
Same signals, same scales, one ID apart. And AiM ships that one protocol,
`YAMAHA – FZ09MT09`, for MT-09/FZ-09 2014–2020 *and* 2021+ Euro5, MT-07/FZ-07 2021+ and
the YZF-R7 from 2022 — the R9's own platform, unchanged across the Euro5 cut. That makes
the `0x20A`/`0x216`/`0x236` set the thing to probe first on the R9, with 16-bit fields
expected rather than 8-bit ones.

The R1 source also confirms the bus runs at **500 kbps** (same as the MT-07 source, and as
AiM's cabling for both eras) and that the CAN network has distinct **dash, ECU, ABS CU,
IMU, and CCU** nodes, all reachable from the diagnostic connector — which on the R1 lineage
is in the tail, and on the R9's platform has moved under the seat (see below).

**That last part does not hold on the R9.** Its service manual, summarised in
[R9_CANbus.md](../R9_CANbus.md), gives the ECU three separate CAN pairs: one to the meter and
the BCM, one to the ABS unit and the IMU, and one to the diagnostic connector. The
connector reaches the third only, and what arrives there is what the ECU chooses to send.
The captures below agree: one terminator, nothing else acknowledging, and no IMU traffic.

**Current blocker**: no hardware logging is possible yet — no diagnostic-connector pigtail
in hand. [r9riders.com](https://www.r9riders.com/) has been searched (site search and
Google) for CAN bus/PID/diagnostic/OBD/Y-TRAC content — nothing indexed, so that lead is
exhausted for now, not just unexplored.
## Status legend

| Status | Meaning |
|---|---|
| 🔴 Unknown | Nothing gathered yet. CAN ID/bytes/equation are placeholders. |
| 🟡 Unconfirmed | Sourced from a forum, another model's DBC, a sniffing session, etc., but not verified against this bike. Treat with suspicion. |
| 🟢 Confirmed | Verified against a real R9, 2026, via logging + cross-checking against Y-TRAC Rev / dash readout. |

## Mapping table

| Displayed Name | Status | CAN ID | Start Byte | Length | Byte Order | Equation | Unit | Source / Notes |
|---|---|---|---|---|---|---|---|---|
| E/G-RPM | 🟢 Confirmed | `0x27C` (measured) / `0x20A` | 0–1 (measured) / 2–3 or **3–4** — conflict | 2 bytes | BE | `raw*100/256` (measured) — `raw*0.391` [S6] vs `raw/2.3` [S1] vs `B2*100+B3` [S3] | rpm | **Measured on the R9, 2026-10-11**: `0x27C` bytes 0–1 big-endian, ×0.3915 ± 1.5 % against the engine's sound between 1 440 and 1 635 rpm. **Exact form found 2026-10-12**: byte 0 is whole hundreds of rpm, as `0x750` sends them separately, so the scale is 100/256 = 0.390625, checked to 3 400 rpm — see [the unnamed fields against the manual](../open-questions.md#the-unnamed-fields-read-against-the-service-manual). [S3]'s `B2*100+B3` was nearly this all along: hundreds in one byte, the rest in the next. That is [S6]'s scale in a frame nobody predicted. `0x20A` itself was not on the diagnostic connector. ID agreed by everyone. **Byte offset is the open conflict**: [S1]/[S3] read bytes 2–3, [S6] puts a 16-bit field at bytes 3–4 — for the MT-07 too, so it is AiM-vs-forum, not model-vs-model. Scales: 0.391 (FZ09/MT09, YZ-F), 0.375 (FZ07), 0.4 (R1 `0x209`) — all one family, and 0.391 vs [S1]'s 1/2.3 = 0.435 is 11% apart. On the R9 the measurement above settled it for [S6]'s scale, and the rider has closed the question there. |
| Gear | 🟢 Confirmed | `0x27C` (measured); `0x236` predicted, not seen | 4 | 3 bits | — | `gear = raw & 0x07`, `7` = between gears | gear # | **Measured 2026-10-11**: 12 of 12 anchors, 1→6 and back to N, on the diagnostic connector — see [The first capture](../captures/session-2026-10-11_115626/README.md#the-first-capture-2026-10-11). `0x751` byte 6 (`0x60` = neutral) agrees. What follows is the prediction it replaced, kept because `0x236` may still exist on a bus this connector does not reach. ID and byte agreed by all four sources — the strongest cross-model match here. The encodings disagree about which bits: [S1]/[S3] say the top 3 bits (`0x00`=N, `0x20`=1 … `0xC0`=6, `0xE0`=between gears), [S6]'s FZ07 file says the high nibble, its FZ09/MT09 file takes the whole byte with no scaling. All three are the same bits read with different masks; shifting through the gearbox on a stand resolves it in one pass. R1 family puts gear elsewhere entirely — `0x209`, 3 bits at bit 29, `0`=N…`6`, per [S6]'s value table. |
| TPS | 🟡 Unconfirmed | `0x758` (measured) / `0x216` (R9 platform) / `0x215` (R1) | 0 high nibble with 2 (measured) / 1–2 | 12 bits (measured) / 2 bytes | LE per [S6], unverified | `raw12 / 16 / 144.4 * 100` (measured, zero provisional) — `raw * 0.15` [S6] | % | **On the R9, 2026-10-11, `0x216` is absent**; the throttle shows in `0x758` (15-bit and 12-bit fields) and `0x759` (8-bit). **Told apart on the evening of 2026-10-11**: the valve is `0x758` `b0hi:b2` (measured, 12 bits) and `0x759` `b0hi:b2hi` (commanded, 8 bits), in a unit that runs to about 144 with no known zero — see [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). Before that: not yet told apart as grip and valve — see [The throttle, field by field](../captures/session-2026-10-11_130905/README.md#the-throttle-field-by-field). **Upgraded by [S6].** AiM's FZ09/MT09 protocol has a 16-bit TPS at `0x216` bytes 1–2, ×0.15 — the *same scale* it uses for the 2015+ R1's `0x215` ECU_THROTTLE, and the same bytes [S4] named for the R1 by hand. The old 8-bit readings ([S1] `0x216` byte 0 raw `0x22`=0%/`0xCF`=100%; [S3] `raw/255*100`) belong to the pre-2021 bikes and are kept as the fallback if the R9 turns out to be 8-bit after all. [S6] labels it "deg" on the R1 and "%" on the MT-09 — the unit is not settled. |
| Water-Temp | 🟢 Confirmed | `0x512` (measured) / `0x23E` | 0 | 1 byte | — | `temp°C = raw*0.625 - 30` | °C | **Measured on the R9, 2026-10-11**: `0x512` byte 0, not `0x23E`, by this same equation — six dash anchors from 17 to 76 °C, all within 2 °C, see [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). **Independently confirmed.** [S3]'s `(raw-0x70)*0.625 + 40` is algebraically the same line as [S6]'s `raw*0.625 - 30`, from a vendor protocol that never saw that forum. Two unrelated sources, identical equation. The R1 family uses a different pair (`raw*0.66 - 36` at the same ID `0x23E`), so the constants are family-specific even where the ID is not. |
| Air-Temp | 🟢 Confirmed against the water sensor | `0x512` (measured) / `0x23E` | 1 | 1 byte | — | `temp°C = raw*0.625 - 30` | °C | **Found on the R9, 2026-10-11**: `0x512` byte 1. After a seven-hour cold soak it read the same raw value as the water sensor beside it, whose equation is held to the dash over 59 °C; the dash shows the air a degree or two lower than the equation gives (18 against 19.4–20, 16 against 17.5), and that difference is the dash's — see [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). One temperature, so the scale away from 17 °C rests on the two sensors being the same kind. Same confirmation as Water-Temp — [S6] gives air temp the identical scale and offset at byte 1, matching [S3]'s `(raw-0x30)*0.625`. **This retires the suspected typo**: the two channels share one equation, and it was only [S3]'s quoted valid-range bound (`0x21`) that looked inconsistent, not the formula. |
| Fuel | 🔴 Unknown | | | | | | L or L/100km | Not in any AiM channel list for this family, and no forum source has it. |
| Acc-X | 🟡 Unconfirmed | `0x250` (R1 family) | 1–2 | 2 bytes | LE per [S6], unverified | `g = raw*0.001 - 7` | g | **The ID [S4] never found.** [S6]'s R1 protocol puts ACCX at `0x250` bytes 1–2 and ACCY at bytes 3–4, ×0.001 with a −7 g offset. Caveat: this is the R1 family; AiM's FZ09MT09 channel list has no IMU channels at all, so either the MT-09 platform does not publish them or AiM does not decode them. The R9 has an IMU, so probe `0x250`/`0x254` regardless. |
| Acc-Y | 🟡 Unconfirmed | `0x250` (R1 family) | 3–4 | 2 bytes | LE per [S6], unverified | `g = raw*0.001 - 7` | g | Same frame as Acc-X — [S6]. |
| Lean | 🟡 Unconfirmed | Unknown | Unknown | 2 bytes (16-bit, guess) | — | Raw step = 5°/count exactly (quoted directly), offset unknown | ° | [S4] 2015+ R1 (SySt, post #18, 2015-08-24, exact quote): "the lean angle output is in increments of 5 degrees...not real precise." Broadcast at 100 Hz from the IMU node. **[S6] does not help here** — AiM decodes accelerometers and one gyro rate off this bus but no lean angle, so lean is still the least-sourced row on the page. **Untested hypothesis**: [S1]'s Ténéré 700 wheel-speed signal resolves *between* its coarse steps by varying the duty cycle of two adjacent raw values over successive 10ms frames (e.g. alternating 0/1 to represent ~9 km/h, between the 0 and 19 km/h steps). Given lean angle here is also coarsely stepped (5°) but broadcast fast (100 Hz), the same trick — reading the update *rate*/duty cycle between adjacent 5° steps rather than trusting the instantaneous raw byte — may recover sub-5° resolution. Worth testing specifically when logging the R9. |
| Pitch | 🟡 Unconfirmed | `0x254` (R1 family) | 5–6 | 2 bytes | LE per [S6], unverified | `deg/s = 300 - raw*0.01` | °/s | **[S4]'s open question answered by [S6]**: he wrote "the gyro values are 16 bits but I have no idea what the resolution is. Anywhere from 250 deg/s to 2000 deg/s is pretty standard." AiM decodes one 16-bit gyro rate at `0x254` bytes 5–6, 0.01°/s per count, offset +300 — i.e. ±300°/s full scale, so his low guess was the right one. Only *one* rate channel is decoded, not the three he saw, and which axis it is is not stated. |
| F-Speed | 🟡 Unconfirmed | `0x205` [S6] vs `0x264` [S4] — conflict | 1–2 | 2 bytes | LE per [S6], unverified | `km/h = raw*0.01` | km/h | Both sources are about the R1 family and they name different frames: [S4] read front/rear wheel speed out of the ABS module's `0x264`, [S6] decodes ECU_W_SPD_F/R at `0x205` bytes 1–2 and 3–4, ×0.01 km/h. Both may be right — the same speeds can be broadcast by the ABS node and rebroadcast by the ECU. [S6] answers what [S4] could not: the raw value is a scaled speed, not a pulse count. For the R9's own family the speed candidates are `0x20A` instead — see Related traffic below. |
| R-Speed | 🟡 Unconfirmed | `0x205` [S6] vs `0x264` [S4] — conflict | 3–4 | 2 bytes | LE per [S6], unverified | `km/h = raw*0.01` | km/h | See F-Speed — [S6], [S4]. |
| APS | 🟡 Unconfirmed | `0x758` (measured) / `0x216` (R9 platform) / `0x215` (R1) | 7, 6 low nibbles with 5 (measured) / 3–4 | 15 bits (measured) / 2 bytes | LE per [S6], unverified | `raw15 / 31672 * 100` (measured) — `raw * 0.145` [S6] | % | **Found on the R9, 2026-10-11**: `0x758` `b7lo:b6lo:b5`, 15 bits, 0 at rest to 31 672 at the stop, unit unknown — see [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). **Upgraded by [S6]**, exactly as TPS was: 16-bit handle-throttle position (AiM calls it PPS) at `0x216` bytes 3–4 on the FZ09/MT09 protocol, ×0.145 — the same scale AiM uses for the R1's ECU_TPS_HAND at `0x215`, and the same bytes [S4] named by hand. The TPS/APS pair sitting at bytes 1–2 and 3–4 of one frame is now agreed by a forum reverse-engineer and a vendor protocol independently. |
| PWR | 🟡 Unconfirmed | `0x27C` byte 4 top 2 bits, `0x76E` byte 1 (measured, unnamed) / `0x209` bit 38 (R1) / `0x20A` bit 61 (R9 platform) | 4 | 2 bits | — | identity | setting # | **A 2-bit field that changes with the ride mode is on the R9**, in both places at once: sport 1, street 2, rain 3, custom 1 0. It is the same width as AiM's "Mode". Whether it is PWR waits on the rider's PWR in each mode, and custom 2 has read two different values — both open, see the end of [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). [S6]: the R1 family carries ECU_POW_MODE as 2 bits at `0x209` bit 38; the FZ09/MT09 protocol has a channel simply called "Mode", 2 bits at `0x20A` bit 61, immediately after a 5-bit unnamed field. Whether "Mode" is the power mode, the ride mode or something else is not stated anywhere — cycling the setting with the bike on and watching those bits answers it. |
| TCS | 🟢 Confirmed | `0x27C` (measured) / `0x209` bit 25 (R1 family) | 3 | 7 bits | — | `raw >> 1` | setting # | **Measured 2026-10-11, twice**: `0x27C` byte 3 read `0x02`, `0x06`, `0x12` at TCS 1, 3, 9 in the first capture and `0x12`, `0x06` at 9 and 3 in the second — 5 anchors of 5, with every step visible on the way down. Bit 0 of the same byte is not part of it: it is set for about half a second as the engine starts, and what it stands for is not known. See [the second capture](../captures/session-2026-10-11_130905/README.md#the-second-capture-2026-10-11-the-adapter-acknowledging). [S6] R1 family: TCS_MODE, 4 bits at `0x209` bit 25, in the same frame as RPM and gear. Not present in the FZ09MT09 protocol, so treat as a candidate to hunt for rather than a mapping — see the caveat under [S6] below. |
| QS△ | 🟡 Unconfirmed | `0x215` bit 45 (R1 family) | — | 2 bits | — | identity | setting # | [S6] R1 family: ECU_QSS_SET. One 2-bit quickshifter setting, not an up/down pair — whether the R9's separate QS△/QS▽ split exists on the bus at all is unknown. |
| QS▽ | 🟡 Unconfirmed | `0x215` bit 45 (R1 family) | — | 2 bits | — | identity | setting # | Same single field as QS△ — [S6]. |
| SCS | 🟡 Unconfirmed | `0x215` bit 40 (R1 family) | — | 2 bits | — | identity | setting # | [S6] R1 family: ECU_SCS_SET. |
| LIF | 🟡 Unconfirmed | `0x215` bit 32 (R1 family) | — | 2 bits | — | identity | setting # | [S6] R1 family: ECU_LIFT_SET. |
| LCS | 🟡 Unconfirmed | `0x215` bit 34 (R1 family) | — | 2 bits | — | identity | setting # | [S6] R1 family: ECU_LAUNCH_SET. |
| EBM | 🔴 Unknown | | | | | | setting # | No match in any AiM channel list — engine brake management is not decoded for either family. |
| BSR | 🔴 Unknown | | | | | | setting # | No match in any AiM channel list. |
| TCS_flg | 🟡 Unconfirmed | `0x215` bit 61 (R1 family) | — | 1 bit | — | identity | bool | [S6] R1 family: ECU_TCS_IN — the intervention flags sit as single bits in byte 7 of the same frame as the settings: TCS bit 61, SCS 60, LIF 59, launch 58, plus front/rear ABS at bits 57/56. |
| SCS_flg | 🟡 Unconfirmed | `0x215` bit 60 (R1 family) | — | 1 bit | — | identity | bool | [S6] ECU_SCS_IN — see TCS_flg. |
| LIF_flg | 🟡 Unconfirmed | `0x215` bit 59 (R1 family) | — | 1 bit | — | identity | bool | [S6] ECU_LIFT_IN — see TCS_flg. |
| BSR_flg | 🔴 Unknown | | | | | | bool | No match in any AiM channel list. |
| LC | 🟡 Unconfirmed | `0x215` bit 58 (R1 family) | — | 1 bit | — | identity | bool | [S6] ECU_LAUNCH_IN — see TCS_flg. |

## How much to trust the AiM reading

[S6] is a different *class* of source from everything above it, and worth reading with its
own set of doubts.

**Why it outranks the forums.** It is a vendor protocol definition, not one person's
afternoon with a logger: AiM sells dataloggers that must decode these bikes correctly for
paying customers, and [S7] is AiM telling those customers which protocol to select for
which model and year. Where it overlaps a forum finding it agrees — the coolant and intake
temperature equations come out algebraically identical to [S3]'s, and the TPS/APS byte
positions come out identical to [S4]'s hand reverse-engineering of the R1. Two independent
methods landing on the same line is the strongest evidence on this page.

**What it is not.** It is a *conversion* of AiM's definitions by a third party, and the
conversion leaks:

- **Scales can be off by orders of magnitude.** The YZ-F frame yields "73 volts per count"
  and "59999.9 minutes per count". Where a scale is implausible, distrust the scale, not
  the ID.
- **Byte order is asserted, not established.** Every signal is marked Intel/little-endian,
  which is what a converter emits when it has nothing better to say. Yamaha's forum
  readings are big-endian. This does not matter for the 8-bit signals and matters a lot for
  the 16-bit ones.
- **Bit offsets are consistent but unverified.** The 8-bit signals land on exactly the
  bytes [S3] found, which is what makes the numbering trustworthy enough to quote; the
  16-bit RPM field still disagrees with the forums by one byte.
- **One frame is impossible as written** — `0x260` declares a 9-byte payload.
- **Channel lists are per-protocol, not per-bike.** AiM's own note says not all channels
  in a template are validated for every model or variant. The FZ09MT09 list has no IMU and
  no rider-aid channels at all, so every R1-family row in the table above is a *candidate
  to hunt for* on the R9, not a mapping to trust.

**What it does not answer.** Nothing here is from an R9, and AiM does not list one. It
supports the R9's platform siblings — MT-09/FZ-09 through 2021+ Euro5, MT-07 2021+, R7
2022+ — which is why the protocol is the right first guess, and why it is still a guess.

## How many frames this actually is

**Measured on the R9, 2026-10-11, and it replaces the arithmetic below.** The diagnostic
connector carries **23 ids at 506 frames a second**, engine off or running — not the
~1050 across 16 that the R1 thread led this section to expect. What the mapping has found
so far sits in four of them:

| ID | Rate on the bus | What is in it | Over BLE at 50 ms |
|---|---|---|---|
| `0x27C` | 100 Hz | engine speed, gear, TCS, the 2-bit mode field | 20 |
| `0x758` | 20 Hz | grip, throttle valve (measured) | 20 |
| `0x759` | 20 Hz | throttle valve (commanded) | 20 |
| `0x512` | 1 Hz | water and air temperature | 1 |
| **four ids** | **141 frames/s** | | **61** |

Three things follow. The whole connector, unfiltered, is under the ~590 msg/s the radio
carries cleanly, so an allow-all no longer overruns it. The four ids at their native rates
are a quarter of that. And the wheel speeds, when they are found, are expected in a frame
already counted or in one more of the same block. The 27-item Y-TRAC list is not what this
connector offers — the rider aids and the IMU are not on it — so "23 of 27 in nine ids"
below is the prediction this replaced, kept as it was written.

One thing in it does not survive at all: **the device has to acknowledge frames.** With
nothing acknowledging, this connector gave two ids, resent in a loop. That is a change to
the Feather's firmware and wiring, recorded in
[bluecan/README.md](../../bluecan/history/readme-2026-10-13.md#what-the-r9-turned-out-to-send).

The Y-TRAC Rev list is 27 items, and the mapping above now names a frame for **23 of
them**. Those 23 do not cost 23 messages: Yamaha packs them, and they land in **nine
arbitration IDs** — ten if the wheel speeds turn out to live on the ABS node rather than
in `0x20A`. The link is charged per *frame*, so this is the number that decides whether
the device can carry the whole data set, not the item count.

| CAN ID | Which family says so | Y-TRAC items in this one frame | # |
|---|---|---|---|
| `0x20A` | R9 platform [S6] | E/G-RPM, PWR — **and F-Speed/R-Speed** if `Speed`/`Speed_2` are the two wheels | 2 or 4 |
| `0x216` | R9 platform [S6] | TPS, APS | 2 |
| `0x236` | R9 platform [S6] | Gear | 1 |
| `0x23E` | R9 platform [S6] | Water-Temp, Air-Temp | 2 |
| `0x215`-equivalent | R1 family, hunt for it | QS△, QS▽, SCS, LIF, LCS, TCS_flg, SCS_flg, LIF_flg, LC | **9** |
| `0x209`-equivalent | R1 family, hunt for it | TCS | 1 |
| `0x250` | R1 family, hunt for it | Acc-X, Acc-Y | 2 |
| `0x254` | R1 family, hunt for it | Pitch | 1 |
| unknown IMU frame | nobody | Lean | 1 |
| `0x205` / `0x264` | R1 family | F-Speed, R-Speed — only if they are not in `0x20A` | 0 or 2 |
| — | — | Fuel, EBM, BSR, BSR_flg — 🔴, no ID anywhere | 4 unreachable |

The row worth staring at is `0x215`. **Nine of the 27 items are one frame**: every
rider-aid setting the R1 family publishes plus four of the five flags. Adding SCS, LCS
and the flags to a session that already reads QS△ costs *nothing* — same ID, same
message, different bits. The same shape holds for TPS/APS, for the two temperatures and
for the two accelerometer axes. Only four items are genuinely unaffordable, and they are
unaffordable because no source names an ID for them at all, not because of bandwidth.

### What that costs over the radio

The firmware forwards whole frames and RaceChrono decodes them app-side, so the BLE load
is the sum of the *frame* rates of the allowed IDs, gated per ID at the interval the app
asks for (the firmware honours both the allow-list and the requested interval, measured
on the bench):
`out = Σ min(bus rate, 1000 / interval_ms)`.

| | On the bus | Over BLE @ 50 ms | @ 20 ms | @ 10 ms |
|---|---|---|---|---|
| eight fast IDs, assumed 100 Hz each | 800 | 160 | 400 | 800 |
| `0x23E` temperatures, assumed 10 Hz | 10 | 10 | 10 | 10 |
| **nine IDs, total** | **~810 msg/s** | **170** | **410** | 810 |
| ten IDs, if the speeds are separate | ~910 | 190 | 460 | 910 |
| measured clean ceiling | — | ~590 | ~590 | ~590 |

**50 ms is not a hypothetical**: it is what RaceChrono Pro asked for on every channel in
the measured session, and the gate held it to 19.4–19.6 msg/s per ID. So the whole R9
data set, everything Y-TRAC Rev shows and this project can name, is **~170 msg/s against
a ceiling of ~590** — a factor of 3.5 in hand. It stays clean down to about a 20 ms
interval per ID; 590 ÷ 9 ≈ 65 Hz, so ~15 ms is where nine IDs would start refusing, and
only a demand for the full 100 Hz on every ID breaks it.

**And the interval gate, not the ID filter, is what buys that.** Nine or ten of the
bus's ~16 IDs are wanted, and they are the fast ones: the ID filter alone would take
~1050 msg/s down to ~810, which is still above the ceiling. The 4.8× cut is the rate
limit. An allow-*all* — which is what the app sends while channels are being configured —
has no per-ID slot to keep a timestamp in and is therefore not rate-limited at all, so
**refusals while configuring channels in the app are expected, and stop when recording
starts**. That is the one trap in this table.

### What is measured here and what is guessed

- **Measured**: the per-ID gate (19.4–19.6 msg/s against a 50 ms request, ±3%), the
  ~590 msg/s clean ceiling with a knee at 600–620, the 12-byte payload per frame (4-byte
  ID + up to 8 data) — 170 msg/s is ~2.0 kB/s, ~2.6 notifications per connection event at
  the 15.00 ms interval the phone chose.
- **Guessed**: every rate in the "On the bus" column. No R9 has ever been logged. 100 Hz
  comes from [S4] (the IMU broadcasts lean at 100 Hz) and from the soak profile;
  10 Hz for the temperatures is a guess in the same spirit. **AiM's "10 Hz" in the [S6]
  comments is the logger's sampling rate, not the bus rate** — do not read it as a
  broadcast interval.
- **Replaces both**: one key-on capture. The census of unique IDs and their rates is
  priority (a) at the end of this file, and it turns this whole table from arithmetic
  into measurement.
- Two channels on one ID cost one slot, not two: `allow_id()` updates the interval in
  place and the last request wins. The allow-list holds 32 IDs; nine is nowhere near it.
- The phone assigns the connection interval on every reconnect, so the ceiling is not a
  constant. The phone picks it quasi-randomly and does not have to grant what the
  peripheral asks for, so two runs at different intervals are not comparable.

## Related but unmapped CAN traffic seen in the sources

Signals the sources reference that don't map cleanly onto a Y-TRAC Rev item yet, kept here
so they aren't lost:

| CAN ID | Bike | What's known | Source |
|---|---|---|---|
| `0x268` | Ténéré 700 (ABS unit) | Byte(2 or 3): front wheel moving flag (`0x00`=still, `0x01`=moving). Byte 4: brake state (`0x00`=no brake, `0x04`=front or rear brake — front/rear not distinguished). Byte 5 high nibble: ABS light state (`0`=off,`1`=on,`2`=fast flash,`3`=slow flash); low nibble: off-road ABS mode (`0`=off,`8`=on). | [S1] |
| `0x268` | Ténéré 700 (ABS unit, Euro5) | Confirmed by real firmware (not forum guesswork): byte 5 is an ABS state machine — `0x18`=init handshake, `0x1A`=request-last-saved-state, `0x30`=ack/stop. `0x1C` (seen once state-restored) = ABS off / off-road mode, matching [S1]'s nibble reading. | [S5] |
| `0x2A0` | Ténéré 700 (ABS off switch, Euro5) | The bike's "ABS off" handlebar button broadcasts on `0x2A0`, 6-byte payload `00 01 00 00 00 00` = ABS on, `00 01 00 00 00 1C` = ABS off. Explains [S1]'s "nothing when engine not running" — it's an on-demand button message, not a periodic sensor frame. | [S5] |
| `0x260` | 2015+ R1 (ABS/YEC ECU) | Front & rear brake pressure — 4 named 16-bit signals: front demanded, front actuated, rear demanded, rear actuated. 4×16-bit = 8 bytes, exactly one full CAN payload, so this is likely the whole frame (order within it not stated). Byte 0 poster suggested normalizing 0–100% against your own max lever squeeze rather than trying to find an absolute scale. | [S4] |
| `0x264` | 2015+ R1 (ABS/YEC ECU) | Front & rear wheel speed as 16-bit values (see F-Speed/R-Speed above). Unresolved whether raw value is a pulse count, frequency, or pre-scaled speed. | [S4] |
| — | 2015+ R1 | CAN bus runs at 500 kbps (confirmed — matches [S3] MT-07). ~1051 msg/s across only 16 unique arbitration IDs, both "key on engine off" and "key on engine running". Network has distinct dash, ECU, ABS CU, IMU, and CCU nodes; all reachable (+12V/GND/CANH/CANL) via the diagnostic connector in the tail. | [S4] |
| `0x20A` | Ténéré 700 | Byte 4(or5): rear wheel moving flag (`0x00`=still,`0x01`=moving), same frame as RPM. | [S1] |
| `0x22E`, `0x23A` | Ténéré 700 | Present but silent when engine is off; contents not decoded. | [S1] |
| `0x20A` | FZ-09/MT-09 (R9 platform) | Two speed channels in the RPM frame: `Speed` 16-bit at bytes 1–2, ×0.0025 km/h, and `Speed_2` 16-bit at bytes 5–6, ×0.6. The first is finely scaled and tops out around 164 km/h; the second's range is nonsense as km/h, so one of the two is mislabelled or one scale is a conversion artefact. Byte 7 also carries a 5-bit unnamed field below the 2-bit `Mode`. | [S6] |
| `0x23A` | FZ-09/MT-09, FZ-07, YZ-F | Error frame, decoded by AiM as two bytes: byte 0 `FlagError` (documented as `3` = MIL, the malfunction indicator lamp) and byte 2 `Error`. This is [S1]'s undecoded `0x23A`. | [S6], [S7] |
| `0x23E` | FZ-09/MT-09 | Beyond the two temperatures, a 16-bit field at bytes 3–4 that the collection names `S_23EB3_forse_MAP` — *forse* being Italian for "maybe", so even the author is guessing manifold pressure. | [S6] |
| `0x205` | 2015+ R1 (ECU) | Front and rear wheel speed, 16-bit, bytes 1–2 and 3–4, ×0.01 km/h. Competes with [S4]'s `0x264` reading — see the F-Speed row. | [S6] |
| `0x209` | 2015+ R1 | The R1 family's RPM/gear/mode frame: RPM 16-bit at bytes 1–2 ×0.4, TCS mode 4 bits at bit 25, gear 3 bits at bit 29 (`0`=N…`6`, with a `10` = "-" state), a second raw gear at bit 32, power mode 2 bits at bit 38. | [S6] |
| `0x226` | 2015+ R1 | `ECU_MAP_SEL`, 3 bits at bit 53. Nothing else decoded in the frame. | [S6] |
| `0x250`, `0x254` | 2015+ R1 (IMU) | Accelerometers and one gyro rate — see the Acc-X and Pitch rows. These are the IMU frame IDs [S4] knew existed but never located. | [S6] |
| `0x260` | 2015+ R1 (ABS/YEC ECU) | [S6] agrees with [S4] on the frame and adds the layout and scale: four 16-bit pressures — front demanded, rear demanded, front actual, rear actual — at bytes 1–2, 3–4, 5–6 and 7–8, ×0.1 bar. Note bytes 7–8: the file declares a **9-byte** payload for this frame, which classic CAN cannot carry, so at least one field is off by a byte. | [S6] |
| `0x247` | YZ-F (motocross) | Battery voltage, total run time and MAP in one frame. Listed only to show where the conversion artefacts are: AiM's scales here come out as `raw*73` volts and `raw*59999.9` minutes, which are wrong by orders of magnitude and are the clearest evidence that [S6]'s *scales* need sanity-checking even where its *IDs* hold up. | [S6] |

## Sources

- **[S1]** RaceChrono forum thread ["PIDs for yamaha"](https://racechrono.com/forum/d/2296-2296/3)
  — posts by users `aol` (2019 Yamaha Ténéré 700) and `moiatto`, 2023-01-09 and
  2025-01-19. Retrieved 2026-08-20.
- **[S2]** [terrafirma2021/Yamaha-DataLogger](https://github.com/terrafirma2021/Yamaha-DataLogger)
  (commit `d06a80a`, 2024-07-27). **Not used for the mapping table above** — this project
  bridges a Yamaha XT660's **K-line** diagnostic serial protocol (5-byte frames) to
  emulated OBD-II PIDs; it is not raw CAN bus and the frame format doesn't apply to the
  R9. Kept for reference because the *shape* of its formulas may still be a useful
  pattern-match: `RPM = raw*50`, `coolant°C = raw-30`, speed = sum of 8 accumulated raw
  samples. See [Yamaha-DataLogger/yamaha/src/main.cpp](https://github.com/terrafirma2021/Yamaha-DataLogger/blob/main/yamaha/src/main.cpp).
- **[S3]** [JackHat1/MT07_CAN_Project](https://github.com/JackHat1/MT07_CAN_Project)
  (commit `fec86c3`, 2025-09-10) — raw CAN bus decoder for a 2014 Yamaha MT-07, built by
  the FSTUC team. See
  [MT07_CANBus.cpp](https://github.com/JackHat1/MT07_CAN_Project/blob/main/src/MT07_CANBus.cpp)
  and [MT07_CAN_Library.h](https://github.com/JackHat1/MT07_CAN_Project/blob/main/src/MT07_CAN_Library.h).
  Note: this repo's own `examples/output_example.md` looks like illustrative/synthetic
  sample output, not a real captured log — don't treat those numbers as evidence.
- **[S4]** r1-forum.com thread ["CAN Bus Protocol"](https://www.r1-forum.com/threads/can-bus-protocol.512553/)
  — 2015+ Yamaha R1/R1M forum, user `SySt` reverse-engineering the CAN bus for a
  data-logger project he built and raced with for a season (Aug 2015 – Nov 2017, thread
  now closed to replies). Read in full (the site now renders it as a single unpaginated
  page, no separate pages 2–4) — pasted in directly by the user 2026-08-20 to get around
  the live site's bot-challenge wall. This is now a complete primary-source read, not a
  fragment. The same forum's
  [Lean Angle Sensor thread](https://www.r1-forum.com/threads/lean-angle-sensor.512769/)
  was checked too — it only reiterates the same 5°/step finding already captured in the
  Lean row above, nothing further.
- **[S6]** [billyjack2/CAN-DBC-Collection](https://github.com/billyjack2/CAN-DBC-Collection)
  — a 789-file collection of vehicle CAN protocols in DBC form, retrieved 2026-08-22. The
  four Yamaha files (`YAMAHA/FZ09_MT09/`, `FZ07/`, `CAN_2015/`, `YZ-F/`) are **conversions
  of AiM's logger protocol definitions**, not forum guesswork: every signal carries an AiM
  channel number and sampling rate in a `CM_` comment, and the protocol names match the
  ones AiM's own manuals tell you to select. Copied into
  [sources/aim-yamaha/](../sources/aim-yamaha/) against link rot; see the trust section above.
- **[S7]** AiM InfoTech user guides, retrieved 2026-08-22 —
  ["MT-07, FZ-07, MT-09, FZ-09 from 2021 and YZF-R7 from 2022", release 1.01, dated 2025-05-12](https://dwls.aim-sportline.com/doc/eng/bike_kits/yamaha/aim_kits_for_sports_bikes_yamaha_doc_06_eng.pdf)
  and ["YAMAHA MT-09 / FZ-09", release 1.00, all models](https://support.aimshop.com/downloads/stockEcu/bike/yamaha/Yamaha_FZ_MT_09_all_models_ecu_eng_100.pdf).
  These are the vendor documents behind [S6]: they name the protocol to select
  (`YAMAHA` / `FZ09MT09`) for every bike on the R9's platform, list its channels, and give
  the diagnostic-connector type and pinout for both the pre-2021 and the Euro5 era. Both
  PDFs and the connector photo are copied into [sources/aim-yamaha/](../sources/aim-yamaha/). The
  channel list is RPM, Speed, Speed2, WaterTemp, AirTemp, PPS, TPS, Gear, Mode, FlagError,
  Error — no IMU, no rider-aid settings, no fuel. **Read again on 2026-10-11**: the MT-09
  guide also has an `OBD_II` / `CAN` configuration "Only EURO5 models", a second protocol
  this entry missed — see [The first capture](../captures/session-2026-10-11_115626/README.md#the-first-capture-2026-10-11).
- **[S5]** [DKey96/T7-ABS-Dongle](https://github.com/DKey96/T7-ABS-Dongle) — real firmware
  (not forum speculation) for a Ténéré 700 Euro5 ABS-off button emulator/dongle. See
  [euro5_mono.ino](https://github.com/DKey96/T7-ABS-Dongle/blob/main/src/euro5_mono.ino).

**Dead ends** (checked, nothing usable found — don't re-chase these unless something
changes): fz07.org (domain has been taken over by an unrelated spam site); fz09.org's
"CANbus info for 2021 MT-09?" and "Canbus wheel speed for 2021 MT-07" threads and
r7forums.com's "CAN Sniffing?" thread are all behind the same bot-challenge wall as
r1-forum.com was, and nobody has pasted their content in. r7forums has since gone further:
it redirects machine traffic to a `tollbit.` host that answers **HTTP 402 Payment
Required**, so it is not a challenge that patience gets past — only a human browser reads
it. Their remaining value is also lower than it was, since [S6]/[S7] cover the same
platform from the vendor side; fz09.org's "Specs for DIY Euro5 OBD cable" thread is the
one still worth a human's copy/paste, and only for the connector.

## Column definitions

- **CAN ID**: arbitration ID of the frame carrying this signal (hex, e.g. `0x216`).
- **Start Byte**: 0-indexed byte offset within the frame's data payload.
- **Length**: signal width in bits or bytes.
- **Byte Order**: `LE` (little-endian/Intel) or `BE` (big-endian/Motorola).
- **Equation**: raw-to-physical conversion, e.g. `rpm = raw * 0.25` or
  `speed_kmh = raw * 0.1`.
- **Source / Notes**: where the value came from — a specific forum thread/post
  (with URL), a DBC file, a logged/sniffed session with date, etc. Required for
  any row above 🔴 Unknown.

## How this gets filled in

1. Log raw CAN traffic from the bike while varying one input at a time (rev the
   engine, change gear, open throttle) and correlate frame changes to the known
   state. [`tools/2-session.sh`](../tools/2-session.sh) is that session in one command:
   a CANable on a laptop, listen-only, capturing while a menu tags what you just did.
   Only the connection to the bike is blocked. (The Feather that streams to RaceChrono
   is a separate device with a separate job — it is telemetry, not capture, and nothing
   in this folder depends on it.)
2. Cross-reference forum/community findings (R1/R6/MT-09 CAN bus documentation
   is often partially reusable since Yamaha shares ECU platforms) — mark these
   🟡 Unconfirmed until verified on this bike.
3. Once a mapping is verified against a real 2026 R9 and matches the
   Y-TRAC Rev / dash display, promote it to 🟢 Confirmed and record how it was
   verified in Source/Notes.
4. Hardware logging is blocked until a diagnostic-connector pigtail is in hand — see the
   "Current blocker" note above, which now names the likely connector and its pinout from
   the vendor side. r9riders.com has already been checked and has nothing on this topic.
5. Priorities for whenever a logging session on the actual R9 becomes possible, in order:
   (a) **test the FZ09MT09 protocol first, rather than probing blind.** [S6]/[S7] give a
   specific prediction for the R9's platform — `0x20A` (RPM + speed + mode), `0x216`
   (16-bit TPS and APS), `0x236` (gear), `0x23A` (errors), `0x23E` (temperatures) — so the
   first question is no longer "which family" but "does the predicted profile appear".
   Confirming or breaking it takes one key-on capture, and the census of unique IDs decides
   it: the R1 lineage runs ~1051 msg/s across 16 IDs per [S4]. If it breaks, fall back to
   probing the R1-style layout (`0x205`/`0x209`/`0x215`/`0x250`/`0x254`/`0x260`/`0x264`);
   (b) confirm `0x236` gear (highest-confidence cross-model match, and three candidate bit
   masks to choose between); (c) TPS/APS/RPM — the 16-bit-vs-8-bit question and the
   one-byte RPM offset conflict, all three resolvable by revving against the dash; (d) hunt
   for the lean/gyro/accel frames from the IMU node — [S4] confirms they exist on CAN
   for the R1 lineage (lean: coarse, exactly 5°/step) even though no ID has been pinned
   down yet; once found, test the duty-cycle hypothesis in the Lean row above (hold the
   bike at a known intermediate angle on a stand and watch whether the raw value
   alternates between two adjacent 5° steps rather than sitting still).
6. **(a) was run on 2026-10-11 and the predicted profile did not appear** — nor did the
   R1-style fallback. [The first capture](../captures/session-2026-10-11_115626/README.md#the-first-capture-2026-10-11) has what did, and
   why most of it is one unacknowledged frame.
7. **Run again the same day with the adapter acknowledging**:
   [23 ids](../captures/session-2026-10-11_130905/README.md#the-second-capture-2026-10-11-the-adapter-acknowledging), still none of them
   predicted. The priorities in 5 are superseded by the list at the end of that section.
8. **And a third time that evening, with the engine started**, against a recording of its
   sound and a video of a lean-angle app:
   [the third capture](../captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running). Items 1 and 4
   above describe a state that is over — the pigtail is in hand, and the session runs with
   the adapter acknowledging, because listen-only gets two ids of 23 on this connector.
   What is left is at the end of that section.
