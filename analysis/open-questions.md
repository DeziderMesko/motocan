# What is still open

What has not been found or not been settled, most useful first, and what would settle it.
The state of what *has* been found is [r9-signals.md](r9-signals.md).

Closed questions are left out; the open ones keep their numbers.

| | Open | What settles it |
|---|---|---|
| 1 | **The front wheel's scale.** The rear's unit, 0.01 km/h, rests on the pulses (46 a revolution, a 1.936 m wheel) and one dash photo (28 km/h against 25.2). The front's rests only on the stretches where it copied the rear; see 8 | the bike pushed by hand, which turns both wheels at one speed |
| 3 | **The throttle valve's zero.** The top of the scale is measured; a warm idle reads about 8 %. With the grip shut from 3 700 rpm in third on the stand, the valve went from 658 to 432 in one frame and then held 384–416, the cold idle's own 352–400, for the whole run-down to 2 000 rpm: at a standstill the ECU holds the valve at its idle opening on a shut grip ([the sixth capture](captures/session-2026-10-17_123714/README.md#looked-for-and-not-found)) | a closed-throttle deceleration, which needs the bike moving |
| 4 | **The fields that change and have no name** — about forty, listed in [r9-signals.md](r9-signals.md#everything-that-changes-and-has-no-name) | the candidates and their tests are in the section below and in [R9_CAN_unknowns_vs_manual.md](R9_CAN_unknowns_vs_manual.md). Of the stand session's extras, B–G are done and H with the engine off; I is open; J–N will not be done ([below](#tests-decided-against)) |
| 5 | **Anything that needs the bike moving**: speed against a reference, brake switches under way, what the valve does against the grip when traction control steps in | nothing is captured while riding, by rule |
| 6 | **Bluecan has not read this connector.** It acknowledges, as this bus needs; that is proved on the bench against the CANable | plugging it into the R9: [bluecan/at-the-bike.md](../bluecan/at-the-bike.md) |
| 8 | **Why the wheel fields are not one sensor each.** With the rear turning on the stand and the front standing still, `0x765` takes `0x756`'s value: from 19:20:47.6 to the stop on 2026-10-16, 295 frames of 295, after staying at zero 25 s earlier; on 2026-10-17 after 27 s at zero with the rear turned by hand, blending in over 4 s, and through the engine run after it; not after a key cycle with the rear on clutch drag. And a second push of the turning front lifts `0x756` with no rear pulse. ABS fail-safe at a guess. The ABS lamp is lit until a bike moves; on 2026-10-17 the BC lamp came on half a minute into the stand run, and the engine lamp after it ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-front-field-copies-the-rear)) | a photo of the dash during a stand run; the bike pushed by hand, so that both wheels agree. On a ride the two wheels turn together, so it may not matter |
| 12 | **First gear is 2.2 % off the gearing.** From the bus alone, rpm against the pulses at 46 a revolution, the engine turns 11.866 times to the wheel's once in first, 8.786 in second and 7.313–7.318 in third. Divided by the service manual's primary and secondary ratios, 79/47 × 43/16, that is 2.627, 1.945 and 1.619. The owner's manual has 36/14 = 2.571, 37/19 = 1.947 and 34/21 = 1.619: second and third agree to 0.1 %, first does not. Through the first-gear stretch `0x751` b6 read `60`, which in second and third it read only while the clutch was pulled: slip with the lever held partly in, at a guess ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-gearing-from-the-bus-alone)) | first gear with the engine running and the lever fully out, `0x751` b6 reading `20`: 11.616 means the clutch slipped, 11.866 again means first gear is not 36/14 |

**Not on this connector, on the evidence:** lean angle, the SCS, LIF, quickshifter and
launch-control settings, the ride mode as a number, battery voltage, fuel used. The service manual says
why: the R9's ECU has three separate CAN pairs, the IMU and ABS unit are on another one, and
the fuel sender is wired to the meter ([R9_CANbus.md](R9_CANbus.md)). Two ways to those
signals exist and neither has been tried, by decision: asking the ECU with diagnostic
requests, and tapping one of the other two pairs. The adapter only acknowledges; it sends
no frame of its own.

## The unnamed fields, read against the service manual

[R9_CAN_unknowns_vs_manual.md](R9_CAN_unknowns_vs_manual.md) takes [the list of unnamed fields](r9-signals.md#everything-that-changes-and-has-no-name) and
holds each entry to what the service manual says the ECU reads, drives and decides. The
manual has no frame definitions; what it has is the vocabulary — the sensors on the ECU's
pins, the YDT monitor items with their ranges, the control states named in the fail-safe
columns. That page was written without the capture files. This section holds its
candidates against the captures.

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
| `0x751` `b0hi:b2` | the shift sensor (quickshifter load cell), 2.5 V at rest | A | **Against it with the engine off.** Rest is 2 052, mid-scale, as a 2.5 V sensor would read. It does not move with lean, which an accelerometer would. It spreads in the blips with the engine running (1 961–2 164 against 2 036–2 068 at rest). The lever pressed up and down firmly, four times, engine off: its three resting values, 2 038, 2 054, 2 070, throughout. Against the candidate, unless the sensor is read only with the engine running; the test is the same with the engine idling |
| `0x751` b6 bit 4, `0x755` b4 bit 6, `0x769` b1 bit 4 | O2 feedback: enabled, running, and its complement | A as a group | **Timing agrees, nothing more to test here.** 30 s, 26 s and 29 s after the start in the third capture, and `0x751` b6 at 31 s in the first. A hot restart is the test |
| `0x764` b1, `0x769` b1 bit 4, `0x76D` b4 / b6 | fuel cut after a blip | B | not tested: no long deceleration in any capture |
| `0x757` b1, b2, b5 | acceleration enrichment, a fast-opening detect | B− | not tested: every opening with the engine running was a snap |
| `0x764` b2 | the grip's rate of change | B− | **No.** In the slow closing the grip fell from 124 to 72 over four seconds and the byte sat at `A1` throughout; it changes with where the grip is, not how fast it moves, and only below about two thirds of the travel |
| `0x764` b0, b3–b7, `0x759` b5, `0x756` b3/b5/b7, `0x75F` b2lo/b5lo | injection time and ignition advance, perhaps per cylinder | C+ | not tested |
| `0x76C` `b1:b0hi:b2hi` | ISC feedback adjustment | B | not tested: nothing loaded the idle |
| `0x512` b3 | ISC amount, or warm-up enrichment | B− | not separable here: the valve and the coolant moved together all session |
| `0x755` b2 bit 4 | a warm-up stage | B− | **Temperature-gated fits.** Never on below 50.0 °C, first on at 50.8, on a growing share of the time through the fifties (27 %, 39 %, 62 %, 83 %), steady from 60.8 °C. Engine speed was flat at 1 450–1 500 the whole while, so it is not an idle-reached condition |
| `0x751` b6 bits 5–6 | neutral, with start permission | B+ | **Every value in six captures fits bit 6 = neutral or the clutch lever pulled, bit 5 = neutral or the sidestand up** ([r9-signals.md](r9-signals.md)). The start needs bit 6 and running in gear needs bit 5, at a guess. With the engine running in gear the sidestand is always up, so bit 5 is tested only with the engine off: in gear, clutch out, the stand up and down, `0x20` against `0x00` |
| `0x27C` b3 bit 0 | starter relay command, or the start switch | A− | **The starter, not the switch** (2026-10-16): with the start blocked, in second gear with the sidestand down, the button was pressed and the bit did not set |
| `0x27C` b3 bits 5–7 | not in the manual's list; from AiM's R1 file, the gear as the ECU uses it | B+ | **Confirmed**: 1, 2 and 3 in first, second and third with the engine running |
| `0x758` `b0hi:b2` unit | AiM's R1 `ECU_THROTTLE`, 0.15° a count on 10 bits | C | would put the valve at 86.6° fully open and 6.6° at a warm idle; a coincidence of scale, nothing measured |
| `0x27C` b6 bit 7, `0x76E` b4 / b6, `0x751` b3, `0x75F` b1 | fuel pump relay, headlight, engine-running judgement, charging | — | From two key-offs and a key-on in one capture: `0x751` b3 bit 1 is clear exactly while the engine runs, the engine-running judgement at a guess; `0x27C` b6 bit 7 clears at key-off. The rest not tested |
| `0x751` b7 bit 2 | "engine stop detected" (event 245) | C | one occurrence, a second after the stop |
| `0x756` b2 | a start-sequence state, or an after-start counter | C | **A state, not a timer.** `06`, `07`, `08` within 100 ms before any engine speed is reported, `09` at 1 190 rpm, `0A` at 1 509, `0B` at 1 541 — gaps of 50, 50, 300, 150 and 60 ms. At the stop `0B`, `03`, `01`, `00` in 100 ms |
| `0x751` b4 hi | a coarse engine-speed band, about `rpm / 380` | B | **Roughly, with a lag.** Medians of 1 477, 2 079, 2 288, 2 701, 2 996 and 3 327 rpm for 4 to 9, so about 300 rpm a step from 1 900 up, but the bands overlap by hundreds of rpm; it is not a clean function of the speed at that instant |
| `0x27C` byte 2 | wheel-speed pulses | B+ | **Confirmed**: the rear wheel's, one a hole of the sensor ring, 46 to a revolution |
| `0x383`, `0x760`–`0x763`, `0x76B` | version, learning state, DTC / MIL status | — | `0x383` b4 bit 3 is the brake. `0x383` b0 bit 2 set as a fault came on and stayed set through a key cycle, with the engine lamp lit: a candidate for a fault flag, one occurrence ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-bc-lamp-and-the-engine-lamp)). `0x760`–`0x763` and `0x76B` not tested: a capture through key-on would show a MIL bit for its 2 s |

**Engine speed is in hundreds of rpm with eight bits of fraction, and that fixes the
scale.** `0x750` carries the whole hundreds on their own, and they are `0x27C` byte 0. So
bytes 0–1 of `0x27C` are one number in units of 100 rpm, the low byte its fraction:
`rpm = raw * 100 / 256`, which is **0.390625**. The two agree in every frame, through the
blips as well, to 33 at 3 400 rpm — the range the sound could not reach. It sits inside what the sound gave
(0.3855–0.3985) and beside [S6]'s 0.391, which is this number rounded. The grip and the
valve are built the same way, a whole part with eight or four bits of fraction under it.

Of the tests at the end of the other page, the one still open is a steady hold at
2 500–3 000 rpm and a snap shut from 4 000 rpm (extra I of the stand session).

### Tests decided against

Extras J–N of the stand session will not be done; the owner sees no value in them. The
fields each would have tested, so that they are not taken for untested by oversight:

| | The test | The fields it would have tested |
|---|---|---|
| J | three fast snaps and three slow openings to the same place | `0x757` b1, b2, b5, the acceleration-enrichment candidate: only the snaps should set them |
| K | idle until the radiator fan starts and stops | the fan relay, which has no candidate field; `0x512` b3 and `0x76C` `b1:b0hi:b2hi`, the idle-control candidates, stepping as the fan loads the idle; a fully warm idle against the manual's 1 200–1 400 rpm |
| L | stop with the stop switch, and see whether the headlight goes off | `0x27C` b6 bit 7, `0x76E` b4 / b6, `0x751` b3, `0x75F` b1: the headlight against the other bits that come on with the engine |
| M | a hot restart within a minute | `0x751` b6 bit 4, `0x755` b4 bit 6, `0x769` b1 bit 4, the O2-feedback group: they should switch sooner than the 26–30 s after a cold start |
| N | the key turned off and on again during a capture | the fuel pump's prime among `0x27C` b6 bit 7, `0x76E` b4 / b6, `0x751` b3, `0x75F` b1; the MIL's 2 s and anything else moving at key-on in `0x383`, `0x760`–`0x763`, `0x76B` |
