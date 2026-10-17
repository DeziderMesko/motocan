# The fifth capture, 2026-10-16: the switches, engine off

> The write-up of this session, as it was written on the day. The current state of every
> signal is in [r9-signals.md](../../r9-signals.md); where a later session corrects
> something here, that page has the correction.

[`captures/session-2026-10-16_192421/`](./):
`tools/2-session.sh --ack`, 19:24:27 to 19:29:42, 155 309 frames, 28 marks, the same 23 ids
at 506 frames a second, nothing transmitted. Taken straight after
[the first capture of the evening](../session-2026-10-16_191234/README.md) with the
ignition left on: the rider's word, 2026-10-16, and the frames agree, since states the
engine left behind in the first capture are still set at the start of this one. The engine
was off throughout, both wheels off the ground, neutral except from 19:28:34.5 to 19:28:59.1.

**The CANable's 120 Ω terminator was on** for this session, as
[stand-session.md](../../../journal/stand-session-2026-10-16.md) asked — the rider's word, 2026-10-16. The
session script has no field for it, so `notes.txt` does not say. There were no bus errors.

Each control was bracketed by a mark before and after (task `9` of the session script),
and the rider worked most of them more than once inside the bracket, to be sure. A field
is listed only if it changed inside the bracket and does not change in the quiet stretch
19:24:30–19:25:15.

## What answered

| Control | Bracket | What changed | Times |
|---|---|---|---|
| Clutch lever | 19:26:08.7–17.3 | `0x755` b6 bit 0: 19:26:10.6–12.0, 13.7–15.9 | 2 of 2 pulls |
| Front brake lever | 19:26:19.4–28.9 | `0x383` b4 bit 3 and `0x755` b6 bit 0, together: 19:26:21.7–23.5, 24.8–25.9 | 2 of 2 |
| Rear brake pedal | 19:26:31.5–41.1 | the same two: 19:26:33.9–35.4, 36.2–37.6 | 2 of 2 |
| Cruise RES+ | 19:27:59.1–28:08.6 | `0x76D` b3 bit 0: 19:28:03.7–04.2, 05.7–07.2 | 2 presses |
| Cruise SET− | 19:28:10.4–16.8 | `0x76D` b5 bit 4: 19:28:12.2–12.7, 14.7–16.2 | 2 presses |
| Front wheel by hand | 19:27:20.6–29.7 | `0x765` to 725, and `0x756` to 272 at the second push, with no rear pulses | see the first capture |
| Rear wheel by hand | 19:27:35.1–47.9 | `0x756` to 473, `0x27C` byte 2: 80 pulses, 46.4 per revolution of `0x756`; `0x765` stayed at zero | |

**`0x383` byte 4 bit 3 is the brake**: either lever, each pull, from the same frame to the
same frame, and nothing else in `0x383` moved. This is the first time anything in `0x383`
has changed in any capture. In the first capture of the evening it is set twice while the
rear wheel was being stopped with the engine running.

**`0x755` byte 6 bit 0 follows the clutch and both brakes**, all six pulls. In the first
capture it was set without a break from the restart to the stop, with the rear wheel driven
in first gear, so it is not the clutch switch alone. Something that is set by the clutch,
by either brake, and by a running engine in gear is what the cruise control would need to
know before it may engage. That is a guess.

**The two cruise buttons have a bit each in `0x76D`**, a frame sent twice a second, so a
press shorter than half a second can fall between frames. Both presses of each button
showed.

## The blocked start

Second gear, sidestand down, the start button pressed: the starter did not turn, as it
should not (the rider's account). **`0x27C` byte 3 bit 0 did not set.** Byte 3 read `06`
in every frame of the session. At both starts captured before, and at the restart in the
first capture, the bit was set for about half a second as the starter turned. So the bit
follows the starter, or the ECU's decision to run it, and not the button.

`0x751` byte 6 went `60` → `40` when second gear went in (19:28:34.6), `40` → `00` at
19:28:45.4, inside the bracket, and back to `60` in neutral (19:28:59.4). What happened at
19:28:45.4 is not in the marks; the button press or the clutch let out are the candidates.
With this and the first capture's sequence, byte 6 is not simply "neutral": see
[r9-signals.md](../../r9-signals.md).

## Looked for and not found

- **The shift lever, pressed up and down without changing gear**, four brackets
  (19:25:18–35, 19:25:40–50, 19:29:19–27, 19:29:28–35). The 12-bit value in `0x751`,
  `(b0 >> 4) << 8 | b2`, the shift-sensor candidate, read 2 038, 2 054 or 2 070 in every
  frame, the same three values and the same median as at rest. Nothing else moved but the
  temperature fields. The
  engine was off; whether the sensor is read with it off is not known.
- **The sidestand**, up and down in neutral (19:25:55–19:26:05): nothing. In gear it does
  something, since it cut the engine in the first capture, but that was not bracketed here.
- **The high beam** (19:26:49–19:27:02), engine off: nothing. The test in
  [stand-session.md](../../../journal/stand-session-2026-10-16.md) asks for it with the engine idling, as a load
  on the idle control, and that was not done.
