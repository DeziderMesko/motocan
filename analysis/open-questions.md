# What is still open

What has not been found or not been settled, most useful first, and what would settle it.
The state of what *has* been found is [r9-signals.md](r9-signals.md).

| | Open | What settles it |
|---|---|---|
| 1 | ~~**The two wheel speeds** — "Speed" and "Speed 2" in AiM's channel list for this platform. Never looked for with the adapter acknowledging. `0x27C` byte 2 is the working guess for one of them~~ **Found 2026-10-16**: the rear in `0x756`, the front in `0x765`, the rear's pulses in `0x27C` byte 2 ([the fourth capture](captures/session-2026-10-16_191234/README.md)). Left open: the unit, 0.01 km/h, rests on a video and the tyre's size, with no dash speed; the front's scale rests only on the stretch where it copied the rear; and see 8 | a dash speed held steady and noted, or the gearing from the manual; the bike pushed by hand, which turns both wheels at one speed |
| 2 | ~~**What the 2-bit mode field is** — `0x27C` byte 4 top two bits, `0x76E` byte 1. It follows the ride mode; PWR is a guess. Custom 2 read `2` in the first capture and `1` in the two after it~~ **Closed 2026-10-16: PWR − 1**, 4 dash photos of 4 in Track 1–4 | — |
| 3 | **The throttle valve's zero.** The top of the scale is measured; a warm idle reads about 8 %, and nothing has shown the valve fully closed | a closed-throttle deceleration, which is not available at a standstill |
| 4 | **The fields that change and have no name** — about forty, listed in [r9-signals.md](r9-signals.md#everything-that-changes-and-has-no-name) | the candidates and their tests are in the section below and in [R9_CAN_unknowns_vs_manual.md](R9_CAN_unknowns_vs_manual.md); the tests are extras A–N of the stand session. *2026-10-16: B–G were done, and H with the engine off; I–N were not* |
| 5 | **Anything that needs the bike moving**: speed against a reference, brake switches under way, what the valve does against the grip when traction control steps in | nothing has been captured while riding, and that is a rule of this project so far, not an oversight |
| 6 | **Bluecan has not read this connector yet.** This bus needs an acknowledging node, and the firmware acknowledges now — proved on the bench against the CANable, not on the bike | plugging it into the R9: [patch 18](../bluecan/history/firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench) |
| 7 | ~~**The Track display and its four modes.** With the display set to Track the dash offers Track 1–4 beside the five modes. None of the captures had it on, so it is not known what those modes read in the mode field, what each of them sets, or whether the bus carries anything more with that display on~~ **Closed 2026-10-16**: the modes set PWR and TCS as any other mode does; SCS and LIF changed and showed nothing; the display itself is `0x76E` byte 2, `0x10` against `0x20`; no new ids | — |
| 8 | **Why the wheel fields are not one sensor each.** With the rear driven on the stand, `0x765` took the exact value of `0x756` from 19:20:47.6 to the stop, the front standing still; 25 s earlier it had stayed at zero. And a second push of the turning front lifts `0x756` with no rear pulse. ABS fail-safe at a guess. The ABS lamp was lit before the run, as it is until a bike moves; whether it went out is not known | a photo of the dash during a stand run; the bike pushed by hand, so that both wheels agree; on a ride it may not matter, because the two wheels turn together |
| 9 | **Is `0x27C` bits 29–31 a 3-bit gear?** Bit 29 is set in first gear with the engine running and was named as a flag. AiM's R1 file has `ECU_GEAR` there, 3 bits, beside the raw gear the R9 has at 32–34, and the rest of bytes 3–4 match that file bit for bit ([r9-signals.md](r9-signals.md#held-against-aims-dbc-files)) | second gear, and third if it is easy, with the engine running on the stand: bit 30 set and bit 29 clear in second, both in third |
| 10 | **How far up the front wheel's field goes.** `(b2 >> 4) << 8 \| (b6 >> 4) << 4 \| (b3 & 0x0F)` is twelve bits, 40.95 km/h at 0.01 km/h a count, and the rear's is sixteen. The highest front value in any capture is 1 706, so a nibble above the twelve would have read zero throughout, as `b2` lo and `b3` hi did. Found 2026-10-16, reading Bluecan's decoder. It is likely there are more bits: AiM's R1 file has both wheels as 16 bits at 0.01 km/h, and the R9's rear is 16. `b2` is not a plain high byte, as `b1` is for the rear: bits 8–11 are in its high nibble and its low nibble stays zero. Candidates for bits 12–15, most likely first: `b0` hi, which would make the high byte `b0` hi followed by `b2` hi, the pairing `0x759` and the rear's `0x756` use ([r9-signals.md](r9-signals.md)); `b2` lo, a byte with its nibbles swapped; `b3` hi, beside the lowest nibble | the bike pushed or ridden past 41 km/h — which is riding, and not done; or on the stand with the rear driven, the stretch where `0x765` copied `0x756`, taken past 41 km/h in a higher gear: the copy would show the top bits |

**Not on this connector, on the evidence:** lean angle, the SCS, LIF, quickshifter and
launch-control settings, the ride mode as a number, battery voltage, fuel used. The service manual says
why: the R9's ECU has three separate CAN pairs, the IMU and ABS unit are on another one, and
the fuel sender is wired to the meter ([R9_CANbus.md](R9_CANbus.md)). Two ways to those
signals exist and neither has been tried, by decision: asking the ECU with diagnostic
requests, and tapping one of the other two pairs. Until now the adapter has only ever
acknowledged; it has never sent a frame of its own.

## The unnamed fields, read against the service manual

[R9_CAN_unknowns_vs_manual.md](R9_CAN_unknowns_vs_manual.md) takes [the list of unnamed fields](r9-signals.md#everything-that-changes-and-has-no-name) and
holds each entry to what the service manual says the ECU reads, drives and decides. The
manual has no frame definitions; what it has is the vocabulary — the sensors on the ECU's
pins, the YDT monitor items with their ranges, the control states named in the fail-safe
columns. That page was written without the capture files and says so. This section is the
other half: its candidates, and what the three captures of 2026-10-11 already say about
them, with later rows dated.

**What the manual settles on its own**, with no capture needed:

- **Fuel level and oil pressure are not the ECU's.** The fuel sender and the oil pressure
  switch are wired to the meter, so neither is to be expected on the diagnostic pair.
- **The ECU's own lean angle is in 5° steps**, 0–60° (YDT item 17) — the R1 forum's
  "increments of 5 degrees" is the same thing. Even if it could be read here it would not
  be telemetry.
- **Both wheels' pulses reach the ECU from the ABS unit** (P0500, YDT items 07 and 16), so
  a wheel field on this connector is what one should expect.
- **The grip sensor does read the forward travel**: YDT shows 7–12 past closed against
  14–18 closed. The 15-bit grip field does not move there, so it is a processed value that
  is held at rest, not the sensor.
- **The YDT's 0–125 throttle scale is not the unit of the CAN fields**: closed to open is
  about 85 YDT counts and about 123 here.

**Its candidates, and what the captures say.** Confidence is the other page's own: A, the
behaviour and the manual fit closely; B, plausible with rivals; C, a guess.

| Field | Candidate from the manual | | Checked against the captures |
|---|---|---|---|
| `0x750` `b4hi:b1` | intake air pressure, or engine-speed-derived if it is zero with the engine off | B+ | **Decoded, and it is engine speed.** Zero with the engine off. `b4hi:b1hi` is `floor(rpm / 100)`: 13 to 33 over the session, and equal to the latest `0x27C` byte 0 in every one of the 32 000 frames sent while the engine ran. `b4hi:b1lo` is a second copy that runs a count ahead or behind while the speed is changing. See below for what this does to the scale |
| `0x750`, the rest | intake air pressure 1 and 2 | B+ | **Not found.** The test asks for a field high and steady with the engine off that drops when it fires. Byte 7's high nibble does that — 9 engine off, 3 while cranking, 5 at a cold idle, 6 warm — but in a blip it goes the wrong way for a pressure: down as the valve opens, and up to 8 on the closed throttle at 3 400 rpm, where a manifold is at its deepest vacuum. So it is something else, unnamed. Bytes 0, 2, 5 and 6 were not resolved |
| `0x751` `b0hi:b2` | the shift sensor (quickshifter load cell), 2.5 V at rest | A | **Not contradicted, not shown.** Rest is 2 052, mid-scale, as a 2.5 V sensor would read. It does not move with lean, which an accelerometer would. It spreads in the blips (1 961–2 164 against 2 036–2 068 at rest). The first capture cannot test it: `0x751` came every 830 ms there and shows only ±16 flicker through every gear change. The other page reads that capture as the value moving "while gears were being changed"; what was recorded is that it moved while the throttle was blipped with the engine running. The lever test stands. *2026-10-16: the lever pressed up and down firmly, four times, engine off — the value read its three resting values, 2 038, 2 054, 2 070, throughout. Against the candidate, unless the sensor is only read with the engine running; that test is the same one with the engine idling* |
| `0x751` b6 bit 4, `0x755` b4 bit 6, `0x769` b1 bit 4 | O2 feedback: enabled, running, and its complement | A as a group | **Timing agrees, nothing more to test here.** 30 s, 26 s and 29 s after the start in the third capture, and `0x751` b6 at 31 s in the first. A hot restart is the test |
| `0x764` b1, `0x769` b1 bit 4, `0x76D` b4 / b6 | fuel cut after a blip | B | not tested: no long deceleration in any capture |
| `0x757` b1, b2, b5 | acceleration enrichment, a fast-opening detect | B− | not tested: every opening with the engine running was a snap |
| `0x764` b2 | the grip's rate of change | B− | **No.** In the slow closing the grip fell from 124 to 72 over four seconds and the byte sat at `A1` throughout; it changes with where the grip is, not how fast it moves, and only below about two thirds of the travel |
| `0x764` b0, b3–b7, `0x759` b5, `0x756` b3/b5/b7, `0x75F` b2lo/b5lo | injection time and ignition advance, perhaps per cylinder | C+ | not tested |
| `0x76C` `b1:b0hi:b2hi` | ISC feedback adjustment | B | not tested: nothing loaded the idle |
| `0x512` b3 | ISC amount, or warm-up enrichment | B− | not separable here: the valve and the coolant moved together all session |
| `0x755` b2 bit 4 | a warm-up stage | B− | **Temperature-gated fits.** Never on below 50.0 °C, first on at 50.8, on a growing share of the time through the fifties (27 %, 39 %, 62 %, 83 %), steady from 60.8 °C. Engine speed was flat at 1 450–1 500 the whole while, so it is not an idle-reached condition |
| `0x751` b6 bits 5–6 | neutral, with start permission | B+ | ~~not tested: the clutch and sidestand were never bracketed~~ *2026-10-16: not neutral alone.* `0x60` in first gear with the clutch pulled and the engine running, `0x20` for 1.6 s as the clutch went out, `0x60` again with the wheel driven; `0x40` in second gear with the sidestand down, engine off, then `0x00` 11 s later; `0x00` for 0.2 s as the sidestand cut the engine. Start permission fits some of it and not the `0x60` in gear. The sidestand and clutch brackets were done in neutral, where nothing moved |
| `0x27C` b3 bit 0 | starter relay command, or the start switch | A− | **The starter, not the switch** (2026-10-16): with the start blocked, in second gear with the sidestand down, the button was pressed and the bit did not set |
| `0x27C` b3 bits 5–7 | not in the manual's list; from AiM's R1 file, the gear as the ECU uses it | B+ | bit 5 set in first gear with the engine running, twice; 6 and 7 never set. Open question 9 |
| `0x758` `b0hi:b2` unit | AiM's R1 `ECU_THROTTLE`, 0.15° a count on 10 bits | C | would put the valve at 86.6° fully open and 6.6° at a warm idle; a coincidence of scale, nothing measured |
| `0x27C` b6 bit 7, `0x76E` b4 / b6, `0x751` b3, `0x75F` b1 | fuel pump relay, headlight, engine-running judgement, charging | — | not tested: needs a capture that begins before the key goes on |
| `0x751` b7 bit 2 | "engine stop detected" (event 245) | C | one occurrence, a second after the stop |
| `0x756` b2 | a start-sequence state, or an after-start counter | C | **A state, not a timer.** `06`, `07`, `08` within 100 ms before any engine speed is reported, `09` at 1 190 rpm, `0A` at 1 509, `0B` at 1 541 — gaps of 50, 50, 300, 150 and 60 ms. At the stop `0B`, `03`, `01`, `00` in 100 ms |
| `0x751` b4 hi | a coarse engine-speed band, about `rpm / 380` | B | **Roughly, with a lag.** Medians of 1 477, 2 079, 2 288, 2 701, 2 996 and 3 327 rpm for 4 to 9, so about 300 rpm a step from 1 900 up, but the bands overlap by hundreds of rpm; it is not a clean function of the speed at that instant |
| `0x27C` byte 2 | wheel-speed pulses | B+ | **Confirmed 2026-10-16**: the rear wheel's, 0–2 a frame, 46.4 to a revolution |
| `0x383`, `0x760`–`0x763`, `0x76B` | version, learning state, DTC / MIL status | — | not tested: a capture through key-on would show a MIL bit for its 2 s. *2026-10-16: `0x383` byte 4 bit 3 is the brake, so `0x383` at least is not static* |

**Engine speed is in hundreds of rpm with eight bits of fraction, and that fixes the
scale.** `0x750` carries the whole hundreds on their own, and they are `0x27C` byte 0. So
bytes 0–1 of `0x27C` are one number in units of 100 rpm, the low byte its fraction:
`rpm = raw * 100 / 256`, which is **0.390625**. The two agree in every frame, through the
blips as well, to 33 at 3 400 rpm — the range the sound could not reach. It sits inside what the sound gave
(0.3855–0.3985) and beside [S6]'s 0.391, which is this number rounded. The grip and the
valve are built the same way, a whole part with eight or four bits of fraction under it.
This was not a re-measurement: it fell out of naming a field in another frame.

The session that would test most of the rest is laid out at the end of the other page:
a capture begun before key-on, each switch bracketed on its own, a long crank, a hot
restart, a steady hold and a snap shut from 4 000 rpm.
