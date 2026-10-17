# RaceChrono channels for the R9

What to type into RaceChrono so that it decodes what Bluecan streams. Twelve channels
from seven frames. The signals themselves, and how sure each one is, are on
[the front page](../README.md#what-is-decoded-so-far) and in
[r9.dbc](../analysis/r9.dbc); this page is those equations in the form the app accepts.

**Status, 2026-10-13.** Every equation here has been entered into RaceChrono Pro on
Android and shown the right values, against a replay of the R9's own captures on the
bench — except what was added or corrected on 2026-10-16 and has not been entered yet:
TCS, PWR, the two wheel speeds and the Track display. Those were computed outside the app
on every frame of the two captures of that day and agree with the DBC. The two wheel
speeds were given their top four bits on 2026-10-17, from a capture that went past 41 km/h,
and agree with the DBC on every frame of all six captures.
**2026-10-17: everything on this page except the Track display has been entered**, TCS,
PWR, both wheel speeds and the valve as commanded among it. The app accepted every
equation as typed, the 110-character front wheel's too. The values have not been seen in
the app yet, against a replay or on the bike. *(Later on 2026-10-17: against a replay, the
wheel speeds read about 3.6 times too fast in the app, because it takes speed in m/s;
both equations now divide by 360.0. The corrected ones have not been entered yet.)* The Track display was left out by choice. None of it has been on the bike. Each was also computed outside the app on every
distinct payload in [the captures](../analysis/captures/) and agrees with the DBC.

## Where they go

In the **vehicle profile**, with `+ Add channel` under **CAN-Bus channels** — not the one
under *OBD-II channels*. Each channel is four things: the **Channel** to map onto (and a
**Channel postfix**), a **PID**, an **Equation**, and a *Source data* row that shows the
live bytes for that PID when *Use live data* is ticked, with the result under *Solution*.

Four things about the app that cost time here:

- **The PID is entered in decimal.** `0x27C` is `636`.
- **Long equations are refused**, with a complaint about the number of brackets, though
  the brackets balance. Two forms of the grip equation went that way: one with `|` and
  `<<`, and one of 117 characters built from `bytesToUint(...) & 15`. Whether it is the
  length or the operators was not settled; the short form below is the one that went in.
- **Write `100.0`, not `100`.** Dividing two integers rounds down, so a percentage built
  from integers comes out as a whole number or as zero.
- **A speed channel takes metres a second**, and the app converts it to km/h for
  display. The bus counts 0.01 km/h, so the wheel speeds divide by 360.0, not 100.0.
  Divided by 100.0 they read 3.6 times too fast: seen on 2026-10-17 against a replay of
  [session-2026-10-17_123714](../analysis/captures/session-2026-10-17_123714/README.md),
  where Bluecan's screen read right and the app did not.

The functions are RaceChrono's own, documented at
[racechrono.com/support/equations](https://racechrono.com/support/equations).
`bitsToUintLe(raw, offset, length)` numbers its bits the way a DBC numbers a
little-endian signal, so `40|12@1` in the DBC is `bitsToUintLe(raw, 40, 12)` here.

## PID 636 — `0x27C`, 100 Hz

Engine speed, rpm:

```
bytesToUint(raw, 0, 2) * 100.0 / 256
```

Gear — `0` is neutral, `1`–`6`, and `7` while the box is between two gears:

```
bytesToUint(raw, 4, 1) & 7
```

To leave the between-gears moments out of a graph instead of showing them as 7:

```
if((bytesToUint(raw, 4, 1) & 7) == 7, NaN, bytesToUint(raw, 4, 1) & 7)
```

TCS, the number on the dash:

```
bitsToUintLe(raw, 25, 4)
```

That is bits 1–4 of byte 3. Until 2026-10-16 this page gave `bytesToUint(raw, 3, 1) >> 1`,
which is right with the engine off and 16 too high in gear with the engine running: bit 5
of the same byte is set then ([r9-signals.md](../analysis/r9-signals.md)). Every capture
before 2026-10-16 had the engine off or in neutral, so the replay on the bench could not
show it. **If the old equation is in the app, replace it.** The new one has not been
typed into the app yet; it is the same function and form as the grip's, which the app
accepts.

PWR, 1–4, the number on the dash:

```
bitsToUintLe(raw, 38, 2) + 1
```

This replaces the "mode field" equation, `bytesToUint(raw, 4, 1) >> 6`, which gave the
same thing less one. The field was named on 2026-10-16 from photos of the dash. It is PWR
and not the ride mode: the mode's own number is not on this bus, and two modes with the
same settings send the same bytes. The Track display below is the one thing about the mode
that is.

## PID 1298 — `0x512`, 1 Hz

Water temperature, °C:

```
bytesToUint(raw, 0, 1) * 0.625 - 30
```

Intake air temperature, °C. The dash shows a degree or two less:

```
bytesToUint(raw, 1, 1) * 0.625 - 30
```

## PID 1878 — `0x756`, 20 Hz

Rear wheel speed, entered in m/s and shown by the app in km/h:

```
(bitsToUintLe(raw, 28, 4) * 4096 + bitsToUintLe(raw, 4, 8) * 16 + bitsToUintLe(raw, 20, 4)) / 360.0
```

`bitsToUintLe(raw, 4, 8)` takes byte 0's high nibble and byte 1's low one together, which
in this order are bits 4–11 of the speed; that keeps the equation short. Held to the
wheel's own pulses on a stand, 0.01 km/h a count makes the wheel 1.936 m round, which is
what the tyre is. The dash showed 28 km/h where this read 25.2, once, which is how far a
speedometer reads high.

## PID 1893 — `0x765`, 10 Hz

Front wheel speed, the same unit:

```
(bitsToUintLe(raw,4,4)*4096+bitsToUintLe(raw,20,4)*256+bitsToUintLe(raw,52,4)*16+bitsToUintLe(raw,24,4))/360.0
```

Written without spaces because it is 110 characters even so, longer than any equation the
app has been seen to take. If it is refused, leave out the first term: the rest is right
up to 40.95 km/h and starts again from zero above it.

On a stand with the rear wheel turning alone, this takes the rear's speed after some
seconds while the front stands still. On the road both turn together, so that should not
show; on a stand, do not read it as the front
([open question 8](../analysis/open-questions.md)).

## PID 1902 — `0x76E`, 2 Hz

`1` with the dash on its Track display, where the modes are Track 1–4, and `0` on the
normal one:

```
if(bytesToUint(raw, 2, 1) == 16, 1, 0)
```

## PID 1880 — `0x758`, 20 Hz

Throttle grip, %. Reads 0 for the first twentieth of the travel:

```
(bitsToUintLe(raw, 56, 4) * 4096 + bitsToUintLe(raw, 40, 12)) * 100.0 / 31672
```

Throttle valve as measured, %. The zero is provisional — a warm idle reads about 8 %:

```
((bytesToUint(raw, 0, 1) >> 4) * 256 + bytesToUint(raw, 2, 1)) * 100.0 / 2310.4
```

## PID 1881 — `0x759`, 20 Hz

Throttle valve as commanded, %. Leads the measured one by a sample:

```
((bytesToUint(raw, 0, 1) >> 4) * 16 + (bytesToUint(raw, 2, 1) >> 4)) * 100.0 / 144.4
```

The two valve equations have a shorter form each, should the app ever refuse these. They
give the same number on every payload in the captures — 476 distinct ones of `0x758` and
2 025 of `0x759`:

```
(bitsToUintLe(raw, 4, 4) * 256 + bitsToUintLe(raw, 16, 8)) * 100.0 / 2310.4
```

```
(bitsToUintLe(raw, 4, 4) * 16 + bitsToUintLe(raw, 20, 4)) * 100.0 / 144.4
```

## Which channel to map each one onto

RaceChrono offers a fixed list of channels, and this is what the R9's went onto. The
names are the app's, read off its Live screen on 2026-10-13:

| In RaceChrono | Is | PID |
|---|---|---|
| Engine RPM | engine speed | 636 |
| Gear | gear | 636 |
| **Digital 1** | **TCS** | 636 |
| **Digital 2** | **PWR** (was the mode field, less one) | 636 |
| Coolant temperature | water temperature | 1298 |
| Air temperature | intake air temperature | 1298 |
| Throttle position 1 | the throttle grip | 1880 |
| Throttle position 2 | the throttle valve as measured | 1880 |
| Throttle position 3 | the throttle valve as commanded, 2026-10-17 | 1881 |
| Wheel speed rear | rear wheel speed, 2026-10-17 | 1878 |
| Wheel speed front | front wheel speed, 2026-10-17 | 1893 |

That is eight channels on three PIDs, as of 2026-10-13. On 2026-10-17 three more went in:
the valve as commanded on PID 1881 and the two wheel speeds on 1878 and 1893, mapped as in
the table. A recording session now asks
Bluecan for six ids, so its BUS page should read `flt 6`. The Track display, which would be
`Digital 3`, was not entered.

**TCS and PWR have no channel of their own**, and went in as `Digital`.
Despite the name it is not a 0-or-1 channel: it shows the number the equation gives, so
TCS reads 7 and PWR 4. (The replay below was with the old mode-field equation, which read
3 for the same frame.)

**Neither can be given a name.** The **Channel postfix** field is how two channels of the
same kind are told apart in a session, but it is a choice from the app's own list — a
number — not free text. An earlier version of this page said to type `TCS` and `Mode` into
it, and that cannot be done. So `Digital 1` and `Digital 2` they stay, and the table above
is where which is which is kept.

Whether the app lets a user define a channel of their own was looked for in its
documentation and not found.

What that screen read, against a replay of
[the third capture](../analysis/captures/session-2026-10-11_200550/README.md) with the
engine idling: 1609 rpm, gear 0, Digital 1 at 7, Digital 2 at 3, coolant 31 °C, air 21 °C,
throttle position 1 at 0 % and 2 at 14 %. The capture at that point holds TCS 7, mode 3,
water 31.8 °C, air 21.8 °C, the grip at rest and the valve between 13 and 14.5 %.

## Time and the rates on the Live screen

With the channels reading correctly, the app's Live screen still showed an empty **Time**
tile under the CAN-Bus reader and `- Hz` under every channel. The GPS receiver was not
connected at the time — the screen said it was still connecting to it.

That is probably the whole explanation, and it is not confirmed. A frame sent to
RaceChrono over this protocol is an id and a payload with no timestamp in it
([the protocol's own description](https://github.com/aollin/racechrono-ble-diy-device)),
so the app has to time the data itself, and a search turned up RaceChrono's developer
telling a forum user that the Hz and time readings appear once the GPS has a lock. That
reply could not be opened and read here, and the app's documentation does not say either
way. The test is the obvious one: the same screen with the GPS receiver connected and
locked.

## What it costs the link

Seven ids with all twelve channels; six as entered on 2026-10-17, all but `0x76E`.
Recording, the app asks for each at 50 ms, so about 61 frames a second cross the radio with
the first four — 41 with three — out of the 506 on the bus — [numbers.md](history/numbers.md).
The six entered come to about 91. The three added on
2026-10-16 come slower than that: `0x756` at 20 a second, `0x765` at 10 and `0x76E` at 2,
so all seven come to about 93. The same seven are what Bluecan decodes for its own LIVE
page, which does not depend on any of this: it reads the frames ahead of the app's filter.
