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

## What the R9 sends, as of 2026-10-11

The summary of the three captures, for whoever opens this page wanting the answer rather
than the argument. 23 ids on the diagnostic connector; bytes numbered from 0. The evidence
for each row is in the three write-ups under [captures/](captures/).

| ID | Rate | What is in it | How sure |
|---|---|---|---|
| `0x27C` | 100 Hz | gear — byte 4, low 3 bits; `7` = between gears | confirmed, 12 anchors of 12 |
| | | TCS — byte 3 `>> 1` | confirmed, 5 anchors of 5 |
| | | byte 3, bit 0 — set for about half a second as the engine starts | seen at both starts captured and at no other time. That is when it changes, not what it means: the starter is a guess |
| | | engine speed — bytes 0–1, big-endian, `raw * 100 / 256` rpm (×0.390625): byte 0 is hundreds of rpm, byte 1 the fraction | the sound of the engine gave 0.3915 ± 1.5 % at idle; the hundreds sent separately in `0x750` give the exact form and hold to 3 400 rpm |
| | | a 2-bit setting that goes with the ride mode — byte 4, top 2 bits; PWR at a guess | the value tracks the modes, the meaning is unknown. **Open**: custom 2 read `2` in the first capture and `1` in the two after it — see [the third capture](captures/session-2026-10-11_200550/README.md) |
| | | byte 6, bit 7 — set while the engine runs | set within half a second of both starts seen, cleared at the one stop seen. That is when it changes, not what it means: nothing says the bit *is* "engine running" rather than something else that comes on with the engine |
| | | byte 2 — `01` for a single frame, six times | only while the bike was being lifted off the stand and set back; unexplained |
| `0x512` | 1 Hz | water temperature — byte 0, `raw*0.625 - 30` °C | confirmed, 6 anchors from 17 to 76 °C, within 2 °C of the dash |
| | | air temperature — byte 1, same equation | found: after a cold soak it reads the same raw value as the water sensor, 76; the dash shows it a degree or two lower than the equation does (16 against 17.5) |
| | | byte 3 — zero with the engine off, about 55 falling to 24 as it warms | unknown |
| `0x751` | 100 Hz | neutral / in gear — byte 6, `0x60` / `0x00` | agrees with every change into and out of neutral |
| | | byte 6 goes `0x60` → `0x70` half a minute after a start, in both sessions with one | unknown |
| | | a noisy 12-bit value | does not follow lean — see [the third capture](captures/session-2026-10-11_200550/README.md) |
| `0x76E` | 2 Hz | the same 2-bit setting as `0x27C` — byte 1 | changes at the same moments |
| `0x758` | 20 Hz | **the grip** — 15 bits, `b7lo:b6lo:b5`, 256 or 512 at rest with the engine off, 0 with it running, 31 672 at the stop | one number, falls without a step back through a slow closing; stays put at idle while the two below move, so it is the grip; `% = raw / 31672 * 100` |
| | | **the throttle valve** — 12 bits, `b0hi:b2`, 320 to about 2 310 | moves with the engine idling and the grip untouched: 368 at a cold idle, 176 warm; `% = raw / 16 / 144.4 * 100`, zero provisional |
| `0x759` | 20 Hz | the throttle valve, 8 bits — `b0hi:b2hi`, 21 to 144 | the same: 24 cold, 12 warm; leads `0x758` by one sample, so the commanded value at a guess; `% = raw / 144.4 * 100` |
| `0x750` | 100 Hz | throttle, coarse — byte 7 low nibble, 0–7; otherwise noise | certainly reacts; the noise does not follow lean |
| | | engine speed in whole hundreds of rpm — `b4hi:b1hi`, the same number as `0x27C` byte 0; a second copy in `b4hi:b1lo` | equal in every frame with the engine running |
| `0x755`, `0x764`, `0x765` | 20 / 10 / 10 Hz | throttle: flags and coarse steps | certainly react, not identified |
| `0x76A` | 2 Hz | ~~battery voltage~~ — **withdrawn**. Byte 2's low nibble is the top nibble of the water temperature's raw value | it steps exactly as `0x512` byte 0 crosses each multiple of 16, seven times, and byte 5's low nibble is the other half; "12.85 V" was a 22 °C engine |
| | | air temperature again — high nibbles of bytes 0 and 2, the same raw value as `0x512` byte 1 | changes at the same moment |
| | | bytes 5 and 7 flip by a count or a few bits | unknown |
| `0x76D` | 2 Hz | single bits that flip now and then | unknown |
| `0x756`, `0x757`, `0x75F`, `0x769`, `0x76C` | 2–20 Hz | not one bit changes with the engine off; alive with it running — see [the third capture](captures/session-2026-10-11_200550/README.md) | unknown |
| `0x383`, `0x760`–`0x763`, `0x76B` | 2–10 Hz | not one bit changed in any session | unknown |

Looked for and not found:

- **The ride mode as a number 1–5** is in none of them. Only the settings a mode carries
  show: TCS and the 2-bit field.
- **LIF, SCS and QS** showed in none of the 23, engine off or running: 8 LIF anchors, 7
  SCS and 6 QS in [the third capture](captures/session-2026-10-11_200550/README.md), and not one bit moved at any of them.
- **Lean** is in none of them either, held against a 30 Hz reference from a phone on the
  bike. See [[the third capture](captures/session-2026-10-11_200550/README.md)](captures/session-2026-10-11_200550/README.md).
- **The grip's forward travel** (the cruise-control cancel) moved nothing, three pushes.
- **Battery voltage** has no candidate any more.
- **Wheel speed** has not been tried with the adapter acknowledging.

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
| `0x27C` b6 bit 7 | set while the engine runs |
| `0x27C` b2 bit 0 | single-frame pulses while the bike was lifted off the stand and set back |
| `0x751` b3, bits 1 and 3 | flip at the start and at the stop |
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

**Not one bit changed**: `0x383`, `0x760`, `0x761`, `0x762`, `0x763`, `0x76B`.

The most unexplained traffic is in `0x750` and `0x764`, where almost the whole frame
moves once the engine runs.
