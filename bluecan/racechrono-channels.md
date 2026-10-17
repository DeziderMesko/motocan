# RaceChrono channels for the R9

What to enter in RaceChrono so that it decodes what Bluecan streams. What each signal is,
and how sure: [the front page](../README.md#what-is-decoded-so-far) and
[r9.dbc](../analysis/r9.dbc).

**Status, 2026-10-17.** Every channel below except the Track display is entered in
RaceChrono Pro on Android and is in [the export](#the-export). Every equation agrees with
the DBC on every distinct payload in [the captures](../analysis/captures/). Not yet seen on
the bike.

## Entering a channel

Vehicle profile → **CAN-Bus channels** → `+ Add channel` (not the one under *OBD-II
channels*). Fill in **Channel**, **PID** and **Equation**; with *Use live data* ticked the
app shows the PID's bytes and the result.

- **PID in decimal:** `0x27C` is `636`.
- **Write `100.0`, not `100`:** an integer divided by an integer rounds down.
- **Speed is in m/s;** the app shows it in km/h. The bus counts 0.01 km/h, so divide by
  `360.0`.
- `bitsToUintLe(raw, start, length)` numbers bits as a DBC does a little-endian signal:
  `40|12@1` is `bitsToUintLe(raw, 40, 12)`. The functions:
  [racechrono.com/support/equations](https://racechrono.com/support/equations).

## The channels

| Channel in the app | Signal | PID | Equation |
|---|---|---|---|
| Engine RPM | engine speed, rpm | 636 | `bytesToUint(raw, 0, 2) * 100.0 / 256` |
| Gear | 0 neutral, 1–6, 7 between gears | 636 | `bytesToUint(raw, 4, 1) & 7` |
| Digital 1 | TCS, as on the dash | 636 | `bitsToUintLe(raw, 25, 4)` |
| Digital 2 | PWR 1–4, as on the dash | 636 | `bitsToUintLe(raw, 38, 2) + 1` |
| Coolant temperature | water, °C | 1298 | `bytesToUint(raw, 0, 1) * 0.625 - 30` |
| Air temperature | intake air, °C | 1298 | `bytesToUint(raw, 1, 1) * 0.625 - 30` |
| Throttle position 1 | grip, % | 1880 | `(bitsToUintLe(raw, 56, 4) * 4096 + bitsToUintLe(raw, 40, 12)) * 100.0 / 31672` |
| Throttle position 2 | valve as measured, % | 1880 | `(bitsToUintLe(raw, 4, 4) * 256 + bitsToUintLe(raw, 16, 8)) * 100.0 / 2310.4` |
| Throttle position 3 | valve as commanded, % | 1881 | `(bitsToUintLe(raw, 4, 4) * 16 + bitsToUintLe(raw, 20, 4)) * 100.0 / 144.4` |
| Wheel speed rear | rear wheel | 1878 | `(bitsToUintLe(raw, 28, 4) * 4096 + bitsToUintLe(raw, 4, 8) * 16 + bitsToUintLe(raw, 20, 4)) / 360.0` |
| Wheel speed front | front wheel | 1893 | `(bitsToUintLe(raw,4,4)*4096+bitsToUintLe(raw,20,4)*256+bitsToUintLe(raw,52,4)*16+bitsToUintLe(raw,24,4))/360.0` |
| Digital 3 | Track display: 1 on, 0 off | 1902 | `if(bytesToUint(raw, 2, 1) == 16, 1, 0)` — not entered |

| PID | Id | Rate |
|---|---|---|
| 636 | `0x27C` | 100 Hz |
| 1298 | `0x512` | 1 Hz |
| 1878 | `0x756` | 20 Hz |
| 1880 | `0x758` | 20 Hz |
| 1881 | `0x759` | 20 Hz |
| 1893 | `0x765` | 10 Hz |
| 1902 | `0x76E` | 2 Hz |

Worth knowing when reading the values:

- **Digital 1–3** are TCS, PWR and the Track display because the app has no channel for
  them. *Digital* shows the equation's number, not just 0 or 1. The *Channel postfix* is a
  number from a list, not a name, so this table is the only key.
- **Gear** without the 7s, as gaps in a graph:
  `if((bytesToUint(raw, 4, 1) & 7) == 7, NaN, bytesToUint(raw, 4, 1) & 7)`.
- **Air temperature:** the dash shows a degree or two less.
- **Throttle position 1** reads 0 for the first twentieth of the grip's travel.
- **Throttle position 2:** the zero is provisional; a warm idle reads about 8 %.
- **Wheel speed front,** on a stand with only the rear wheel turning, takes the rear's
  speed after a few seconds. Do not read it as the front there
  ([open question 8](../analysis/open-questions.md)).
- **Without a GPS fix** the Live screen shows an empty Time tile and `- Hz` under every
  channel; the values still show.

## The export

[racechrono/vehicle_2026_yamaha_r9.rcz](racechrono/vehicle_2026_yamaha_r9.rcz) is the
vehicle profile with every channel above except Digital 3, exported from the app. Engine
speed in it is `* 100 / 256`, so it shows whole rpm. To import it, open the file on the
phone and choose RaceChrono, or use *Import* in the menu on the app's main screen; neither
has been tried yet. An import adds a profile and does not overwrite one of the same name.

To check an export against the DBC:

```bash
analysis/.venv/bin/python analysis/tools/7-check-racechrono-export.py bluecan/racechrono/vehicle_2026_yamaha_r9.rcz analysis/r9.dbc analysis/captures
```

## Load on the link

Recording, the app asks Bluecan for the six ids it needs, every 50 ms; Bluecan's BUS page
then reads `flt 6`. That is about 91 frames a second over Bluetooth, of the 506 on the bus ([numbers.md](history/numbers.md)).
