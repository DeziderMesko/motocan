# The third capture, 2026-10-11: the engine running

> The write-up of this session, as it was written on the day. The current state of every
> signal is in [r9-signals.md](../../r9-signals.md); where a later session corrected
> something here, that page has the correction.

[`captures/session-2026-10-11_200550/`](./):
`tools/2-session.sh --ack`, 20:06:15 to 20:21:58, 464 793 frames, 49 marks. Engine off
until 20:15:58, running until 20:21:23. The same 23 ids at 506 frames a second, the adapter
transmitted nothing. Two recordings from a phone came with it, in `other/`: the sound of
the engine, and a screen recording of a lean-angle app with the phone on the bike.

**The phone's files are stamped with when they ended, not when they began.** The video's
name carries both: `201115` is the end, and `1791742211459` is the start in epoch
milliseconds, 20:10:11.459, which is 63.6 s earlier and the length of the video. The
audio's `creation_time` is 20:21:37 and it is 310.8 s long, so it began about 20:16:26, to
the second and no better. Both are the phone's clock. How far that is from the laptop's
could not be measured: it needs one signal seen by both, and neither recording gave one
sharp enough.

**Engine speed is `0x27C` bytes 0–1, big-endian, × 0.3915 rpm.** Measured against the
sound. A four-stroke triple fires three times in two turns, so its firing frequency is
rpm / 40, and the recording shows that frequency and its multiples and nothing between
them (orders 1.5, 3, 4.5, 6 stand well clear; the half-orders between are at the noise).
Predicting those harmonics from the raw value and sliding the scale, the fit peaks at
0.3915 and is within a tenth of its peak from 0.3855 to 0.3985; the next-best scales are
that one's neighbours and its half. [S6]'s 0.391 for the FZ-09/MT-09 is the same number.
What carried the fit is the idle drifting down as the engine warmed, raw 4 175 to 3 680,
which is 1 635 to 1 440 rpm. **It was not checked above idle**: the three blips are too
short for this method to resolve, and the scale that comes out of them alone is noise. By
the scale the highest of them reached raw 8 681, about 3 400 rpm. The video is not in the
repository; the audio is. `tools/5-rpm-from-audio.py` reruns this, and
`tools/4-lean-from-video.py` the reading of the video below.

**Lean is not on this connector.** The app draws a coloured half-plane whose edge tilts
with the phone, and the tilt of that edge agrees with the number it prints to 0.05° in six
frames checked, so every third frame gives a reading: 30 a second, in
[`other/lean-from-video.csv`](other/lean-from-video.csv).

<img src="other/lean-app-screenshot.png" width="180" alt="A frame of the screen recording: the level app showing 5.8 degrees, a blue half-plane tilted against a white one">

*One frame of the recording, 25 s in. The edge between blue and white is what
`tools/4-lean-from-video.py` measures; it gives 5.79° here against the 5.8° printed.*

The bike went from 13.4° on the stand to upright and back three times. Every byte, every
nibble and every 12- and 16-bit pairing in all 23 frames was correlated with that trace at
offsets of ±6 s, and the best of them is 0.28, at the edge of the range — nothing. The
12-bit values in `0x750` and `0x751` sit on their resting noise throughout. The second
lean, with the engine running, has no video; comparing the bracket with the ten seconds
either side shows nothing there either. So the IMU is not on this pair, or does not send
lean, roll rate or lateral acceleration while the bike is stationary.

**One thing did happen only while the bike was being handled**: `0x27C` byte 2 read `01`
for a single frame six times — four in the first lean bracket, two in the second, none
anywhere else in 16 minutes. They do not fall at one angle (upright twice, about 6° and
10° on the way down). A wheel turning a few centimetres as the bike comes off the stand
would look like this, and that is a guess.

**Water temperature is confirmed: `0x512` byte 0, `raw*0.625 - 30`.**

| Dash, °C | Raw | By the equation |
|---|---|---|
| 17 | 76 | 17.5 |
| 40 | 115 | 41.9 |
| 43 | 118 | 43.8 |
| 44 | 120 | 45.0 |
| 76 | 171 | 76.9 |

Half a degree to two degrees above the dash, most while it was rising fastest. With the
22 °C anchor from the second capture that is six, over 59 °C of span. Byte 1 read 76 for
the air anchor, 17.5 °C against 16 on the dash, and then climbed to 114 — 41 °C — over
five minutes of idling at a standstill. That is what an intake-air sensor behind a hot
engine does, and it is still one anchor.

**The air equation is the water one, and it is the dash that differs.** Before the start
the bike had stood for seven hours, and for the nine minutes from 20:06 to 20:15 the two
bytes read the same raw value, 76, with byte 1 dipping to 75. Two sensors at one
temperature giving one number is what a shared equation looks like, and the water side of
it is held to the dash over 59 °C. The dash showed 17 for the water and 16 for the air at
that moment, off the same raw value — so the dash takes about a degree off the air, or
rounds the two differently, and the 1.5–2 °C "error" in both air anchors is that and not
the equation. The value had been still for nine minutes, so the dash being slow to refresh
does not account for it. One cold soak at one temperature: it says nothing about the
scale away from 17 °C beyond what the shared sensor type implies. Byte 3 is zero with the engine off and falls from
about 55 to 24 as it warms; unidentified.

**The battery-voltage candidate in `0x76A` is withdrawn.** Read as before it gave 10.30 V
with the key on, and 15 to 28 V with the engine running. The low nibble of byte 2 is the
top nibble of the water temperature's raw value: it steps 4, 5, … B at the moments
`0x512` byte 0 crosses 80, 96, 112, 128, 144, 160, 176, and the bottom nibble is the low
nibble of byte 5, stepping with it 96 times. Byte 3 beside it is a constant `06`. The earlier 12.85 V was an engine at 22 °C.

**Which throttle field is the grip, and which the valve.** With the engine idling and the
grip untouched, the 15-bit field in `0x758` sits still while the other two move with the
idle, which only the ECU is controlling:

| | Engine off, at rest | Cold idle | Warm idle | Blips |
|---|---|---|---|---|
| `0x758` `b7lo:b6lo:b5`, 15 bits | 512 | 0 | 0 | to 3 550–7 970 |
| `0x758` `b0hi:b2`, 12 bits | 336 | 368 | 176 | to 510–590 |
| `0x759` `b0hi:b2hi`, 8 bits | 22 | 24 | 12 | to 33–37 |

So the 15-bit field is the grip, and the 12-bit and 8-bit ones are the throttle valve —
open further for a cold idle than the grip's rest position leaves it, closing as the engine
warms. `0x759` leading `0x758` by a sample then reads as commanded against measured. The
blips were small: an eighth to a quarter of the grip's travel. The grip's rest value has
now been seen at 256, 512 and 0, so the bottom of that field is not a calibrated zero.

**All three are in one unit.** The 15-bit field divided by 256 runs 0 to 123.7 (124.7 on
this session's sweep); the 12-bit one divided by 16 runs 20 to 144.4; the 8-bit one 21 to
144. So the grip is an 8-bit number with eight more bits of fraction under it, the measured
valve the same with four, and the commanded valve the whole number alone. With the engine
off the valve simply follows the grip, 21 above it across the whole travel (20.0 to 22.2
in every tenth of it). What the unit is — percent, degrees, a sensor's volts — is still
not known, and 124 is not an obvious full scale for any of them. Until it is, the grip as
a share of its travel is `raw15 / 31 672`, 31 931 at the very stop.

**The valve has no known zero.** Engine off it rests at 20–21, and a warm idle takes it
down to 11, so the engine-off rest is a parked position and not closed. Closed was not
reached in this session; a warm engine on a closed grip at speed would be nearer to it.

**With the engine running the valve does not follow the grip one for one.** In the blips
the grip went to 14–17 and the valve rose 12–17 above idle, to 31–37 — close to the same
movement, but from the idle position and not from 21 above the grip. The commanded value in
`0x759` moves in the same 50 ms sample as the grip and the measured one a sample or two
later, which is the order a command and its result should come in. The mode was rain for
all three, so this is one map of five.

**Percent is what gets used, and the unit is left alone.** The rider's decision: nothing
downstream needs degrees or volts. So `APS % = raw15 / 31 672 × 100`, both ends measured,
and `TPS % = (raw12 / 16) / 144.4 × 100`, with the field's own zero standing in for a
closed valve that has not been seen. On that line a warm idle reads about 8 %. The top is
the valve with the engine off and the grip at the stop, taken to be fully open because the
valve follows the grip there. If a ride shows where the valve goes on a closed grip at
speed, the bottom moves to that.

**The grip reports nothing for the first twentieth of its travel.** Opening or closing, in
the slow sweep and in every blip, the field goes straight between rest and about 6–9 (raw
1 543 to 2 310) with no sample between, the 2.5 s closing included. Either the sensor has
that much free play before it reads, or the ECU holds the value at rest inside it.

**The grip's forward travel showed nothing.** Three pushes forward past closed, bracketed,
engine off: no byte in any frame took a value it does not also take at rest. The repeat of
the slow sweep agrees with the second capture — 511 to 31 931, 320 to 2 331, 22 to 145.

**SCS, LIF and QS are not here.** SCS 3-2-1-2 and LIF 1-2-3-2-1 with the engine off, LIF
1-2-3 and SCS 1-2-3 with it running, QS 1-2-off twice: 21 anchors, and nothing changed at
any of them outside the bytes that never sit still, none of which maps onto the settings.
The ride modes, done in between, show as before — custom 2 `06 40`, custom 1 `08 00`,
rain `0E C0` in `0x27C` bytes 3–4 with `0x76E` byte 1 following — so the method does see
a setting when one is sent.

**What the engine switched on**, none of it identified:

| Where | What it did |
|---|---|
| `0x27C` byte 6 bit 7 | set at 20:15:58.4, cleared at the stop — a second start agreeing with the first capture. It goes with the engine running; what it stands for is not known |
| `0x27C` byte 3 bit 0 | set from 20:15:57.9 to 20:15:58.4, the half second before the bit above. The first capture has the same bit set in one sample, at its start. It goes with the engine being started; that it is the starter is a guess. It is the bit below the TCS setting, which is why TCS is `>> 1` |
| `0x751` byte 6 | `0x60` → `0x70` at 20:16:28, thirty seconds after the start, as in the first capture (31 s); back at the stop |
| `0x751` byte 4, high nibble | 4 at idle, up to 7 and 9 in the blips, and 5 for a moment at the start and the stop |
| `0x750` `b4hi:b1` | follows engine speed, r = 0.95 over the five minutes |
| `0x755` byte 2 bit 4 | off until 20:18:06, then flickering with a growing share of on, steady on from 20:18:59 to the stop |
| `0x755` byte 4 bit 6 | on from 20:16:24, dropping out for a second or so after each blip |
| `0x756` bytes 2, 3, 5, 7 | byte 2 counts 0 to `0B` through the start and back at the stop; the others never rest |
| `0x757` bytes 1, 2, 5 | a single frame of non-zero at two of the three blips |
| `0x769` byte 1 bit 4 | set with the engine off, cleared 29 s after the start and set again at the stop; while running, a half-second pulse about a second after each blip |
| `0x76C` `b1:b0hi:b2hi`, 16 bits | `0x9200` with the engine off; up to `0x9224` in the ten seconds after the cold start, back down through `0x9200` to `0x91FA` and home again by the end. A correction around a rest value, not a count |
| `0x75F` bytes 2, 5 | never rest while it runs |
| `0x76D` bytes 4, 6; `0x76E` bytes 4, 6 | change at the start and the stop |

**Next.** Engine speed is closed: the rider has taken the scale as it stands, and it is
not to be measured again. What is left, by what each needs:

- *The rear wheel on a stand, in gear*: R-Speed, what `0x27C` byte 2 is, and whether
  anything in `0x750`/`0x751` is a wheel.
- *A ride, or at least rolling*: F-Speed, the accelerations, lean and pitch if they are
  sent at all once the bike moves, the valve's closed position, and fuel — nothing in this
  capture counts upward with the engine running, so fuel used has no candidate.
- *From the saddle, no engine*: the rider's PWR in each mode, to name the 2-bit field; the
  unit of the throttle fields, if the dash or Y-TRAC shows a percentage to hold them to;
  LCS, EBM and BSR, which have never been stepped.
- *Not from this connector, on the evidence*: SCS, LIF, QS. Y-TRAC shows them, so the CCU
  gets them from somewhere; either another bus or a request rather than a broadcast.

**Open, about the 2-bit field.** Custom 2 has not read the same in all three captures. In
the first it was the unanchored fifth state, `08 80`: TCS 4, field 2. In the second, with a
key of its own, `12 40`: TCS 9, field 1. In the third, `06 40`: TCS 3, field 1 — the same
bytes as sport, so stepping from sport to custom 2 showed nothing. The TCS differences are
the rider's own changes. The field going from 2 to 1 between 12:08 and 13:09 is either a
PWR change in custom 2 that the rider does not now remember making, or the first capture's
fifth state was not custom 2. Not resolved; the PWR notes in
[stand-session.md](../../stand-session.md) are what will settle both this and the field's name.

**Since this was written** the rider narrowed the aim to what AiM's `FZ09 MT09` channel
list holds — RPM, Speed, Speed 2, Water Temp, Air Temp, PPS, TPS, Gear, Mode, FlagError,
Error — on this connector, without asking the ECU anything. The R9's service manual turned
up the same evening and says why the rest is missing: [R9_CANbus.md](../../R9_CANbus.md). Of the
list above that leaves the stand session, and it is written up as a checklist in
[stand-session.md](../../stand-session.md).
