# RaceChrono channels for the R9

What to type into RaceChrono so that it decodes what Bluecan streams. Nine channels from
four frames. The signals themselves, and how sure each one is, are on
[the front page](../README.md#what-is-decoded-so-far) and in
[r9.dbc](../analysis/r9.dbc); this page is those equations in the form the app accepts.

**Status, 2026-10-13.** Every equation here has been entered into RaceChrono Pro on
Android and shown the right values, against a replay of the R9's own captures on the
bench. None of it has been on the bike. Each was also computed outside the app on every
distinct payload in [the captures](../analysis/captures/) and agrees with the DBC.

## Where they go

In the **vehicle profile**, with `+ Add channel` under **CAN-Bus channels** — not the one
under *OBD-II channels*. Each channel is four things: the **Channel** to map onto (and a
**Channel postfix**), a **PID**, an **Equation**, and a *Source data* row that shows the
live bytes for that PID when *Use live data* is ticked, with the result under *Solution*.

Three things about the app that cost time here:

- **The PID is entered in decimal.** `0x27C` is `636`.
- **Long equations are refused**, with a complaint about the number of brackets, though
  the brackets balance. Two forms of the grip equation went that way: one with `|` and
  `<<`, and one of 117 characters built from `bytesToUint(...) & 15`. Whether it is the
  length or the operators was not settled; the short form below is the one that went in.
- **Write `100.0`, not `100`.** Dividing two integers rounds down, so a percentage built
  from integers comes out as a whole number or as zero.

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
bytesToUint(raw, 3, 1) >> 1
```

The mode field — sport `1`, street `2`, rain `3`, custom 1 `0`. It follows the ride mode;
[what it is](../analysis/open-questions.md) is still open:

```
bytesToUint(raw, 4, 1) >> 6
```

## PID 1298 — `0x512`, 1 Hz

Water temperature, °C:

```
bytesToUint(raw, 0, 1) * 0.625 - 30
```

Intake air temperature, °C. The dash shows a degree or two less:

```
bytesToUint(raw, 1, 1) * 0.625 - 30
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
| **Digital 2** | **the mode field** | 636 |
| Coolant temperature | water temperature | 1298 |
| Air temperature | intake air temperature | 1298 |
| Throttle position 1 | the throttle grip | 1880 |
| Throttle position 2 | the throttle valve as measured | 1880 |

That is eight channels on three PIDs. The ninth, the valve as commanded on PID 1881, has
its equation above and was not entered, so a recording session asks Bluecan for three ids
and its BUS page reads `flt 3`.

**TCS and the mode field have no channel of their own**, and went in as `Digital`.
Despite the name it is not a 0-or-1 channel: it shows the number the equation gives, so
TCS reads 7 and the mode field 3.

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

## What it costs the link

Four ids with all nine channels, three as entered so far. Recording, the app asks for
each at 50 ms, so about 61 frames a second cross the radio with four — 41 with three — out
of the 506 on the bus — [numbers.md](numbers.md). The same four are what
Bluecan decodes for its own LIVE page, which does not depend on any of this: it reads the
frames ahead of the app's filter.
