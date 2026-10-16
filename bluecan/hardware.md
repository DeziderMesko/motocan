# Hardware

What Bluecan is made of, how it is wired, and how it connects to the R9. The reasoning
behind each choice, and the bench rig as it was first built, are in
[history/hardware-2026-10-13.md](history/hardware-2026-10-13.md).

## Parts

| Part | Notes |
|---|---|
| [Adafruit ESP32-S3 Reverse TFT Feather](https://www.adafruit.com/product/5691), #5691 | ESP32-S3 with 4 MB flash and 2 MB PSRAM, a 240×135 colour display, three buttons (D0, D1, D2), an RGB LED (NeoPixel), a MAX17048 battery gauge, a LiPo charger and a USB-C port |
| [KAmod CAN SN65HVD230](https://kamami.pl/en/CAN-converters/1200043-kamod-can-sn65hvd230-can-interface-module-with-the-sn65hvd230-chip-5902186316799.html) | 3.3 V CAN transceiver. Goldpin header for the Feather, screw terminal and goldpins for `CAN H`/`CAN L`, power LED, and a `120R` jumper that switches a 120 Ω terminator in and out |
| Pigtail for the R9's diagnostic connector | the male half of the red 6-pin connector, with leads. See [The R9's diagnostic connector](#the-r9s-diagnostic-connector) |
| USB-C cable | must carry data if you will flash or read the console through it. A charge-only cable powers the board and nothing else, and looks like a dead board |
| LiPo cell, optional | 2-pin JST PH 2.0 mm, 500 mAh. See [Power](#power) |

The ESP32-S3 is what makes a bare transceiver enough: it has a CAN controller built in
(Espressif calls it TWAI) as well as Bluetooth LE. The SN65HVD230 is only the physical
layer. See [Substituting parts](#substituting-parts) before using a different board.

## Pins

| GPIO | Feather label | Used for |
|---|---|---|
| 18 | `A0` | **CAN RX**, from the transceiver |
| 17 | `A1` | **CAN TX**, to the transceiver. Held recessive (high) until the firmware starts acknowledging |
| 0 | `D0` | button: display and LED on / LED only / both off. Reads low when pressed |
| 1 | `D1` | button: next page. Reads high when pressed |
| 2 | `D2` | button: freeze the screen. Reads high when pressed |
| 3, 4 | `SDA`, `SCL` | I²C to the MAX17048 battery gauge at `0x36`, and the STEMMA QT connector |
| 21, 33 | — | NeoPixel power and data |
| 7 | — | power switch for the display and the I²C devices |
| 40, 41, 42, 45 | — | the display |
| 19, 20 | — | USB. Never use them |

`D0` is also the ESP32-S3's BOOT pin, which is why it has the opposite polarity from the
other two buttons. Holding it while pressing RESET puts the board into download mode.

## Wiring

The KAmod's 4-pin header to the Feather:

| KAmod | Feather |
|---|---|
| `3.3V` | `3V` |
| `GND` | `GND` |
| `CAN RX` | `A0` |
| `CAN TX` | `A1` |

The KAmod's labels are from the microcontroller's side, so RX goes to RX and TX to TX;
nothing crosses. `CAN H` and `CAN L` are on the green screw terminal and, duplicated, on
goldpins.

The transceiver runs from the Feather's own 3.3 V rail. Do not feed any external supply
into the Feather's `3V` pin.

### Termination

A CAN bus has a 120 Ω resistor at each of its two ends. Whether Bluecan should add one
depends on where it is plugged in.

- **On the R9, the `120R` jumper is on.** The diagnostic pair has the engine control unit
  and nothing else on it, and with the key off and nothing plugged in it reads about
  120 Ω: one terminator, on the bike's side. A CAN bus has one at each end and reads
  60 Ω; this one has only the first. **It works without the second**: the three captures
  of 2026-10-11 ran with the CANable's terminator off and saw no bus error. But Bluecan
  acknowledges frames, which makes it the bus's other node and its other end, and with
  its terminator on the bus is terminated as the standard has it, so on is the better
  setting. The CANable ran both captures of 2026-10-16 that way, also without an error.
  Until 2026-10-16 this page said off, reasoning from a bus terminated at both ends
  already; this one is not.
- **On a two-node bench**, it is on as well: Bluecan is then one end of the bus and the
  other adapter the other end. See [bench.md](bench.md).

To check: with the KAmod connected to nothing, a meter across `CAN H` and `CAN L` reads
open with the jumper off and 120 Ω with it on. Plugged into the bike with the key off, the
pair should read about 60 Ω, as a CAN bus should. About 120 Ω means the jumper is off: the
bus still works that way, but it is terminated at one end only.

## The R9's diagnostic connector

The red 6-pin connector under the seat, which the service manual calls the YDT coupler. It
is the ISO 19689 connector used across Euro 5 motorcycles. Pinout, measured on an R9 on
2026-10-10:

| Pin | Function | Connect to |
|---|---|---|
| 2 | CAN High | KAmod `CAN H` |
| 3 | Ground | KAmod `GND` only when the board is not powered from the bike — see [Isolation and ground](#isolation-and-ground) |
| 4 | Battery +12 V, unswitched | **nothing.** Insulate it. Bluecan is powered from USB |
| 5 | CAN Low | KAmod `CAN L` |
| 6 | K-line | nothing |

Pins 1 to 3 are the row with the latch, pins 4 to 6 the other row, so CAN High and CAN Low
are **the middle cavity of each row**. The middle cavities are in the same place on both
halves of the connector, which makes them the safe ones to find by eye; the outer cavities
are mirrored between the bike's half and the pigtail's. Do not go by wire colour: on the
pigtail used here the latch row is all green and the other row brown.

The bus runs at 500 kbps with standard 11-bit ids. It carries only the engine control
unit's diagnostic traffic: 23 ids at 506 frames a second, when something acknowledges
them. How the pins were found and verified is in
[analysis/connector.md](../analysis/connector.md); the bike's wider CAN wiring is in
[analysis/R9_CANbus.md](../analysis/R9_CANbus.md).

## Power

**On the bike**: from the R9's USB outlet, which is rated 3 A, through the Feather's USB-C
port. No 12 V converter is needed. Whether the outlet is live with the key off has not been
checked; if it is not, Bluecan starts when the key is turned on.

**On the bench**: USB from the computer, which also gives the serial console and flashing.

**A LiPo cell is optional.** It keeps the board running when the outlet cuts out, for
instance with the ignition, and the board charges it from USB while it runs.

- Connector: 2-pin JST PH 2.0 mm. **Check the polarity before plugging it in.** The
  Feather expects red = +, black = −, and cells sold with this connector are often wired
  the other way. Reversed, it destroys the charger.
- Size: 500 mAh, about 30 × 35 × 5 mm, fits under the board. The board charges at 196 mA
  (an MCP73831 with a 5.1 kΩ programming resistor), so 500 mAh keeps that below 0.4 C and
  fills from empty in about 2.5 hours. Up to about 2000 mAh fits physically.
- How long it runs the board has **not been measured**. At an assumed 100–150 mA, 500 mAh
  is 3–4 hours.

**What the board can tell you about its supply**: the BUS page shows the voltage of the
battery rail, from the MAX17048 gauge. With no cell fitted that rail is the charger's
output, about 4.1 V. The 5 V coming in over USB is not measurable by anything on the board.
The gauge cannot tell whether a cell is present, so the state of charge is shown only in a
build with `CONFIG_CELL_FITTED` defined ([build.md](build.md#build-time-settings)), and
that build has not been tested with a cell.

## Isolation and ground

**Bluecan has no galvanic isolation.** The transceiver's ground is the Feather's ground.

- **Powered from the bike's USB outlet**, the board shares the bike's ground through the
  outlet. Leave pin 3 unconnected.
- **Powered from a power bank or its own cell only**, it shares nothing with the bike, and
  the transceiver has no ground reference but the bus itself. Connect pin 3 to the KAmod's
  `GND`.

Neither arrangement has yet been tried on the bike. A laptop and an isolated adapter such
as the CANable 2.0 Pro are the safer way to look at an unknown bus.

## Substituting parts

- **The board must have both a CAN controller and Bluetooth LE.** The ESP32 and ESP32-S3
  have both. The ESP32-S2 has no Bluetooth; nRF52 boards have no CAN controller and would
  need an MCP2515, which this firmware does not support.
- **Other boards the firmware builds for**: Adafruit Feather ESP32-S3 (RX GPIO18, TX
  GPIO17), Adafruit HUZZAH32 and ESP32 Dev Module (RX GPIO26, TX GPIO25). On those there
  is no display, no status LED and no battery reading; only the Reverse TFT Feather has
  been run.
- **The transceiver must work at 3.3 V logic.** The SN65HVD230 does. On the KAmod its
  slope-control pin is tied on the board; on a bare SN65HVD230 module, check that `Rs` is
  tied low, or the transceiver sits in standby.
