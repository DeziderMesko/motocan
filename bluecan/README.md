# Bluecan

A Bluetooth LE CAN-bus reader for the Yamaha R9 (2026). It plugs into the bike's
diagnostic connector, reads the CAN bus, and streams the frames to
[RaceChrono Pro](https://racechrono.com/) on a phone, where they become channels in a
recorded lap session. Its own screen shows the main values decoded, and the state of the
bus and the Bluetooth link.

It is an Adafruit ESP32-S3 Feather, a 3.3 V CAN transceiver and a diagnostic pigtail. The
signals it decodes were worked out in the rest of this repository, motocan, from captures
of the R9's bus; Bluecan is the logger built on them. This folder has its documentation,
its firmware in [firmware/bluecan/](firmware/bluecan/), three bench sketches and the
tools.

**Status, 2026-10-14.** It works on the bench: against a replay of the R9's own captured
traffic, and with RaceChrono Pro on Android recording a session from it. **It has not yet
been connected to an R9.** The first time it is, follow [at-the-bike.md](at-the-bike.md).

## What it does

- **Reads a 500 kbps CAN bus**, standard 11-bit ids. Frames with 29-bit ids and remote
  frames are counted and discarded.
- **Streams raw frames to RaceChrono** over Bluetooth LE, using RaceChrono's DIY device
  protocol. The firmware does not decide what a frame means: the app does, from channels
  you enter in it. The R9's channels are in [racechrono-channels.md](racechrono-channels.md).
- **Sends only what the app asks for.** While recording, RaceChrono asks for the ids it has
  channels for, each at an interval (50 ms by default), and only those leave the board.
  While you are setting channels up, it asks for everything.
- **Acknowledges frames but never sends one.** The R9's diagnostic connector has only the
  engine control unit on it, and it repeats one frame endlessly unless someone
  acknowledges. Bluecan starts each boot listening only, begins acknowledging once it has
  received clean frames at the right bit rate, and goes back to listening only if the bus
  looks wrong. It never transmits a frame, a diagnostic request or a query.
- **Shows the bike's values on its screen**: gear, engine speed, both wheel speeds,
  throttle grip, throttle valve, water and air temperature, and the TCS and PWR settings,
  decoded on the board itself whether or not a phone is connected. Three more pages show the bus, the ids
  on it and the last frames received.
- **Shows the state of the link on an RGB LED** that can be read at a glance: booting,
  waiting for a phone, configuring, recording, frames stopped, something lost.

## What it does not do

- **It does not record anything.** RaceChrono on the phone records; the board keeps
  nothing.
- **It has no GPS.** RaceChrono needs its own position source, the phone's or an external
  receiver.
- **It decodes only what has been worked out.** Brake pressure and lean angle are not
  among the signals found on this connector so far, nor is the ride mode itself, only the
  settings it makes. What is known is in the
  [root README](../README.md#what-is-decoded-so-far).
- **It is not isolated.** Its ground is the bike's ground. See
  [hardware.md](hardware.md#isolation-and-ground).
- **It has been used with an Android phone only.** An iPhone needs one setting changed
  before it will connect well; see [build.md](build.md#build-time-settings).

## What you need

| | |
|---|---|
| Board | [Adafruit ESP32-S3 Reverse TFT Feather](https://www.adafruit.com/product/5691) (#5691) |
| CAN transceiver | [KAmod CAN SN65HVD230](https://kamami.pl/en/CAN-converters/1200043-kamod-can-sn65hvd230-can-interface-module-with-the-sn65hvd230-chip-5902186316799.html), or another 3.3 V SN65HVD230 board |
| Bike connection | a pigtail for the R9's red 6-pin diagnostic connector (Yamaha YDT coupler, ISO 19689) |
| Power | the R9's USB outlet and a USB-C **data** cable. Optionally a 500 mAh LiPo cell |
| Phone | Android, with **RaceChrono Pro**. The free version does not support DIY devices |
| To build it | a computer with `arduino-cli` and the ESP32 Arduino core 3.3.11, about 8 GB of disk |
| To test it without the bike | optional: a USB-CAN adapter such as a CANable 2.0 Pro, and Linux with `can-utils` |

The parts, the wiring and the power are in [hardware.md](hardware.md).

## Getting it running

1. **Wire it**: the transceiver to the Feather, the pigtail to the transceiver, and the
   transceiver's terminating resistor **on** — [hardware.md](hardware.md#termination).
2. **Build and flash the firmware** — [build.md](build.md).
3. **Add it to RaceChrono and enter the channels** — [usage.md](usage.md#connecting-racechrono)
   and [racechrono-channels.md](racechrono-channels.md).
4. **At the bike**, with the bike standing still, go through
   [at-the-bike.md](at-the-bike.md) before riding with it.

## Documents

| | |
|---|---|
| [hardware.md](hardware.md) | Parts, pins, wiring, the R9's connector, termination, power, ground |
| [build.md](build.md) | Toolchain, build settings, flashing on Linux, WSL2 and by UF2, the serial console |
| [usage.md](usage.md) | What it does when it starts, the screen page by page, the buttons, the LED, RaceChrono, limits, troubleshooting |
| [racechrono-channels.md](racechrono-channels.md) | The R9's channels as RaceChrono accepts them: PID, equation, which channel to map onto |
| [at-the-bike.md](at-the-bike.md) | The checklist for the first time it is plugged into the R9 |
| [bench.md](bench.md) | Testing without the bike: a USB-CAN adapter, generated and replayed traffic, the tools |
| [firmware/bluecan/](firmware/bluecan/) | The firmware, and a README on how its code is laid out |
| [firmware/](firmware/) | Bench sketches and the tools that [bench.md](bench.md) describes |
| [history/](history/) | How it got here: every firmware patch and the measurements behind it. Not needed to build or use it |

## Before connecting it to a vehicle

Connecting anything to a vehicle's CAN bus is at your own risk. This device does more than
listen: it acknowledges frames on the bike's bus, and its ground is the bike's ground. If
it misbehaves on the bus, it can disturb the engine control unit's traffic on that pair.
It is built to stop acknowledging at the first sign of trouble, and that has been tested on
the bench, not on a bike.

Do not capture or set it up while riding. Everything here was done at a standstill.

## Credits and licence

The firmware is built on Joe Roback's
[joeroback/racechrono-canbus](https://github.com/joeroback/racechrono-canbus), a RaceChrono
CAN reader for BMW cars, and keeps its structure. It is under the MIT licence, with his
copyright and this project's: [firmware/bluecan/LICENSE](firmware/bluecan/LICENSE). The
Bluetooth protocol is RaceChrono's, as specified in
[aollin/racechrono-ble-diy-device](https://github.com/aollin/racechrono-ble-diy-device).
