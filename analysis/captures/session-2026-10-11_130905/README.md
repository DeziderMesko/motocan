# The second capture, 2026-10-11: the adapter acknowledging

> The write-up of this session, as it was written on the day. The current state of every
> signal is in [r9-signals.md](../../r9-signals.md); where a later session corrected
> something here, that page has the correction.

[`captures/session-2026-10-11_130905/`](./): the same
connector an hour later, `tools/2-session.sh --ack`, 13:09:38 to 13:14:12. Key on, engine
off throughout. 134 449 frames, 15 marks. The adapter transmitted nothing and the watchdog
took it off the bus only when the session ended.

**Acknowledging the frames was the whole problem.** 23 ids instead of two, 506 frames a
second instead of 3 800, and not one repeated frame. `0x27C` arrives 100 times a second,
on the dot. The retransmission reading of the first capture is no longer a reading.

| ID | DLC | Rate | What moved |
|---|---|---|---|
| `0x27C` | 7 | 100 Hz | TCS and the mode-linked bits, as before; engine speed zero |
| `0x383` | 8 | 10 Hz | nothing |
| `0x512` | 4 | 1 Hz | bytes 0 and 1, by one count — temperatures, below |
| `0x750` | 8 | 100 Hz | noisy at rest; byte 7 follows the throttle |
| `0x751` | 8 | 100 Hz | noisy at rest |
| `0x755` | 8 | 20 Hz | throttle |
| `0x756`, `0x757` | 8 | 20 Hz | nothing, all zero |
| `0x758` | 8 | 20 Hz | throttle, several fields |
| `0x759` | 8 | 20 Hz | throttle |
| `0x75F`, `0x760`–`0x763` | 8 | 10 Hz | nothing |
| `0x764`, `0x765` | 8 | 10 Hz | throttle |
| `0x769`, `0x76B`, `0x76C` | 8 | 2 Hz | nothing |
| `0x76A`, `0x76D` | 8 | 2 Hz | single bits flickering, unexplained |
| `0x76E` | 8 | 2 Hz | byte 1, with the ride mode |

**Still none of the predicted ids**, with the bus now running as it is meant to. The R9
does not speak the FZ09MT09 profile on this connector, nor the R1-style one: its traffic is
`0x27C`, `0x383`, `0x512` and a block from `0x750` to `0x76E`. Every 🟡 row below that
names `0x20A`, `0x216`, `0x236` or `0x23E` is now a prediction that failed here, though the
*equations* may yet carry over — the temperatures suggest they do.

**The OBD reading above is no longer needed to explain anything.** It was offered for a
listener that saw no temperatures, throttle or speed; they were being broadcast all along
and never got out. Whether the port also answers OBD requests is untested and nothing now
depends on it.

**What this capture adds**, bytes numbered from 0:

| Signal | Where | Evidence |
|---|---|---|
| TCS | `0x27C` byte 3, `>> 1` | anchors 9 and 3 read `0x12` and `0x06`, and the walk down from 9 to 3 shows every step. With the first capture that is 5 anchors of 5 |
| ride mode | not found as a number | named keys this time. Sport `06 40`, street `0A 80`, rain `0E C0`, custom 1 `08 00` in `0x27C` bytes 3–4, as guessed from the first capture; custom 2 reads `12 40`, which is TCS 9 and the value the rider left it at an hour earlier. So the frame carries the settings, and the custom modes carry whatever they were last set to |
| the 2-bit setting | `0x27C` byte 4 top 2 bits, and `0x76E` byte 1 | the same value in both, changing in the same 100 ms: 1 sport, 2 street, 3 rain, 0 custom 1, 1 custom 2. Still unnamed; PWR is the guess, and the rider's PWR per mode would settle it |
| throttle | `0x750`, `0x755`, `0x758`, `0x759`, `0x764`, `0x765` | both sweeps (60.0–63.6 s and 71.5–77.4 s) sit inside the two marked windows and nothing in these frames moves outside them. The fields, their ranges and steps are under [The throttle, field by field](#the-throttle-field-by-field) below |
| water temperature | `0x512` byte 0, **candidate** | `0x54`/`0x55`. At [S6]'s `raw*0.625 - 30` that is 22.5–23.1 °C against 22 on the dash. One anchor, cold engine, no span |
| air temperature | `0x512` byte 1, **candidate** | `0x4F`/`0x50`, 19.4–20 °C by the same line against 18 on the dash — 1.4 to 2 °C high. The same raw value sits in `0x76A`, split across the high nibbles of bytes 0 and 2, and steps from `0x4F` to `0x50` within 0.1 s of `0x512` doing so |
| battery voltage | `0x76A`, `(b2&15)<<8 \| b3`, **candidate** | `0x505`/`0x506` all session, flickering by one count: 12.85–12.86 V at ×0.01. There is no dash reading behind this. What there is: a multimeter read 12.9 V on the pigtail's battery pin while the wiring was being checked, key off, and the rider expected about 12.8 V after the ignition had been left on for a long while with the lights and the dash drawing from it. 12.85 V with the key on sits between the two, which is where a battery a little under its resting voltage should be. Found by searching every byte and nibble field for a value in that window; the other hits were round constants that never moved (`0x80`, `0x100`, `0x200`, `0x500`) and are not counted. Engine off throughout, so the test is starting it: the value should go to about 14 V |

## The throttle, field by field

Two sweeps with the engine off: a fast opening (0.4 s), a hold at the stop, a slow closing
(2.5 s). `hi` and `lo` are a byte's high and low nibble; a field written `b7lo:b6lo:b5` is
those nibbles and bytes in that order, most significant first.

| Where | Bits | At rest | At the stop | Step | Rate |
|---|---|---|---|---|---|
| `0x758` `b7lo:b6lo:b5` | 15 | 256 | 31 672 | 1 — about 31 400 over the travel | 20 Hz |
| `0x758` `b0hi:b2` | 12 | 320, flickering 304–336 | 2 299–2 315 | 1 while moving — about 2 000 | 20 Hz |
| `0x759` `b0hi:b2hi` | 8 | 21 | 144 | 1 — 123 | 20 Hz |
| `0x750` byte 7, low nibble | 3 | 0 | 7 | 1 — eight levels | 100 Hz |

The coarse 0–7 level is also in `0x758` (byte 7, and byte 0's low nibble) and a 0–8 one in
`0x765` (byte 7's high nibble). `0x755` byte 5 bit 4 is set whenever the grip is off its
rest.

**The 15-bit field is one number, and it was found by putting the coarse level on top.**
The low 12 bits alone, `b6lo:b5`, look like noise during a sweep — 812, 3 248, 1 092,
3 822 — because they wrap eight times across the travel. With byte 7 as the top three bits
the same samples read 29 484, 27 824, 25 668, 24 302, and the whole slow closing, 49
samples, falls without one step back. Held at the stop it flickers between 31 672 and
31 673, so the last bit is live. At rest it does not: it sits at 256 and twice went to 512,
a step of 256.

**The three fine fields look like one signal at three resolutions.** Divided by 256 the
15-bit field runs 1.00 to 123.7; the 8-bit one runs 21 to 144, which is the same 123
counts with an offset of about 20. So an 8-bit base value and a fraction under it — which
is a guess from the numbers, not a decode. What a count means (percent, degrees, sensor
volts) is not known.

**`0x759` leads `0x758`.** On the opening the 8-bit field in `0x759` has moved one sample
(50 ms) before the 12-bit field in `0x758`, and it stays ahead on the closing. At rest it
is perfectly still, 1 100 samples at 21, where the 12-bit field flickers. That is the
shape of a commanded value against a measured one, or of the grip against the valve the
ECU moves — and this capture cannot say which, least of all with the engine off.

**The rest position is not the bottom of the travel.** The rider's note, after seeing
these numbers: the grip also turns *forward* past closed, which is what cancels the cruise
control. That would be why rest reads 21 and 1.00 and not zero, and the step from 256 to
512 at rest may be the edge of it. `tools/2-session.sh` has had a `c` key in the throttle
task since, which brackets a forward push on its own so that it is never inside a roll.

**What limits this.** The fine fields come 20 times a second, so the fast opening is eight
samples; only the eight-level field is at 100 Hz. And the engine was off: a field that is
the throttle valve may behave differently once the ECU has a reason to move it. Next time:
one very slow opening and closing, the forward push, and the same again with the engine
running.

**Not found**: LIF, on two anchors, in any of the 23 frames. `0x750` and `0x751` carry
12-bit values that are noisy with the bike standing still, at 100 Hz — the shape of an IMU,
and the lean was not repeated this time, so there is nothing to hold them to. The first
capture's note on `0x751` ("high nibbles of bytes 0 and 2") was the top of one of these:
`(b0>>4)<<8 | b2`, resting at `0x804`.

**Next, in order of what one session buys**: engine running with rpm read off the dash, for
the scale of `0x27C` bytes 0–1; a warm-up with several temperature anchors, for `0x512`;
lean, for `0x750`/`0x751`; SCS, and LIF again; the rear wheel on a stand. Starting the
engine also settles the battery-voltage candidate in `0x76A` for free.
