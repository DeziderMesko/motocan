# Bluecan firmware

The firmware that runs on the Bluecan board. How to build and flash it is in
[../../build.md](../../build.md); what it does, in [../../usage.md](../../usage.md). This
page is for working on the code.

## Layout

| | |
|---|---|
| `bluecan.ino` | `setup()` on core 1: the display, the Bluetooth task, then the CAN controller. The Bluetooth task's loop, which drains the frame queue to the phone |
| `src/racechrono-canbus.hpp` | build-time settings and board pin definitions — see [build.md](../../build.md#build-time-settings) |
| `src/canbus/controller.*` | the CAN controller, driven at register level: the receive interrupt, the frame queue, listen-only and acknowledging, the supervisor that trips it back to listen-only |
| `src/canbus/decoder.*` | the base class for RaceChrono's filter requests |
| `src/canbus/decoder_yamahar9.cpp` | the R9 decoder: the run-time allow-list the app fills, and the latest payload of the seven ids the LIVE page decodes |
| `src/canbus/r9.hpp` | the R9 signal equations for the LIVE page, written from [analysis/r9.dbc](../../../analysis/r9.dbc). Check them with [r9-decode-check.py](../tools/r9-decode-check.py) after changing either |
| `src/canbus/frame.hpp` | one CAN frame, laid out so the notification to the phone is a pointer into it |
| `src/racechrono/device.*` | the Bluetooth LE server: RaceChrono's service, the connection interval, the counters |
| `src/display/display.*` | the four pages, the buttons and the status LED, on their own task |
| `src/led/`, `src/power/`, `src/logging/`, `src/utils/` | the LEDs, the MAX17048 battery gauge, the serial logger, a timer |

## How it runs

```
      core 1                                  core 0
 ┌───────────────────────┐            ┌────────────────────────────┐
 │ CAN receive interrupt │            │ Bluetooth task (IDLE+1)    │
 │  · count, classify    │  frame     │  · waits on the queue      │
 │  · drop 29-bit / RTR  │  queue     │  · notifies the phone      │
 │  · allow-list gate ───┼──2000 ────▶│  · yields 1 tick / 128     │
 │  · enqueue or drop    │  frames    │                            │
 ├───────────────────────┤            │ display task (IDLE+1)      │
 │ loop(): supervisor    │            │  · pages, buttons, LED     │
 │  every 10 ms          │            │ NimBLE host and controller │
 └───────────────────────┘            └────────────────────────────┘
```

**All CAN work is on core 1 and all Bluetooth and display work on core 0.** The display
must never be drawn from the CAN side: a full repaint takes about 250 ms, and on core 1
that lost 1200 of 2000 frames on the bench. The display redraws only the characters that
changed, which keeps an idle pass under half a millisecond.

The frame path sends each notification straight to the NimBLE host with
`ble_gatts_notify_custom()`, without the Arduino `BLECharacteristic::notify()` wrapper,
which costs three heap allocations and a lock per frame. Every counter is cumulative and
read by two consumers, the display and the debug statistics, which derive rates by
difference.

## The RaceChrono protocol

As specified by [aollin/racechrono-ble-diy-device](https://github.com/aollin/racechrono-ble-diy-device):

| | |
|---|---|
| Service | `0x1FF8` |
| Characteristic `0x0001`, notify | a CAN frame: bytes 0–3 the 11-bit id as a 32-bit **little-endian** integer, then up to 8 payload bytes |
| Characteristic `0x0002`, write | the app's filter: `0` deny all; `1` + 2-byte interval in ms: allow all; `2` + 2-byte interval + 4-byte id: allow one id. These are **big-endian** |

The board provides only the CAN-Bus characteristics, not GPS (`0x0003`/`0x0004`) or
Monitor (`0x0005`/`0x0006`).

## Origin and licence

This firmware started as [joeroback/racechrono-canbus](https://github.com/joeroback/racechrono-canbus),
a RaceChrono CAN reader for BMW G8x cars, taken at its last commit of 2023-10-01. Its
structure is still there: the register-level receive interrupt, the decoder per vehicle,
the two-core split. The R9 decoder, the display, acknowledging, the allow-list, the
battery gauge and most of the CAN and Bluetooth paths were written here. The BMW
decoder, upstream's documentation and its log-parsing tool are removed.

MIT licence, Joe Roback's copyright and this project's — see [LICENSE](LICENSE). The
history of every change since the fork is in
[bluecan/history/firmware.md](../../history/firmware.md), and the commits themselves are
in this repository's history.
