# Bluecan firmware

The firmware that runs on the Bluecan board. How to build and flash it is in
[../../build.md](../../build.md); what it does, in [../../usage.md](../../usage.md). This
page is for working on the code.

## Layout

| | |
|---|---|
| `bluecan.ino` | `setup()` on core 1: the display, the Bluetooth task or the Wi-Fi mode, then the CAN controller. The Bluetooth task's loop, which drains the frame queue to the phone. `loop()`: the bus supervisor, the button, and standby or the Wi-Fi mode's exits |
| `src/racechrono-canbus.hpp` | build-time settings — see [build.md](../../build.md#build-time-settings) |
| `src/boards/` | one file per board profile: its pins and what it has, picked by `board.hpp`; `check.hpp` refuses combinations that cannot work — see [build.md](../../build.md#profiles) |
| `src/canbus/controller.*` | the CAN controller, driven at register level: the receive interrupt, the frame queue, listen-only and acknowledging, the supervisor that trips it back to listen-only and proves the bus again |
| `src/canbus/decoder.*` | the base class for RaceChrono's filter requests |
| `src/canbus/decoder_yamahar9.cpp` | the R9 decoder: the run-time allow-list the app fills, and the latest payload of the seven ids the LIVE page decodes |
| `src/canbus/r9.hpp` | the R9 signal equations for the LIVE page, written from [analysis/r9.dbc](../../../analysis/r9.dbc). Check them with [r9-decode-check.py](../tools/r9-decode-check.py) after changing either |
| `src/canbus/frame.hpp` | one CAN frame, laid out so the notification to the phone is a pointer into it |
| `src/racechrono/device.*` | the Bluetooth LE server: RaceChrono's service, the connection interval, the counters |
| `src/display/display.*` | the four pages and the buttons, on their own task |
| `src/status/status_light.*` | the status light, on its own task, on any board with a pixel (`CONFIG_STATUS_LED`): what colour, from the counters, every 25 ms |
| `src/standby/standby.*` | light sleep while the bus is silent, and the wake on CAN RX or the button, built in with `CONFIG_STANDBY` |
| `src/mode/mode.*` | which mode this start runs, normal or Wi-Fi, and the restart that switches; built in with `CONFIG_WIFI` |
| `src/button/button.*` | the board's button for the switch: held for `CONFIG_BUTTON_HOLD_S` and let go |
| `src/web/web.*`, `src/web/page.hpp` | the Wi-Fi mode: the access point, the web server and its page, the census of ids that takes the frame queue in place of Bluetooth, and the mode's exits. The password is `src/web/secret.hpp`, which git ignores |
| `src/sdlog/sdlog.*` | the microSD log, built in with `CONFIG_SDLOG`: a ring the CAN interrupt fills, and a task on core 1 that writes it out. Also its stand-in for a board with no card |
| `src/led/`, `src/power/`, `src/logging/`, `src/utils/` | the LED drivers, on the pins the board file gives; the MAX17048 battery gauge; the serial logger; a timer |

## How it runs

```
      core 1                                  core 0
 ┌───────────────────────┐            ┌────────────────────────────┐
 │ CAN receive interrupt │            │ Bluetooth task (IDLE+1)    │
 │  · count, classify    │  frame     │  · waits on the queue      │
 │  · drop 29-bit / RTR  │  queue     │  · notifies the phone      │
 │  · microSD ring       │            │                            │
 │  · allow-list gate ───┼──2000 ────▶│  · yields 1 tick / 128     │
 │  · enqueue or drop    │  frames    │                            │
 ├───────────────────────┤            │ display task (IDLE+1)      │
 │ loop(): supervisor    │            │  · pages, buttons          │
 │  every 10 ms          │            │ status light task (IDLE+1) │
 ├───────────────────────┤            │  · the LED, every 25 ms    │
 │ microSD task (IDLE+1) │            │ NimBLE host and controller │
 │  · 4096-byte sectors  │            │                            │
 └───────────────────────┘            └────────────────────────────┘
```

The display task exists only on the Feather, the status light's on a board with a pixel,
and the microSD ring and task only in a build with `CONFIG_SDLOG`. The ring is filled
before the allow-list gate, so the card keeps every frame whatever the app asked for.

In the Wi-Fi mode the right-hand side is different: no Bluetooth task and no NimBLE, but
the Wi-Fi driver, a web server task, and a census task that drains the frame queue
instead, counting each id for the page. Neither card task nor ring runs; the server
reads the card itself. The CAN side on core 1 is the same in both modes. Everything the
Wi-Fi mode needs is allocated when it starts, so that the normal mode does not carry
it, except what the Wi-Fi driver sets aside statically, about 21 KB.

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
