# Hardware

What Bluecan is made of, how it is wired, and how it connects to the R9. The reasoning
behind each choice, and the bench rig as it was first built, are in
[history/hardware-2026-10-13.md](history/hardware-2026-10-13.md).

## Parts

| Part | Notes |
|---|---|
| [Adafruit ESP32-S3 Reverse TFT Feather with w.FL Antenna](https://www.adafruit.com/product/6303), #6303 | ESP32-S3 with 4 MB flash and 2 MB PSRAM, a 240×135 colour display, three buttons (D0, D1, D2), an RGB LED (NeoPixel), a MAX17048 battery gauge, a LiPo charger, a USB-C port and a w.FL antenna connector. Sold without an antenna |
| 2.4 GHz antenna with a w.FL connector | for example [Adafruit #5445](https://www.adafruit.com/product/5445). w.FL is the same connector as MHF3 and IPEX3, and smaller than u.FL (MHF1, IPEX1): a u.FL antenna does not fit |
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
| 2 | `D2` | button: freeze the screen; held 5 s, the Wi-Fi mode. Reads high when pressed |
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
  60 Ω; this one has only the first. It works without the second: captures with the
  CANable's terminator off saw no bus error. But Bluecan acknowledges frames, which makes
  it the bus's other node and its other end, and with its terminator on the bus is
  terminated as the standard has it. Captures with the CANable's terminator on saw no bus
  error either.
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
| 3 | Ground | KAmod `GND` only when the board is not powered from the bike — see [Isolation and ground](#isolation-and-ground). On the AtomS3 Lite, the Unit Mini CAN's `GND` |
| 4 | Battery +12 V, unswitched | **nothing** on the Feather: insulate it, the Feather is powered from USB. On the AtomS3 Lite, the Unit Mini CAN's `HPWR+`, which powers everything ([below](#the-m5stack-atoms3-lite)) |
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
- **The same board with a PCB antenna** is [#5691](https://www.adafruit.com/product/5691).
  It needs no antenna and has no connector for one.
- **Other boards the firmware builds for**: Adafruit Feather ESP32-S3 (RX GPIO18, TX
  GPIO17), Adafruit HUZZAH32 and ESP32 Dev Module (RX GPIO26, TX GPIO25), and the M5Stack
  AtomS3 Lite, below. On those there is no display, no status LED and no battery
  reading; only the Reverse TFT Feather has been run.
- **The transceiver must work at 3.3 V logic.** The SN65HVD230 does. On the KAmod its
  slope-control pin is tied on the board; on a bare SN65HVD230 module, check that `Rs` is
  tied low, or the transceiver sits in standby.

### The M5Stack AtomS3 Lite

A smaller board the firmware builds for, not yet bought or run. It is the `atom` profile in
[build.md](build.md#profiles).

| | Feather #6303 | [AtomS3 Lite](https://docs.m5stack.com/en/core/AtomS3%20Lite) |
|---|---|---|
| Chip | ESP32-S3, 4 MB flash, 2 MB PSRAM | ESP32-S3FN8, 8 MB flash, no PSRAM |
| Display, buttons | 240×135, three buttons | none; one button, G41 |
| RGB LED | NeoPixel, with a power switch | WS2812C on G35, always powered |
| Battery | charger and MAX17048 gauge | none |
| Antenna | w.FL, sold without one | built in |
| USB | the chip's own USB serial port or TinyUSB, and a UF2 bootloader; Bluecan uses the former, as on the Lite | the chip's own USB serial port, `303a:1001`; flashed with esptool only |
| Size | 51 × 23 mm | 24 × 24 × 9.5 mm |

The firmware does not use PSRAM, so its absence costs nothing. What the Lite loses is the
display, the buttons and the battery reading. Its RGB LED carries the status light, as the
NeoPixel does on the Feather.

The parts it is meant to be used with:

- **CAN and power**: an M5Stack [Unit Mini CAN](https://docs.m5stack.com/en/unit/Unit-Mini%20CAN)
  on the Grove port. It is the reason for this variant: it takes the bike's 12 V from the
  diagnostic connector and powers the Atom over the Grove cable, so nothing but the
  pigtail goes to the bike and no USB is needed. **TX G2** (yellow), **RX G1** (white), as in M5Stack's
  `MiniCAN_TJA1051T` example. Its transceiver, a TJA1051T/3, is not isolated, like the
  KAmod; it runs from the Grove 5 V with 3.3 V logic. It carries a **fixed 120 Ω
  terminator**, R1 across `CAN H` and `CAN L` with no jumper
  ([schematic V1.0](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/631/SCH_UNIT_Mini_CAN_V1.0.pdf)),
  which is what the R9 needs: see [Termination](#termination). Its HT3.96 terminal takes
  9–24 V on `HPWR+` into a converter of its own, whose 5 V output reaches the Grove
  connector through a diode and powers the Atom.

  | Diagnostic connector | Unit Mini CAN, HT3.96 |
  |---|---|
  | 2, CAN High | 4, `CAN_H` |
  | 5, CAN Low | 3, `CAN_L` |
  | 4, battery +12 V | 2, `HPWR+` |
  | 3, ground | 1, `GND` |

  Pin 4 is unswitched, so this variant is powered whenever it is plugged in, key off
  included. Its firmware sleeps a minute after the bus goes quiet, in light sleep, and
  wakes on the first frame or a press of the button ([usage.md](usage.md#standby),
  `CONFIG_STANDBY`). Asleep, the chip draws a quarter of a milliampere; the Unit Mini CAN
  does not sleep (its transceiver's `S` pin
  is tied to ground, and its power LED is always lit), nor do the two boards'
  converters or the Atom's RGB LED. None of it has been measured yet; what the
  datasheets put it at is under [Current while asleep](#current-while-asleep).
- **A microSD log**, optional: the [Atomic TF-Card Base](https://docs.m5stack.com/en/atom/Atomic_TFCard_Base)
  under the Atom, on SPI: **SCK G7, MISO G8, MOSI G6**, no chip select, as in M5Stack's
  Arduino example for it. FAT32, up to 16 GB as M5Stack say; a card over 32 GB comes
  formatted exFAT, which this firmware cannot mount. The base is passive: the socket, a
  100 nF capacitor and 4.7 kΩ resistors pulling MISO, CLK and MOSI up and CS down
  ([schematic](https://docs.m5stack.com/en/atom/Atomic_TFCard_Base)). The firmware looks
  for the card at each start and logs to it if it finds one, and tells the base without
  a card from no base by that pull-up on MISO; a Lite without the base runs the same
  build ([usage.md](usage.md#the-microsd-log)). The Atomic CAN Base would put CAN on the
  same header instead, RX G6 and TX G5.


#### Current while asleep

Estimated on 2026-10-19 from the datasheets and the schematics, not measured: the
[Unit Mini CAN's](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/631/SCH_UNIT_Mini_CAN_V1.0.pdf)
and the [AtomS3's](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/471/Sch_M5_AtomS3_v1.0.pdf),
which is the one M5Stack link from the Lite's page.

| Part | Rail | Current | From |
|---|---|---|---|
| TJA1051T/3 transceiver, normal mode, bus recessive | 5 V | 5 mA typical, 2.5–10 mA | [NXP datasheet](https://mouser.com/datasheet/2/302/TJA1051-119511.pdf) |
| ME3116 converter, 12 V to 5 V, on, no load | 12 V | 1.35 mA typical, 1.85 mA maximum | [Microne datasheet V03](https://radiodetali.com/media/files/146913/pdf-146913.pdf), at 12 V in |
| ME3116 feedback divider, R3 + R4 = 62.3 kΩ | 5 V | 0.08 mA | schematic |
| Unit Mini CAN's white power LED, through the MST5333 regulator and 1 kΩ | 5 V | about 0.5 mA | 3.3 V less a forward voltage of about 2.8 V, over 1 kΩ |
| WS2812C-2020, dark | 3.3 V | 0.6 mA | a distributor's listing ([JLCPCB](https://jlcpcb.com/partdetail/Worldsemi-WS2812C/C114587)); Worldsemi's datasheet gives none |
| SY8089 converter, 5 V to 3.3 V | 5 V | 0.055 mA | a distributor's listing ([LCSC](https://www.lcsc.com/product-detail/C78988.html)) |
| SY8089 feedback divider, R11 + R12 = 122.1 kΩ | 3.3 V | 0.027 mA | schematic |
| ESP32-S3 in deep sleep, RTC peripherals on for the wake on CAN RX | 3.3 V | 0.008 mA | Espressif's datasheet |
| ESP32-S3 in light sleep | 3.3 V | 0.24 mA | Espressif's datasheet |

Both converters are taken at 80 % efficiency; the ME3116's datasheet plots it only from
0.1 A up. The current from the bike is then
`(I5 + 3.3 × I3.3 / 0.8 / 5) × 5 / 0.8 / 12 + I(ME3116)`, with `I5` and `I3.3` the
sums of each rail's rows.

| Asleep | From 12 V | 4 Ah lasts |
|---|---|---|
| deep sleep, typical | 4.6 mA | 36.6 days |
| light sleep, typical | 4.7 mA | 35.8 days |
| deep sleep, worst case (transceiver and ME3116 at their maximum) | 7.7 mA | 21.8 days |
| light sleep, worst case | 7.8 mA | 21.5 days |
| the Unit Mini CAN alone, the Atom drawing nothing, worst case | 7.4 mA | 22.6 days |

The R9's battery is 9 Ah. At most 4 Ah of it is counted as Bluecan's to use, and the
worst case is the one to plan on.

The Unit Mini CAN is 7.4 mA of the worst case's 7.7 mA, and the depth of the chip's sleep
moves the total by 0.1 mA. Its largest term is the transceiver, held in normal mode by its
`S` pin; in silent mode the TJA1051 draws 0.1–2.5 mA, 1 mA typical, and still receives.

Not counted: the R9's own draw with the key off, a microSD card, the AtomS3's reset
circuit (a GS321 comparator), and the 0.3 V across the Unit Mini CAN's output diode, D4.
