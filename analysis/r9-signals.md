# What the Yamaha R9 sends on its diagnostic connector

The reference page: every id seen, what is decoded, what changes without a name, and what
was looked for and is not there. Measured on a 2026 R9, on the red 6-pin diagnostic
connector ([connector.md](connector.md)), at 500 kbps, with the adapter acknowledging
frames — in listen-only the connector gives 2 ids of the 23.

- The decoded signals as one table with bit masks and equations: the
  [front page](../README.md#what-is-decoded-so-far), and machine-readable in
  [r9.dbc](r9.dbc).
- The evidence, session by session: [captures/](captures/).
- What is still open, and the test for each: [open-questions.md](open-questions.md).

Bytes are numbered from 0. `hi` and `lo` are a byte's high and low nibble. A field is
described by **when it changes**, and gets a name only once something independent — the
dash, a sound recording, a second sensor — has agreed with it.

## What the R9 sends, as of 2026-10-17

The summary of the six captures, for whoever opens this page wanting the answer rather
than the argument. 23 ids on the diagnostic connector; bytes numbered from 0. The evidence
for each row is in the write-ups under [captures/](captures/). Where a row was corrected,
the old wording is struck through and the date says when.

| ID | Rate | What is in it | How sure |
|---|---|---|---|
| `0x27C` | 100 Hz | gear — byte 4, low 3 bits; `7` = between gears | confirmed, 12 anchors of 12 |
| | | TCS — byte 3, `(b3 >> 1) & 0x0F` | confirmed, 9 anchors of 9 (four more in Track 1–4, 2026-10-16), and a tenth in a photo of the dash on 2026-10-17. The five modes' TCS in the dash's mode menu, photographed 2026-10-17, are the five values the first capture read ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-ride-modes-in-the-menu)). ~~byte 3 `>> 1`~~: corrected 2026-10-16, because bit 5 above it is set in gear with the engine running, and `b3 >> 1` then reads 16 too high. AiM's DBC for the MT-09 has the field 4 bits wide |
| | | byte 3, bit 0 — set for about half a second as the starter turns, and in the last frames before the bus goes quiet at key-off | seen at all six starts captured. **Also set in the last frames at both key-offs captured** (2026-10-17), with no start. `0x751` byte 3 bit 3 sets and clears in the same frames at every edge, seven in that capture. **Not set when the start button was pressed with the start blocked** (second gear, sidestand down, 2026-10-16), so it follows the starter or the ECU's command to run it, not the button. Once, for 0.14 s, with the engine running (19:21:15.9 on 2026-10-16), unexplained |
| | | **the gear as the ECU has it** — bits 29–31, `(b3 >> 5) & 0x07`. ~~byte 3, bit 5 — set in gear with the engine running~~ | named 2026-10-17: 1 in first gear (2026-10-16), 2 in second and 3 in third with the engine running, the same number as the box's own field; AiM's R1 file has `ECU_GEAR` here. It is not the box's field: between gears it holds the last gear where byte 4 reads 7, and when the engine is stopped in gear it keeps that gear while the box is shifted with the engine off, until key-off. 0 in neutral with the engine running and in gear with it off. The same number is in `0x765` byte 1, bits 0–2 ([the sixth capture](captures/session-2026-10-17_123714/README.md#second-and-third-gear-0x27c-bits-2931-are-the-gear)). *Until 2026-10-17 this row read: both times first gear went in with the engine running, 2026-10-16; not set in second gear with the engine off. Same frames as `0x765` byte 1 bit 0. Candidate: the low bit of a 3-bit gear. Only first gear has been seen with the engine running. Bits 6 and 7 have never been set* |
| | | engine speed — bytes 0–1, big-endian, `raw * 100 / 256` rpm (×0.390625): byte 0 is hundreds of rpm, byte 1 the fraction | the sound of the engine gave 0.3915 ± 1.5 % at idle; the hundreds sent separately in `0x750` give the exact form and hold to 3 400 rpm |
| | | **PWR − 1** — byte 4, top 2 bits; AiM's R1 file has `ECU_POW_MODE` at the same bits | named 2026-10-16: PWR 1, 2, 3, 3 on the dash in Track 1–4, 0, 1, 2, 2 here, 4 anchors of 4, photographed ([the fourth capture](captures/session-2026-10-16_191234/README.md#track-14-and-what-the-2-bit-field-is)). By it sport is PWR 2, street 3, rain 4, custom 1 PWR 1, which is a reading of the field, not of the dash. *2026-10-17: the dash's mode menu, photographed, says the same of all four, and custom 2 is PWR 2; a fifth anchor in a photo of the dash in custom 2.* ~~a 2-bit setting that goes with the ride mode; PWR at a guess~~. Custom 2 read `2` in the first capture and `1` in the two after it: a PWR change in custom 2 between them is now the likely reason |
| | | byte 6, bit 7 — set at the first start after key-on; clears for a third of a second at each later start | corrected 2026-10-16: it stayed set after all three engine stops captured (2026-10-11 20:21:23.8, 2026-10-16 19:20:35.7 and 19:21:18.0) and cleared only at a restart. ~~set while the engine runs — set within half a second of both starts seen, cleared at the one stop seen~~; the stop on 2026-10-11 did not clear it either, the capture says so. When it clears at key-off has not been seen. *2026-10-17: it clears in the last frames before the bus goes quiet at key-off, both key-offs captured* |
| | | **rear wheel pulses** — byte 2, the count in each 10 ms frame, 0–2 at idle, 0–4 at 59 km/h | **one pulse a hole of the sensor ring, 46 to a revolution** (2026-10-17): the rider counted 46 holes by hand, and two sets of turns by hand counted 507 and 462, 11 × 46 + 1 and 10 × 46 + 2 ([the sixth capture](captures/session-2026-10-17_123714/README.md)). ~~named 2026-10-16: 46.4 pulses for each revolution of the rear wheel by `0x756`, in three stretches; 46.2 against the video~~: those were 0.9 % and 0.4 % high. Counts at speeds below where `0x756` shows anything. ~~`01` for a single frame, six times, while the bike was being lifted~~: that was the wheel turning a fraction |
| | | byte 4, bit 3 — set as the BC lamp came on, cleared at the next key-on | one occurrence, 2026-10-17: set at 12:41:10.87, within half a second of a photo of the dash with the BC lamp lit (the phone's clock against the laptop's), after half a minute of the rear wheel driven in third with the front standing still; held through the engine stop and key-off; clear 0.3 s after key-on, and the lamp was out after the key cycle (the rider). Never set in the five earlier captures. **Candidate: the BC lamp** ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-bc-lamp-and-the-engine-lamp)) |
| | | byte 5, bit 3 — set for 1.9 s in a run-up to 3 700 rpm in third | one occurrence, 2026-10-17. Byte 5 was zero in every other frame of all six captures. Unexplained |
| `0x512` | 1 Hz | water temperature — byte 0, `raw*0.625 - 30` °C | confirmed, 6 anchors from 17 to 76 °C, within 2 °C of the dash. A seventh, 2026-10-17, is 2.75 °C over the dash's 21, with the coolant rising 0.3 °C a second |
| | | air temperature — byte 1, same equation | found: after a cold soak it reads the same raw value as the water sensor, 76; the dash shows it a degree or two lower than the equation does (16 against 17.5) |
| | | byte 3 — zero with the engine off, about 55 falling to 24 as it warms | unknown. Not the manifold pressure AiM's MT-09 file guesses for this frame: a pressure would read the atmosphere with the engine off, not zero. Byte 2 beside it is `00` in every frame of all five captures |
| `0x751` | 100 Hz | neutral / in gear — byte 6, `0x60` / `0x00` | agrees with every change into and out of neutral at rest. **Not neutral alone** (2026-10-16): in first gear with the engine running it read `0x60`, `0x20` for 1.6 s as the clutch went out, then `0x60` again while the wheel was driven; in second gear, engine off, `0x40` and then `0x00`. Bits 5 and 6 are start permissions at a guess (neutral, clutch, sidestand) — see [the fifth capture](captures/session-2026-10-16_192421/README.md#the-blocked-start) |
| | | byte 6 goes `0x60` → `0x70` half a minute after a start, in both sessions with one, and again 30 s after the restart on 2026-10-16 | unknown |
| | | a noisy 12-bit value | does not follow lean — see [the third capture](captures/session-2026-10-11_200550/README.md) |
| `0x76E` | 2 Hz | the same 2-bit setting as `0x27C` — byte 1: PWR − 1 | changes at the same moments |
| | | byte 2 — `0x20` with the normal display, `0x10` with the Track display | one switch each way on 2026-10-16, in the frame the mode changed; `0x20` in every frame of the earlier captures. A candidate for the display setting |
| `0x756` | 20 Hz | **rear wheel speed** — `(b3 >> 4) << 12 \| (b1 & 0x0F) << 8 \| (b0 & 0xF0) \| b2 >> 4`, 0.01 km/h. ~~`b1 << 8 \| (b0 & 0xF0) \| b2 >> 4`, 0.01 km/h at a guess~~ | **corrected 2026-10-17**: past 40.95 km/h the twelve lower bits start again from zero and byte 3 bit 4 sets, not byte 1 bit 4; bit 12 seen in 51 frames up to 59.0 km/h, bits 13–15 the rest of the nibble at a guess. `b1` hi has been zero in every frame of six captures. **The unit, against the dash**: 28 km/h on the dash in a photo while the field averaged 25.2, a speedometer's usual 11 % high, one photo. **Against the pulses**, 46 a revolution: 696.5–697.6 counts per revolution a second in first, second and third, a 1.936 m wheel at 0.01 km/h ([the sixth capture](captures/session-2026-10-17_123714/README.md#the-scale-against-the-dash)). *Up to 2026-10-17:* 700 counts per revolution a second against a video of the wheel, sd 1.0 % in 24 windows; as 0.01 km/h that is a 1.946 m wheel, which the 180/55 ZR17 tyre is. Follows the rear when the rear alone turns. **But** it also rises, to 2.7–4 km/h, when the turning front is pushed a second time, with no rear pulse counted — three times in two captures — so it is a processed value, not the sensor ([the fourth capture](captures/session-2026-10-16_191234/README.md#two-things-that-do-not-fit-one-field-per-wheel)). **Its layout, read little-endian** (2026-10-16; *its top four bits are wrong, corrected 2026-10-17 above: they are `b3` hi, not `b1` hi*): `b1:b0` is an ordinary 16-bit value whose lowest nibble, `b0` lo, is `0` in every frame of all five captures; the top twelve bits are there, 0.16 km/h a count, and the lowest four sit in `b2` hi instead. That `b2` hi belongs to the same number was checked: over the 343 frames of the fourth capture above 3 km/h where the top twelve bits move by 32 or less between neighbours, the RMS of the second difference is 8.99 counts with `b2` hi in place, 10.49 with 7.5 in its place, and 10.46–11.67 with its values shuffled, 200 times. The same pair, `b0` hi followed in value by `b2` hi, carries the valve in `0x759` and the top of the valve in `0x758`, so this looks like a convention of these frames rather than an accident of one |
| | | byte 2 low nibble — `06` to `0B` through the start | see the unnamed fields |
| `0x765` | 10 Hz | **front wheel speed** — `(b0 >> 4) << 12 \| (b2 >> 4) << 8 \| (b6 >> 4) << 4 \| (b3 & 0x0F)`, the same unit as `0x756`. ~~`(b2 >> 4) << 8 \| (b6 >> 4) << 4 \| (b3 & 0x0F)`, at a guess~~ | **The top nibble is `b0` hi** (2026-10-17): in the run-up past 40.95 km/h, while the field copied the rear's, `b0` bit 4 set in the same frames as the rear's bit 12, and with it the field equalled the rear's in all 25 frames; bits 13–15 the rest of the nibble at a guess. **It copies the rear again** (2026-10-17): with the rear turned by hand, zero for 27 s, then rising over 4 s to meet the rear's value, then equal to it in every frame, 298 by hand and 539 of 539 in the engine run that followed; after a key cycle, with the rear on clutch drag at up to 2.6 km/h, zero in all 567 frames. On 2026-10-16 the rear by hand for 13 s left it at zero. *Up to 2026-10-17:* follows the front when the front alone turns, three pushes by hand, to 747. **But** from 19:20:47.6 to the stop of the second engine run on 2026-10-16 it carried exactly the value of the last `0x756` frame, 295 frames of 295, with the front wheel standing still; in the first run it had stayed at zero. ABS fail-safe at a guess. Its scale has only that copy to go on. ~~**Twelve bits as read, so 40.95 km/h at the top** (2026-10-16)~~ *answered 2026-10-17, above*: the highest value in any capture is 1 706, 17 km/h, so whether more bits sit above them is not known. The nibbles of the frame that have read zero in every capture — `b2` lo, `b3` hi, `b4` hi, `b7` lo, `b1` hi, and all of `b0` and `b5` — are where they would be ([open question 10](open-questions.md)). The low nibble `b3` lo belongs to the number by the same test as the rear's: 14.98 counts with it, 16.18 without, 15.70–17.41 shuffled, over 164 steady frames |
| | | byte 1, bits 0–2 — the gear as the ECU has it. ~~byte 1, bit 0 — set in gear with the engine running~~ | the same number as `0x27C` bits 29–31 in the same frames: 1, 2, 3 in first, second and third (2026-10-16 and 2026-10-17) |
| `0x383` | 10 Hz, ~~2 Hz~~ | **the brake** — byte 4, bit 3 | named 2026-10-16: front lever and rear pedal, each pulled twice, from the same frame to the same frame; twice more as the rear wheel was stopped in the engine run. The first change ever seen in `0x383`. Also set in the last frame before the bus goes quiet at key-off, both times, 2026-10-17; whether the brake was on is not known. *The rate was written 2 Hz until 2026-10-17; it is 10 Hz in all five acknowledged captures* |
| | | byte 0, bit 2 — set as the engine lamp's fault came on, and not cleared by a key cycle | one occurrence, 2026-10-17: set at 12:41:10.92, 0.05 s after `0x27C` byte 4 bit 3; set from key-on to the end of the capture after a key cycle, while the engine lamp stayed lit. But the rider saw the engine lamp come on some time after BC, and the photo near the moment shows it dark. **Candidate: the engine fault**, which the lamp follows with a delay, at a guess |
| `0x76D` | 2 Hz | **cruise RES+** — byte 3, bit 0; **cruise SET−** — byte 5, bit 4 | two presses each, 2026-10-16, engine off. At 2 Hz a press shorter than half a second can fall between frames |
| `0x758` | 20 Hz | **the grip** — 15 bits, `b7lo:b6lo:b5`, 256 or 512 at rest with the engine off, 0 with it running, 31 672 at the stop | one number, falls without a step back through a slow closing; stays put at idle while the two below move, so it is the grip; `% = raw / 31672 * 100` |
| | | **the throttle valve** — 12 bits, `b0hi:b2`, 320 to about 2 310 | moves with the engine idling and the grip untouched: 368 at a cold idle, 176 warm; `% = raw / 16 / 144.4 * 100`, zero provisional |
| `0x759` | 20 Hz | the throttle valve, 8 bits — `b0hi:b2hi`, 21 to 144 | the same: 24 cold, 12 warm; leads `0x758` by one sample, so the commanded value at a guess; `% = raw / 144.4 * 100` |
| `0x750` | 100 Hz | throttle, coarse — byte 7 low nibble, 0–7; otherwise noise | certainly reacts; the noise does not follow lean |
| | | engine speed in whole hundreds of rpm — `b4hi:b1hi`, the same number as `0x27C` byte 0; a second copy in `b4hi:b1lo` | equal in every frame with the engine running |
| `0x755`, `0x764`, `0x765` | 20 / 10 / 10 Hz | throttle: flags and coarse steps | certainly react, not identified |
| `0x755` | 20 Hz | byte 6, bit 0 — set with the clutch lever pulled or either brake applied | 6 of 6, engine off, 2026-10-16. But also set without a break from the restart to the stop of an engine run, with the wheel driven in first gear, so it is not the clutch switch |
| `0x76A` | 2 Hz | ~~battery voltage~~ — **withdrawn**. Byte 2's low nibble is the top nibble of the water temperature's raw value | it steps exactly as `0x512` byte 0 crosses each multiple of 16, seven times, and byte 5's low nibble is the other half; "12.85 V" was a 22 °C engine |
| | | air temperature again — high nibbles of bytes 0 and 2, the same raw value as `0x512` byte 1 | changes at the same moment |
| | | bytes 5 and 7 flip by a count or a few bits | unknown |
| `0x76D` | 2 Hz | single bits that flip now and then; and the cruise buttons above | unknown |
| `0x757`, `0x75F`, `0x769`, `0x76C` | 2–20 Hz | not one bit changes with the engine off; alive with it running — see [the third capture](captures/session-2026-10-11_200550/README.md). *`0x756` was in this row until 2026-10-16; its speed field moves with the engine off whenever the wheel turns* | unknown |
| `0x760`–`0x763`, `0x76B` | 2–10 Hz | not one bit changed in any session. *`0x383` was in this row until 2026-10-16* | unknown |

Looked for and not found:

- **The ride mode as a number 1–5** is in none of them. Only the settings a mode carries
  show: TCS and the 2-bit field, which is PWR (2026-10-16).
- **LIF, SCS and QS** showed in none of the 23, engine off or running: 8 LIF anchors, 7
  SCS and 6 QS in [the third capture](captures/session-2026-10-11_200550/README.md), and not one bit moved at any of them. SCS and LIF changed once more between
  Track 2 and Track 3 on 2026-10-16, and again nothing.
- **The launch-control setting**: on and off once each, engine off, 2026-10-16. Nothing.
- **The shift lever pressed without changing gear**: four brackets, engine off,
  2026-10-16. The shift-sensor candidate in `0x751` read its resting three values
  throughout, and nothing else moved.
- **The sidestand, in neutral**, engine off: nothing. In first gear with the engine running
  it cut the engine, and `0x751` byte 6 read `0x00` for 0.2 s as it did.
- **The high beam**, engine off: nothing. Not tried with the engine running.
- **The start button on its own**: with the start blocked, no field changed for it.
- **Lean** is in none of them either, held against a 30 Hz reference from a phone on the
  bike. See [the third capture](captures/session-2026-10-11_200550/README.md).
- **The grip's forward travel** (the cruise-control cancel) moved nothing, three pushes.
- **Battery voltage** has no candidate any more.
- ~~**Wheel speed** has not been tried with the adapter acknowledging.~~ Found 2026-10-16:
  `0x756`, `0x765` and the pulses in `0x27C` byte 2, above.
- **Two wheel-speed channels beyond these**: nothing else in the 23 ids moved with either
  wheel turning by hand.
- **A second bit for the engine lamp**, one that sets later than the BC bit: nothing set
  and stayed set between 12:41:10.9 and the engine stop at 12:41:27.2 on 2026-10-17, though
  the rider saw the lamp come on in that time.

## Held against AiM's DBC files

Written 2026-10-16, once the captures of that day had named enough to compare. The files
are AiM's protocols for the MT-09 and for the 2015+ R1, copied in
[sources/aim-yamaha/](sources/aim-yamaha/README.md). Neither is about the R9, and AiM's
own guide sends Euro5 MT-09 owners to OBD-II requests rather than to its MT-09 broadcast
protocol, which is why none of the MT-09 file's ids is on this connector. Everything in this
section is a reading of AiM's files against the captures: it names candidates and tests,
and nothing in the summary table above rests on it.

**`0x27C` has the R1's `0x209` layout, bit for bit, in bytes 3 and 4.**

| Bits | AiM, R1 `0x209` | R9 `0x27C`, measured |
|---|---|---|
| 24 | — | the starter bit |
| 25–28 | `TCS_MODE`, 4 bits | TCS, 4 bits since 2026-10-16; it was read as 7 until bit 29 turned up |
| 29–31 | `ECU_GEAR`, 3 bits | the gear with the engine running: 1, 2 and 3 in first, second and third (2026-10-16 and 2026-10-17). ~~bit 29 set in first gear with the engine running; 30 and 31 never set~~ |
| 32–34 | `ECU_GEAR_RAW`, 3 bits | the gear, `7` between gears |
| 38–39 | `ECU_POW_MODE`, 2 bits | PWR − 1 |

Three fields of five land where the R1's are, and the TCS width came from this file before
the captures showed why. That makes it likely that bits 29–31 are a second gear field, the
one the ECU works with, set only with the engine running, and that the "raw" one is what
the box's sensor says. The test is second gear with the engine running on the stand:
bit 30 instead of bit 29, byte 3 `0x42` instead of `0x22` at TCS 1. *Done 2026-10-17: byte 3
read `0x46` in second gear and `0x66` in third, at TCS 3, so the field is the gear as the
ECU has it ([the sixth capture](captures/session-2026-10-17_123714/README.md#second-and-third-gear-0x27c-bits-2931-are-the-gear)).* AiM's own value table
for `ECU_GEAR` has `10` = "-", which 3 bits cannot hold, so the file is not exact here
either.

**AiM's 16-bit fields are very likely big-endian, written down as little-endian.** The R1's
engine speed is declared `8|16@1+`, bytes 1–2 low byte first. On the R9 the same quantity is
bytes 0–1, high byte first, measured. A converter that writes a big-endian field as Intel
with the start bit of its low byte produces exactly `8|16@1+`. Read that way, the R1's
`0x260` loses its impossible ninth byte (its last field becomes bytes 6–7), and the MT-09's
"maybe MAP" in its temperature frame becomes bytes 2–3. The "LE per [S6]" notes in
[background/pre-bike-mapping.md](background/pre-bike-mapping.md) rest on the file's
declaration and not on a measurement.

**Wheel speed: the R1 file has 0.01 km/h for both wheels**, which is the unit the R9's
`0x756` fits on its tyre. That is the only support the unit has besides the tyre's size.
The MT-09 file's two speeds, ×0.0025 and ×0.6 km/h, differ by 240 times and the second
reaches 39 000 km/h; on the R9 both wheels are in one unit. Neither file's plain 16-bit
layout would decode the R9's fields, which are split over nibbles.

**The throttle valve may be in degrees.** The R1's `ECU_THROTTLE` is 10 bits at 0.15° a
count. If it is the R9's 12-bit valve without its two lowest bits, the valve is 0.0375° a
count: fully open at 86.6°, a warm idle at 6.6°, 12° at rest with the engine off. Those are
believable angles for a throttle plate. One coincidence of scale, no measurement; the
percent in use stays. The R1's `ECU_TPS_HAND`, 0.145 % a count, maps onto nothing in the
R9's grip field.

**Engine speed**: the R1 file's ×0.4 is 2.4 % off the R9's exact 100/256, and the MT-09's
0.391 is that number rounded. The MT-09 file's temperatures, ×0.625 − 30, are the R9's
exactly; the R1 file's ×0.66 − 36 is not (17.5 °C against 14.2 at the cold anchor).

## Everything that changes and has no name

All of it, from [the third capture](captures/session-2026-10-11_200550/README.md), so that nothing seen is lost between sessions. Left out
are the fields the summary table names: engine speed, gear, TCS, the mode field, the
temperatures and their copies in `0x76A`, the grip, the valve and the neutral byte. `hi`
and `lo` are a byte's high and low nibble. This is one session at a standstill: a bit that
answers to speed, the brakes or the clutch would not be in it, since none of those was
tried.

**Answers to the throttle**

| Where | What it does |
|---|---|
| `0x750` b7 lo | a coarse level, 0–7 |
| `0x758` b0 lo, and b7 | the same coarse level, 0–7 |
| `0x758` b1, bits 4–5 | three steps, `00` / `10` / `20`, on the way open |
| `0x765` b7 hi | a coarse level, 0–8 |
| `0x755` b5 bit 4 | set whenever the grip is off its rest |
| `0x755` b7 hi | moves only when the grip is moved with the engine off |
| `0x764` b2 | changes with the grip's position over the lower two thirds of the travel, constant above it |

**Comes alive with the engine running**

| Where | What it does |
|---|---|
| `0x750` b0, b2, b5, and b4 lo | nearly every bit moving. *b1 and b4's high bits have since been named: engine speed in hundreds, see the next section* |
| `0x750` b3, bits 4–5 | three values, changing often |
| `0x751` b4 hi | 4 at idle, up to 7 and 9 in the blips |
| `0x751` b1 hi, b5 lo | sixteen values each |
| `0x755` b1 lo, b3, b6 hi | change continuously |
| `0x755` b2 bit 4 | off below 50 °C of coolant, on a growing share of the time through the fifties, steady from 61 °C to the stop |
| `0x755` b4 bit 6 | on from 26 s after the start, dropping out briefly after each blip |
| `0x756` b3, b5, b7 | b5 and b7 never rest |
| `0x759` b5, b7 bits 0–1, b0 lo, b2 lo | b5 takes nearly all 256 values |
| `0x75F` b2 lo, b5 lo | never rest |
| `0x764` b0, b3, b4, b5, b6, b7 | the whole frame moving |
| `0x512` b3 | zero with the engine off, then about 55 falling to 24 as it warms |
| `0x76C` `b1:b0hi:b2hi` | 16 bits around `0x9200`, up after the start and back |

**Changes only at the start, at the stop, or for a moment**

| Where | What it does |
|---|---|
| `0x27C` b3 bit 0 | about half a second as the engine starts |
| `0x27C` b6 bit 7 | set while the engine runs. *Corrected 2026-10-16: set at the first start and not cleared by a stop; see the summary* |
| `0x27C` b2 bit 0 | single-frame pulses while the bike was lifted off the stand and set back. *Named 2026-10-16: the rear wheel's pulses, see the summary* |
| `0x751` b3, bits 1 and 3 | flip at the start and at the stop. *2026-10-17: bit 1 is clear while the engine runs and set otherwise, two starts and two stops; bit 3 is set and cleared in the same frames as `0x27C` b3 bit 0, the starter bit, at all seven edges in that capture* |
| `0x751` b6 bit 4 | set 30 s after the start, cleared at the stop |
| `0x751` b7 bit 2 | set once, a second after the stop |
| `0x756` b2 | steps `06` to `0B` through the start in half a second, unevenly, and back at the stop |
| `0x757` b1, b2, b5 | one frame of non-zero at two of the three blips |
| `0x75F` b1 bit 4 | briefly at the start |
| `0x764` b1 | `44` to `43` for a fraction of a second after each blip |
| `0x769` b1 bit 4 | set with the engine off, cleared 29 s after the start; a half-second pulse after each blip |
| `0x76D` b4, b6 bit 1 | change at the start, the stop and the blips |
| `0x76E` b4 bit 1, b6 bit 0 | set at the start, cleared at the stop |

**Moves by itself, engine off**

| Where | What it does |
|---|---|
| `0x750` b0 lo, b5, b6 | noise |
| `0x751` `b0hi:b2` | a noisy 12-bit value around `0x804`; does not follow lean |
| `0x765` b4 lo | noise |
| `0x76A` b7, bits 2–3 | flip now and then |
| `0x76D` b0, bits 6–7 | flip now and then |

**Not one bit changed**: ~~`0x383`~~ (its brake bit moved on 2026-10-16), `0x760`, `0x761`, `0x762`, `0x763`, `0x76B`.

**Added from the captures of 2026-10-16**, both wheels on stands, the controls bracketed
one by one ([fourth](captures/session-2026-10-16_191234/README.md),
[fifth](captures/session-2026-10-16_192421/README.md)). The named ones are in the summary.

| Where | What it does |
|---|---|
| `0x755` b6 hi | `F` with the engine off; went `F` → `0` and back six times in the second engine run, with the wheel driven in first gear |
| `0x765` b7 bit 4 | on and off from 19:20:20 through both engine runs, held on through the ten seconds the engine was stalled between them, off from 19:21:10.5; never set before the first start or in the fifth capture |
| `0x27C` b3 bit 0 | 0.14 s once, 2 s before a stop with the stop switch, the engine running |
| `0x751` b6 bits 5–6 | not neutral alone; see the summary |

**Added from the capture of 2026-10-17**, the rear wheel driven to 59 km/h in third with the
front standing still, and two key-offs ([the sixth capture](captures/session-2026-10-17_123714/README.md)). The named and the
candidate ones are in the summary.

| Where | What it does |
|---|---|
| `0x27C` b5 bit 3 | 1.9 s in the run-up to 3 700 rpm; byte 5 zero in every other frame of six captures |
| `0x765` b6 bit 1 | the constant `2` in b6 lo: clear from the clutch going out in third to just before the stop; not when the clutch went out in second |
| `0x76D` b1 bits 0, 2, 3, b2 bit 7; `0x76A` b3 bits 2–3, b5 bit 7 | change for one frame at key-on |
| `0x27C` b3 bit 0, `0x751` b3 bit 3, `0x383` b4 bit 3 | set, and `0x27C` b6 bit 7 cleared, in the last frames before the bus goes quiet at key-off, both times |

The most unexplained traffic is in `0x750` and `0x764`, where almost the whole frame
moves once the engine runs.
