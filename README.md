# motocan

Reverse-engineering what a **Yamaha R9 (2026)** says on its CAN bus, so the bike's own
data — engine speed, gear, throttle, temperatures, wheel speeds — can be logged while
riding without Yamaha's Y-TRAC app or an AiM logger.

Nothing here comes from Yamaha. Every row below was measured on one bike with a USB CAN
adapter on the diagnostic connector, a laptop, and something independent to hold each
number against: the dash, a recording of the engine's sound, a second sensor.

**Status, 2026-10-17:** twenty fields decoded from six captures taken at a standstill,
the two wheel speeds among them, now to 59 km/h and against the dash. The device that would stream all this to a phone works on
the bench, acknowledging as this connector needs, and has not yet been plugged into it.

## What is decoded so far

The R9's diagnostic connector carries 23 ids at 506 frames a second, at 500 kbps — and
only when the listening node acknowledges them. Bytes are numbered from 0; `b3` is byte 3.

| Signal | Frame | Rate | Raw value | Equation | How sure |
|---|---|---|---|---|---|
| Engine speed | `0x27C` | 100 Hz | `b0 << 8 \| b1` (16 bits, big-endian) | `rpm = raw * 100 / 256` | `b0` is whole hundreds of rpm, sent again on their own in `0x750`; agrees with the engine's sound at idle (0.3915 ± 1.5 %) and holds to 3 400 rpm |
| Gear | `0x27C` | 100 Hz | `b4 & 0x07` | `0` = N, `1`–`6`, `7` = between gears | confirmed, 13 anchors of 13 |
| Gear, as the ECU has it | `0x27C` | 100 Hz | `(b3 >> 5) & 0x07` | `1`–`3` seen; `0` in neutral and with the engine off | first, second and third with the engine running. It holds the last gear between gears, and after the engine stops in gear, until key-off |
| TCS setting | `0x27C` | 100 Hz | `(b3 >> 1) & 0x0F` | the number on the dash | confirmed, 10 anchors of 10, and the five modes' values in the dash's menu. The bits above it are the ECU's gear, which is why the mask |
| A bit that is set for about half a second as the starter turns | `0x27C` | 100 Hz | `b3 & 0x01` | `1` during the start and in the last frames at key-off, `0` at all other times | all six starts captured, and not set when the button was pressed with the start blocked. So it follows the starter, not the button; what it is beyond that is not known |
| A bit that is set at the first start | `0x27C` | 100 Hz | `b6 >> 7` | `0` from key-on to the first start, `1` after it until key-off; `0` for a third of a second at each restart | four starts, and three stops that did not clear it. *Until 2026-10-16 this row read "set while the engine runs"; the stops say otherwise* |
| PWR setting | `0x27C` and `0x76E` | 100 Hz / 2 Hz | `b4 >> 6`, and `0x76E` `b1` | `PWR = raw + 1` | 5 dash photos of 5, and the five modes' values in the dash's menu |
| Neutral, at rest | `0x751` | 100 Hz | `b6` | `0x60` or `0x70` = neutral, `0x00` = in gear | agrees with every change into and out of neutral with the engine off. It reads `0x60` in first gear with the engine running, so for the gear, use `0x27C` |
| Rear wheel speed | `0x756` | 20 Hz | `(b3 >> 4) << 12 \| (b1 & 0x0F) << 8 \| (b0 & 0xF0) \| b2 >> 4` (16 bits) | `km/h = raw / 100` | 697 counts per revolution a second by the wheel's own pulses, in three gears; as 0.01 km/h that is a 1.936 m wheel, which the tyre is. The dash showed 28 km/h at 25.2, a speedometer's usual 11 % high, one photo. Seen to 59 km/h; bit 12 seen set, bits 13–15 not |
| Rear wheel pulses | `0x27C` | 100 Hz | `b2` | pulses in each 10 ms frame, 46 to a revolution | one a hole: 46 holes counted on the sensor ring, and 11 and 10 turns by hand counted 507 and 462 |
| Front wheel speed | `0x765` | 10 Hz | `(b0 >> 4) << 12 \| (b2 >> 4) << 8 \| (b6 >> 4) << 4 \| (b3 & 0x0F)` (16 bits) | `km/h = raw / 100` | follows the front turned by hand. On the stand, with the rear turning alone, it takes the rear's value after some seconds instead ([open question 8](analysis/open-questions.md)), and that copy is what showed the top nibble, to 59 km/h; bit 12 seen set, bits 13–15 not |
| Brake, front or rear | `0x383` | 10 Hz | `(b4 >> 3) & 0x01` | `1` while either brake is on | 4 pulls of 4, and twice as the wheel was stopped |
| Cruise RES+ and SET− | `0x76D` | 2 Hz | `b3 & 0x01`, and `(b5 >> 4) & 0x01` | `1` while the button is held | 2 presses of each. At 2 Hz a short press can fall between frames |
| Track display | `0x76E` | 2 Hz | `b2` | `0x10` with the Track display, `0x20` with the normal one | one switch each way |
| Water temperature | `0x512` | 1 Hz | `b0` | `°C = raw * 0.625 - 30` | confirmed, 6 dash anchors from 17 to 76 °C |
| Intake air temperature | `0x512` | 1 Hz | `b1` | `°C = raw * 0.625 - 30` | held against the water sensor after a cold soak; the dash shows 1–2 °C less |
| Throttle grip (APS) | `0x758` | 20 Hz | `(b7 & 0x0F) << 12 \| (b6 & 0x0F) << 8 \| b5` (15 bits) | `% = raw / 31672 * 100` | both ends measured; reads 0 for the first twentieth of the travel |
| Throttle valve (TPS), measured | `0x758` | 20 Hz | `(b0 >> 4) << 8 \| b2` (12 bits) | `% = raw / 16 / 144.4 * 100` | top measured, zero provisional: a warm idle reads about 8 % |
| Throttle valve, commanded | `0x759` | 20 Hz | `(b0 >> 4) << 4 \| (b2 >> 4)` (8 bits) | `% = raw / 144.4 * 100` | the same unit as the measured one, and leads it by a sample |

The same as a DBC file: [analysis/r9.dbc](analysis/r9.dbc). The same as equations
RaceChrono accepts, with the ids in decimal as it wants them:
[bluecan/racechrono-channels.md](bluecan/racechrono-channels.md). Every id, including about
forty fields that change and have no name yet:
[analysis/r9-signals.md](analysis/r9-signals.md). What those unnamed fields could be, going
by the sensors, monitor items and control states the service manual says the ECU has:
[analysis/R9_CAN_unknowns_vs_manual.md](analysis/R9_CAN_unknowns_vs_manual.md) —
candidates, graded by how well they fit, not findings.

## What is not there, and what is left

- **Not on this connector:** lean angle, the SCS, LIF, quickshifter and launch-control settings, battery
  voltage, fuel used. The R9's ECU has three separate CAN pairs and the diagnostic
  connector is on the one with nothing but the ECU on it
  ([analysis/R9_CANbus.md](analysis/R9_CANbus.md)).
- **Next:** why the front's field takes the rear's on the stand; first gear, which turns
  2.2 % faster against the wheel than the owner's manual's ratio says, held against the
  manual's second and third; and two lamp bits, BC and the engine fault, each seen once.
  The stand session that found the wheels is
  [analysis/stand-session.md](analysis/stand-session.md).
- **Open:** the throttle valve's zero, the unnamed fields, anything that needs the bike
  moving. The whole list, with what would settle each item:
  [analysis/open-questions.md](analysis/open-questions.md).

## Where things are

Two halves that meet at the CAN bus and nowhere else.

| | |
|---|---|
| [analysis/](analysis/) | **What the bytes mean.** A laptop, a CANable adapter and a log file |
| &nbsp;&nbsp;[r9-signals.md](analysis/r9-signals.md), [r9.dbc](analysis/r9.dbc) | the result: what every id carries |
| &nbsp;&nbsp;[open-questions.md](analysis/open-questions.md) | what is left |
| &nbsp;&nbsp;[connector.md](analysis/connector.md) | where to connect, and the pinout |
| &nbsp;&nbsp;[captures/](analysis/captures/) | the raw logs, and a write-up beside each with the evidence |
| &nbsp;&nbsp;[tools/](analysis/tools/) | the capture session as one script, and the converters |
| &nbsp;&nbsp;[R9_CANbus.md](analysis/R9_CANbus.md) | the bike's CAN wiring, from the service manual |
| &nbsp;&nbsp;[background/](analysis/background/) | the research from before the bike was reached: other Yamahas, AiM's protocols |
| [bluecan/](bluecan/) | **The logger built on them**: an ESP32-S3 Feather feeding [RaceChrono](https://racechrono.com/) over Bluetooth LE, with its firmware, documentation and tools. Proven on the bench, including the acknowledging this connector needs; it has not been on the R9 yet |
| [journal/](journal/) | the author's working notes. Set aside, and may be removed |
| [AGENTS.md](AGENTS.md) | the conventions, for a coding agent or a person about to change something |

## Starting from here

To **use the result**, [analysis/r9.dbc](analysis/r9.dbc) and the table above are all of
it. To **check it or carry it further** without a bike:

```bash
cd analysis && ./tools/0-setup.sh && ./tools/1-selftest.sh   # Linux or WSL2; builds .venv, proves the chain on a virtual bus
xz -dk captures/session-2026-10-11_200550/capture.log.xz      # a candump log, 465 000 frames, engine running
```

Each capture folder has the log, `ref.csv` with the marks made during the session (what
the rider did and what the dash showed, with timestamps), and a write-up of what was
found. To **capture on your own bike**, [analysis/README.md](analysis/README.md) has the
order of the scripts and [analysis/connector.md](analysis/connector.md) the connection.

## Before you connect anything to a motorcycle

This is one person's measurements on one bike, with no warranty of any kind. The adapter
here never sends a frame of its own, but it does acknowledge the ECU's, which makes it a
participant on the bus and not a bystander; `tools/2-session.sh` proves the bit rate in
listen-only first and runs a watchdog that takes the adapter off the bus if it ever
transmits. Nothing in this repository was captured while riding. If you do otherwise, that
is your decision and your risk.

## Thanks

None of this started from nothing. In the order they helped:

- The RaceChrono forum thread ["PIDs for yamaha"](https://racechrono.com/forum/d/2296-2296/3),
  and the r1-forum.com thread ["CAN Bus Protocol"](https://www.r1-forum.com/threads/can-bus-protocol.512553/)
  — the first evidence of what Yamaha frames look like, worked out by hand by people who
  then wrote it down.
- [terrafirma2021/Yamaha-DataLogger](https://github.com/terrafirma2021/Yamaha-DataLogger),
  [JackHat1/MT07_CAN_Project](https://github.com/JackHat1/MT07_CAN_Project) and
  [DKey96/T7-ABS-Dongle](https://github.com/DKey96/T7-ABS-Dongle) — working code for the
  R9's relatives.
- [AiM](https://www.aim-sportline.com/) — its published user guides for the MT-09 platform
  gave the connector, the pin functions and the channel list this project aims at, and its
  engine-speed scale of 0.391 turned out to be the R9's 100/256. Copies of two guides are
  in [analysis/sources/aim-yamaha/](analysis/sources/aim-yamaha/README.md); they are AiM's.
- [billyjack2/CAN-DBC-Collection](https://github.com/billyjack2/CAN-DBC-Collection) — AiM's
  protocols as DBC files, four of which are copied beside the guides.
- [KDS](https://kds-online.de/en/obd-adapter-euro5-for-motorcycle-6-pin-to-obd2-k-line-can-bus.html)
  — the one picture found anywhere that numbers the cavities of the Euro5 connector.
- [CSS Electronics](https://www.csselectronics.com/pages/can-bus-reverse-engineering-ai-llm-claude)
  — the analysis scripts vendored in [analysis/css-skill/](analysis/css-skill/) (MIT), and
  a method written up as engineering. Jordans Garage's
  ["How to decode CANbus with AI"](https://www.youtube.com/watch?v=GlQO8fLgHi4) for
  showing where that method fails.
- [joeroback/racechrono-canbus](https://github.com/joeroback/racechrono-canbus), which
  Bluecan is built on, [aollin/racechrono-ble-diy-device](https://github.com/aollin/racechrono-ble-diy-device)
  for the specification of RaceChrono's Bluetooth service, and
  [timurrrr/RaceChronoDiyBleDevice](https://github.com/timurrrr/RaceChronoDiyBleDevice).
- [Elmue's Candlelight firmware](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight) for the CANable, which fixed a stall the stock firmware had
  ([bluecan/history/gs-usb-stall.md](bluecan/history/gs-usb-stall.md)).

Most of the analysis and the writing was done in conversation with Claude, Anthropic's
model, working from the owner's captures and the owner's decisions.

If something here is yours and you would rather it were not copied, open an issue and it
will be removed.

## Licence

Code is under the [MIT licence](LICENSE). The text, the captures and `r9.dbc` are under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). `analysis/css-skill/` and the
files in `analysis/sources/` belong to their authors and keep their own terms.
