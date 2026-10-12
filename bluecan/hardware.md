# Hardware

## Parts

| Part | Status | Notes |
|---|---|---|
| [Adafruit ESP32-S3 Reverse TFT Feather](https://www.adafruit.com/product/5691) | working | 4 MB flash / 2 MB PSRAM, 240×135 ST7789, 3 buttons |
| [KAmod CAN SN65HVD230](https://kamami.pl/en/CAN-converters/1200043-kamod-can-sn65hvd230-can-interface-module-with-the-sn65hvd230-chip-5902186316799.html) | working | Kamami's 3.3 V SN65HVD230 breakout: goldpin + Phoenix MC 3.81 screw terminal, power LED, **switchable** on-board 120 Ω terminator. Proven on a real two-node bus, not just self-reception — [bench-link.md](bench-link.md) |
| MKS CANable 2.0 Pro | working | Makerbase build of the isolated USB-CAN adapter, STM32G431, CAN-FD capable — the "fake bike" and reference sniffer. Runs **ElmüSoft Candlelight 2.5** ([Elmue/CANable-2.5-firmware-Slcan-and-Candlelight](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight), `STM32G431 - Candlelight2.5 - Multiboard.dfu`), flashed 2026-08-21 in place of the shipped build, which stalled after three frames — where it came from, how it was flashed and the diagnosis are in [gs-usb-stall.md](gs-usb-stall.md). See also [Firmware modes](#firmware-modes) below |
| Diagnostic pigtail | in hand since 2026-09-21 | pins metered and marked — see the connector section of [connector.md](../analysis/connector.md). It was "the only missing part" when the rest of this page was written |

## Why this board

The SN65HVD230 is only a **transceiver** — the physical layer. It converts logic-level
TX/RX into the differential CAN-H/CAN-L pair. It contains no protocol controller, so
the MCU must provide one. That single fact rules out most of the Feather line:

| Board | CAN controller | BLE | Verdict |
|---|---|---|---|
| Feather ESP32-S3 (incl. TFT / Reverse TFT) | ✅ TWAI (SJA1000-like) | ✅ | works |
| Feather ESP32-S2 TFT | ✅ TWAI | ❌ **none** | dead end — RaceChrono's DIY protocol is BLE-only |
| Feather nRF52840 | ❌ | ✅ | would need an MCP2515 (SPI controller+transceiver) instead |

The ESP32-S3 is also dual-core, which matters for the firmware: the upstream project
pins all BLE work to core 0 and all CAN work to core 1.

## Pin map

Board pins already spoken for, from the Arduino core's
[variant file](https://github.com/espressif/arduino-esp32/blob/master/variants/adafruit_feather_esp32s3_reversetft/pins_arduino.h):

| GPIO | Used by | Free? |
|---|---|---|
| 0, 1, 2 | buttons D0 / D1 / D2 (see below) | ❌ |
| 3, 4 | I2C SDA / SCL (+ STEMMA QT, and the MAX17048 fuel gauge at 0x36) | ❌ |
| 7 | `TFT_I2C_POWER` | ❌ |
| 13 | red LED (`LED_BUILTIN`) | driven, barely: on once the CAN controller starts and never again. The instrument that replaced it is the NeoPixel — [patch 16](firmware.md#patch-16--the-neopixel-as-the-riders-status-light--applied) |
| **17 (A1)** | — | ✅ **CAN TX** |
| **18 (A0)** | — | ✅ **CAN RX** |
| 19, 20 | USB D− / D+ | ❌ never touch |
| 21 | NeoPixel power (`NEOPIXEL_POWER`, active HIGH) | ❌ — the status light's off switch |
| 33 | NeoPixel data (`PIN_NEOPIXEL`) | ❌ — the status light, driven by `src/display` |
| 35, 36, 37 | SPI MOSI / SCK / MISO (header) | free, unused here |
| 38, 39 | UART RX / TX (header) | free |
| 40, 41, 42, 45 | TFT DC / RST / CS / backlight | ❌ |

The three buttons do not share a polarity, which is worth knowing before writing any
UI against them: **D0 idles HIGH and reads LOW when pressed** (it doubles as the BOOT
strapping pin, so it has to be pulled up), while **D1 and D2 idle LOW and read HIGH
when pressed**. So D0 wants `INPUT_PULLUP` and D1/D2 want `INPUT_PULLDOWN`. All three
pulls are internal to the ESP32-S3; the board adds no external resistors.

GPIO 17 and 18 are plain GPIOs on the ESP32-S3 — not strapping pins, not shared with
the display or USB. Convenient bonus: **A0 = GPIO18 and A1 = GPIO17 on all three
ESP32-S3 Feathers** (plain, TFT, Reverse TFT), so upstream pin definitions written for
the plain S3 Feather are numerically correct here; only the board `#define` differs.

## Wiring

The KAmod's 4-pin goldpin header, read off the silkscreen, top to bottom:

| KAmod pin | Feather | Note |
|---|---|---|
| `3.3V` | `3V` | |
| `GND` | `GND` | |
| `CAN RX` ◄ | `A0` / GPIO18 | module output → MCU input |
| `CAN TX` ► | `A1` / GPIO17 | module input ← MCU output |

The labels are from the MCU's point of view and the silkscreen arrows confirm the
direction, so the connection is one-to-one by name — RX to RX, TX to TX, nothing
crosses. The bus side is `CAN L` / `CAN H` on the green Phoenix screw terminal, with the
same pair also on goldpins.

Once wired, the bring-up sketch's `A0-A1 continuity` check **keeps passing** — worth
knowing before it gets misread as a wiring fault. The SN65HVD230 is non-inverting end to
end (`D` low → dominant → `R` low, `D` high → recessive → `R` high), so GPIO18 reads
exactly what a bare jumper would. A pass confirms the module is powered, out of standby,
and that both directions land on the right pins; it cannot tell a transceiver from a
plain short between the two pins, and it exercises nothing on the `CAN H`/`CAN L` side.

**`Rs` is not exposed on this module**, so the usual SN65HVD230 trap — a floating
slope-control pin leaving the transceiver in standby — does not apply here. Kamami tied
it down on the board. There is a `PWR` LED to confirm the 3.3 V rail.

**120 Ω termination is situational**, and on the KAmod it is the `120R` jumper rather
than a soldered resistor, so it is a 2-second change either way.

- On the **bench rig**, you want it — a CAN bus needs 120 Ω at each of its two ends,
  so two nodes each carrying one resistor is exactly right.
- On the **bike**, you must remove or un-jumper it. The R9's bus is already terminated
  at both ends; adding a third resistor drops the effective termination and degrades
  signalling for every ECU on the network, not just for the reader.

## Power

- **Bench:** USB from the laptop. Also gives serial console and reflashing.
- **Bike:** the R9 has a **USB outlet rated 3 A**, so the Feather is fed the way it is on
  the bench and no 12 V → 5 V step-down is needed. That part was on this list until
  2026-08-22 and is not being sourced. Do not feed any external supply into `3V`.
- **Battery:** the LiPo/Li-ion connector remains an option and is worth having as one —
  it rides through the outlet cutting out with the ignition, and it decouples the device
  from whatever else gets plugged into that outlet. Display plus BLE plus CAN is not a
  light load, so size it against a session rather than against idle.
- **Which cell, and why those numbers:** a **2-pin JST PH 2.0 mm** flat LiPo, and **the
  one chosen is 500 mAh** — the floor rather than the comfortable 1200–2000 mAh band,
  because it has to be small enough to sit under the board, and for a cell whose job is
  riding through the ignition rather than being the supply, the small one is not short.
  That floor is the charger's: the board charges at **196 mA**, and 500 mAh keeps that
  under 0.4 C. The figure is off the schematic, not off the product page, which never
  states it — the charger is an **MCP73831T-2ACI/OT** and its program resistor R4, PROG
  to GND, is 5.1 kΩ, so I_REG = 1000 V / R_PROG = 196 mA. (The board's own legend rounds
  it to 200 mA. Do not read the current off R2, which is the same value and is only the
  STAT LED's ballast.) The ceiling is only physical: 2000 mAh is about the largest flat
  cell that does not outgrow the board, where a 500 mAh one is about 30 × 35 × 5 mm.
  Filling the 500 mAh from empty at 196 mA takes ~2.5 h and a 2000 mAh one would take
  ~10 h, neither of which matters: the cell tops up across a ride rather than being
  charged for one.
- **How long 500 mAh lasts has not been measured.** At an assumed 100–150 mA for display
  plus BLE plus CAN that is 3–4 hours, far more than the ride-through role asks for —
  but nothing here has ever metered this board's draw, so treat it as arithmetic and not
  as a bench result. The trustworthy number comes from the MAX17048 once a cell is in
  the jack and the firmware is built with `CONFIG_CELL_FITTED`, which is the remaining
  half of [patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read).
- **Check the polarity before plugging anything in.** Adafruit wires that jack
  **red = +, black = −**, and cells sold with the same connector are frequently the
  other way round. Reversed, it takes the charger with it.
- **Reading it back:** the board's **MAX17048** fuel gauge (I2C, `0x36`) sits on the BAT
  rail, and the BUS page shows that rail's voltage — see
  [firmware.md, patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read).
  Two things about it are worth knowing before trusting a number off the glass. **VBUS
  itself is not readable** by any firmware on this board: the 5 V from the outlet goes to
  the `USB` pad and to no ADC, and reading it would mean soldering a divider from `USB`
  to one of the free analog pins (A2–A5). And the gauge **cannot tell whether a cell is
  fitted** — with the jack empty it reported 98.6% charged and discharging at 26.6%/hr
  for a rail sitting still at 4.09 V. So the state of charge and the charge/discharge
  marker are shown only when the firmware is built with `CONFIG_CELL_FITTED`; the
  voltage is shown always, because the voltage is the part that is true either way.

Note that unlike MCP2515-based builds (which need 5 V for the module), the SN65HVD230
runs at 3.3 V, so on the bench the Feather's own `3V` rail is sufficient for the
transceiver.

## Bench rig — the "fake bike"

Hardware logging on the actual R9 is blocked on the pigtail, but nothing about
building and validating this device is. The counterpart on the bench is a
**CANable v2.0 Pro**: an isolated USB-CAN adapter that plays the bike, so the whole
chain — transceiver → TWAI → BLE → RaceChrono → decoded channel — can be proven end
to end before the bike is ever involved.

Wire the two together with a twisted pair, CANH to CANH and CANL to CANL, 120 Ω at
each end — one from the CANable, one from the SN65HVD230 breakout. That is exactly
the two-resistor case the termination note above describes, so on the bench both
boards keep their resistors.

**This is now done and tested.** Frames cross in both directions, both nodes ACK each
other, and the Feather takes a saturated 500 kbps bus — 10000 of 10000 frames at a peak
of 4078/s — without dropping one. Results and method in [bench-link.md](bench-link.md).
Nothing about the pair, the termination or the bit timing needed adjusting: it worked on
the first attempt, and the only failure found in the whole session was in firmware.

### Why this beats a second microcontroller

The original plan here was a second ESP32 running a `FakeToyotaGR86`-style sketch.
The CANable removes the need to write, flash and debug that firmware at all, because
with **candleLight/gs_usb** firmware it presents as a native SocketCAN interface and
the whole of `can-utils` applies:

```bash
sudo ip link set can0 up type can bitrate 500000
cangen can0 -I 236 -L 8 -D r -g 10      # synthetic gear frames every 10 ms
cansend can0 236#2000000000000000        # a single frame: gear 1, per the mapping table
candump -l can0                          # log everything to a file
```

Sweeping a value through its range with `cansend` while watching RaceChrono is how you
prove that a channel definition in the app is correct. Doing that against synthetic
data separates "my equation is wrong" from "my CAN mapping is wrong" — two failures
that are almost impossible to tell apart once real bike data is involved.

The kernel side is confirmed present in this WSL install (`gs_usb`, `slcan`, `can-raw`,
`vcan` all available) and the adapter comes up as `can0` at 500 kbps. Transmission used
to stall after three frames over the usbip passthrough — that turned out to be a bug in
the adapter's shipped firmware, not in usbip, and a reflash fixed it. Full diagnosis in
[gs-usb-stall.md](gs-usb-stall.md).

### Firmware modes

The adapter can run in either of two modes, and both work here:

| Mode | Appears as | Needs |
|---|---|---|
| candleLight / gs_usb | native `can0` netdev | `gs_usb` (accepts VID:PID `1D50:606F`) |
| slcan | CDC-ACM serial + `slcand` | `cdc-acm` + `slcan` |

candleLight is the better mode — native netdev, no `slcand` daemon, higher throughput.

Which mode the board is actually in can be read straight off the USB descriptor, no
guessing: `usbipd list` on Windows showing `1d50:606f` means candleLight, while a plain
CDC serial device (a COM port) means slcan.

[Makerbase's own page](https://github.com/makerbase-mks/CANable-MKS) confirms the 2.0
uses an STM32G431C8T6 and supports both firmwares, but does not say which is
preloaded — hence checking rather than assuming. Note that STM32G431-based boards are
**not** supported by the upstream
[candleLight_fw](https://github.com/candle-usb/candleLight_fw); the working firmware for
them is
[Elmue's CANable 2.5 build](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight),
which covers both modes and CAN-FD.

**What this adapter runs now.** It shipped in candleLight mode carrying an old MKS build
of the canable2/budgetcan gs_usb firmware (`sw_version=2`, `1d50:606f`) — the build with
the three-frame bug. It has since been reflashed over DFU to **ElmüSoft Candlelight 2.5**
and stays in candleLight mode, same VID:PID.

One hardware quirk to know about: MKS shares `PB8` between CAN-RX and `nBOOT0`, with a
power-on delay that is too slow, so a brief 5 V dip — wiggling the USB plug is enough —
drops the adapter into the STM32 ROM bootloader. It then enumerates as `0483:df11` and
`can0` never appears. Nothing is damaged and a clean power cycle fixes it. The option-byte
fix was considered and deliberately declined; the reasoning is in
[gs-usb-stall.md](gs-usb-stall.md).

### A note on roles

The CANable is the better *sniffer* of the two devices: it is galvanically isolated,
it logs straight to a file through `candump`, and it needs no custom firmware. The
Feather is the better *vehicle device*: it is small, battery-capable, has a display,
and speaks RaceChrono's BLE protocol so no laptop has to ride along.

**That split is now the decision, not a guess**: CANable for the reverse-engineering
sessions that fill in [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md), Feather for riding with
RaceChrono and nothing else. The Feather's firmware is a telemetry device — see
[the bluecan README](README.md#sniffing-is-the-canables-job-not-this-one) for what
follows from that. Both still wait on the pigtail before either touches the bike.

*Since 2026-10-11 the CANable has been on the bike, three times.* What it found changes
one thing on this page: the R9's diagnostic pair has only the ECU on it, and a device that
does not acknowledge gets almost nothing from it. The Feather is wired and flashed as a
pure listener, so it needs its TX path and normal mode before it is any use there — see
[the bluecan README](README.md#what-the-r9-turned-out-to-send). The note below about
isolation matters more for that, not less: an acknowledging Feather drives the bus from a
board that shares the bike's ground.

Isolation is worth calling out: the CANable Pro isolates USB from CAN, so a laptop
plugged into the bike is protected. The Feather build has **no** isolation — its ground
is common with the bike's.
