# Building and flashing

How to get the firmware onto the board. Everything here was done with the versions in the
table below; other versions may work and have not been tried.

## Toolchain

| | Version used | |
|---|---|---|
| [arduino-cli](https://arduino.github.io/arduino-cli/) | 1.5.2 | the Arduino IDE 2 works too, with the same board settings |
| ESP32 Arduino core (`esp32:esp32`) | **3.3.11** | about 7.3 GB once installed. The firmware is written for core 3.x and uses its NimBLE Bluetooth stack |
| Adafruit GFX Library | 1.12.6 | display |
| Adafruit ST7735 and ST7789 Library | 1.11.0 | display; pulls in Adafruit BusIO |
| Python 3 | any | only for the UF2 conversion and the tools in [firmware/tools/](firmware/tools/) |

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

```bash
arduino-cli core update-index && arduino-cli core install esp32:esp32@3.3.11
```

```bash
arduino-cli lib install "Adafruit GFX Library@1.12.6" "Adafruit ST7735 and ST7789 Library@1.11.0"
```

On Linux, add yourself to the `dialout` group so the serial port can be opened without
root, then log in again:

```bash
sudo usermod -aG dialout $USER
```

## Getting the source

The firmware is in [firmware/bluecan/](firmware/bluecan/) in this repository; its own
[README](firmware/bluecan/README.md) describes the code.

```bash
git clone https://github.com/DeziderMesko/motocan.git
```

## Profiles

Bluecan is built for one of two profiles. Each is a board, and with it what the board
has: one file per profile under
[firmware/bluecan/src/boards/](firmware/bluecan/src/boards/) gives the pins and the parts,
and the build options are in
[firmware/tools/reflash-bluecan.sh](firmware/tools/reflash-bluecan.sh) and nowhere else.

| Profile | Board | What it has |
|---|---|---|
| `feather` (the default) | Adafruit Feather ESP32-S3 Reverse TFT | display, buttons, status light, battery gauge |
| `atom` | [M5Stack AtomS3 Lite](hardware.md#the-m5stack-atoms3-lite) on a Unit Mini CAN | the status light on its RGB LED, and nothing else of those; sleeps while the bus is silent ([standby](#build-time-settings)). Run on the bench from USB ([history](history/firmware.md#the-atoms3-lite-on-the-bench--run-and-it-works)) |

### Build options

| Option | `feather` | `atom` | Why |
|---|---|---|---|
| FQBN | `esp32:esp32:adafruit_feather_esp32s3_reversetft` | `esp32:esp32:m5stack_atoms3` | |
| `PartitionScheme` | `tinyuf2_noota` | `default_8MB` | the Feather's keeps its UF2 bootloader, with 2816 KB for the application. **Not `huge_app`**, which overwrites the bootloader. The Lite has 8 MB and no UF2 bootloader to keep |
| `LoopCore`, `EventsCore` | `1`, `1` | `1`, `1` | the firmware asserts that `setup()` runs on core 1; core 0 is for Bluetooth and the display |
| `CDCOnBoot` | `cdc` | `cdc` | the serial console is on the USB port |
| `USBMode` | `hwcdc` | `hwcdc` | the chip's own USB serial port, the only one the Lite has |
| `PSRAM` | `disabled` | `disabled` | the Lite has none |
| `FlashSize` | `4M` | `8M` | |

The Feather has 2 MB of PSRAM and could use TinyUSB, and is built without either on
purpose: so that what is measured on it is what the Lite will run, the same heap and the
same USB stack. Nothing in the firmware depends on the choice. Setting `PSRAM=enabled` and
leaving `USBMode` out, which is TinyUSB, gives the Feather's own back with no other
change; the board then appears as `239a:8123` instead of `303a:1001`, and the tools find
it either way.

| Build, release | Program storage | Static RAM | Free heap after start-up |
|---|---|---|---|
| `feather` | 1342 KB | 100.2 KB | not measured with the Wi-Fi mode built in |
| `atom` | 1306 KB | 94.1 KB | 166 660 bytes in the normal mode with no card, measured on the Lite on 2026-10-23 in a debug build with no standby, no phone connected, before patch 50 added ESP-IDF's card driver; 114 KB with the stand-in for a card (`--sd-emulate`), measured on the Feather with the Lite's parts |

The microSD log is in every build, and costs about 104 KB of the program and 1 KB of the
static RAM. Its ring and its task's stack, 38 KB, are taken from the heap only when a
card is found at start-up, and the stand-in holds what a mounted card would besides.

The Wi-Fi mode is about 570 KB of the program and 21 KB of the static RAM, which the
Wi-Fi driver sets aside whether the mode runs or not: built without it
(`CONFIG_NO_WIFI`), `atom` is 729 KB and 72.8 KB.

The clock is about 19 KB of the program, the C library's time zones and time
formatting, in every build.

## Building

From `bluecan/firmware/`:

```bash
./tools/reflash-bluecan.sh feather --no-upload
```

It compiles the profile into `build/bluecan-feather/`, and for the Feather
writes `bluecan.uf2` beside the binaries. Without `--no-upload` it also uploads, under
WSL2 as [below](#under-wsl2). The same with `arduino-cli` alone, from the repository root:

```bash
arduino-cli compile --fqbn "esp32:esp32:adafruit_feather_esp32s3_reversetft:PartitionScheme=tinyuf2_noota,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M" --build-path build/bluecan-feather bluecan/firmware/bluecan
```

`atom` takes its own FQBN and options from the table. In the Arduino IDE, open
`bluecan/firmware/bluecan/bluecan.ino` and set the Tools menu as in the table.

The first build on a computer also writes
`firmware/bluecan/src/web/secret.hpp`, the Wi-Fi mode's password, and prints it. Git
ignores that file; a plain `arduino-cli compile` without it stops with a message saying
so.

`./build.sh` is for the bench sketches and refuses Bluecan.

A clean build takes about half a minute.

### A debug build

A release build writes only a few lines to the serial console: start-up, warnings and
errors. A debug build adds a block of statistics every 5 seconds — frame counts, queue,
Bluetooth, bus state, the LED's state in words, display cost, internal heap, and the
microSD log when there is a card — and a line for each change of bus state. Add this to the compile command:

```bash
--build-property "compiler.cpp.extra_flags=-DDEBUG"
```

`tools/reflash-bluecan.sh <profile> --debug` builds and uploads one in a step. Its other
flags are for the bench, and combine with it and with each other:

| Flag | Sets | |
|---|---|---|
| `--sd-emulate` | `CONFIG_SDLOG_EMULATE` | stands in for a card when none is found at start-up |
| `--recording` | `CONFIG_RC_BENCH_RECORDING` | the filter RaceChrono sends when it records |
| `--no-standby` | `CONFIG_NO_STANDBY` | keeps `atom` awake on a silent bus |
| `--quick` | `CONFIG_WIFI_IDLE_S=30`, `CONFIG_STANDBY_TIMEOUT_S=15` | short timeouts, to try them out without waiting: the Wi-Fi mode gives way after 30 s without a request, standby sleeps after 15 s of silence |
| `--no-upload` | | compile only |

A debug build waits 5 seconds at start-up before doing anything, so that a console can be
attached in time. Use a release build on the bike.

## Build-time settings

In [firmware/bluecan/src/racechrono-canbus.hpp](firmware/bluecan/src/racechrono-canbus.hpp), and what a board has in its
file under [firmware/bluecan/src/boards/](firmware/bluecan/src/boards/). The defaults are
what the R9 and an Android phone need. A combination that cannot work, such as standby
with the display, is refused at compile time by
[boards/check.hpp](firmware/bluecan/src/boards/check.hpp).

| Setting | Default | What it does |
|---|---|---|
| `CONFIG_CANBUS_DECODER_YAMAHAR9` | on | the R9 decoder: forwards frames by the app's filter, and decodes the values for the LIVE page |
| `CONFIG_CANBUS_DECODER_BMWG8X` | off | upstream's BMW decoder. Define one decoder, not both |
| `CONFIG_CANBUS_ACK` | on | lets the controller acknowledge once the bit rate is proved. Undefined, the board only ever listens, which on the R9's diagnostic connector gets almost nothing |
| `CONFIG_BLE_CONN_ITVL_MIN` / `_MAX` | `6` / `10` (7.5–12.5 ms) | the Bluetooth connection interval asked of the phone, in 1.25 ms units. **For an iPhone, change these to `12` / `24`** (15–30 ms): iOS rejects a request below 15 ms or with less than 15 ms between minimum and maximum, and then uses an interval of its own choosing. `12` / `24` works on Android too |
| `CONFIG_CELL_FITTED` | off, in `boards/feather_revtft.hpp` | show the battery's state of charge and whether it is charging. Define it only with a cell plugged in; with none, the gauge reports a full, discharging battery that is not there. Untested with a cell |
| `CONFIG_SDLOG` | on, in both board files, with the card's pins | log every standard data frame to a microSD card on SPI, as on M5Stack's Atomic TF-Card Base: 24 bytes a frame, written in 4096-byte sectors by a task on core 1, into a new `/bluecan-NNNN.bin` on every start. The card keeps the whole bus whatever the app's filter. The card is looked for once at start-up, on the log's own task, without holding up Bluetooth or CAN; with none, the task ends and nothing is logged until the next start. The look tells nothing on the pins, an empty socket and a card that does not mount apart ([usage.md](usage.md#the-microsd-log)). A card that fails a write is mounted again every 5 s (`CONFIG_SDLOG_RETRY_S`) into a new file. Before each new file the oldest logs are deleted to keep `CONFIG_SDLOG_KEEP_FREE_PCT` of the card free. The card goes through ESP-IDF's `sdspi` driver on SPI3 with DMA, not Arduino's SD library, whose writes failed beside Bluetooth ([history](history/firmware.md#patch-50--the-card-through-esp-idfs-sdspi-driver--applied-run-on-the-lite)). Run on the Lite with a card, not yet on the Feather |
| `CONFIG_SDLOG_KEEP_FREE_PCT` | 5 | the room kept free on the card, in percent of its size, by deleting the oldest `bluecan-NNNN.bin` before a new one is opened. A share, so that a 1 or 2 GB card still keeps most of itself for logs: 5% is about 18 hours of the R9's bus on a 16 GB card, about an hour on a 1 GB one |
| `CONFIG_SDLOG_EMULATE` | off | with no card at start-up, drives the SPI bus and holds the heap as a card would, so that its cost can be measured on a board without one. `--sd-emulate` sets it |
| `CONFIG_SDLOG_BUFFER` | 32 768 bytes | the RAM between the CAN interrupt and the card, about 2.7 s of the R9's bus |
| `CONFIG_SDLOG_SPI_HZ` | 25 MHz | the card's SPI clock, as M5Stack's example for the base has it |
| `CONFIG_SDLOG_CORE` | 1 | the core the card's task runs on. The SD library waits for the card by polling, so this is the core a slow card costs |
| `CONFIG_CLOCK_TZ` | Prague's, `CET-1CEST,M3.5.0,M10.5.0/3` | the time zone, in POSIX form, of the card's file times and of the stamp in a downloaded file's name ([usage.md](usage.md#the-clock)) |
| `CONFIG_CLOCK_KEEP_S` | 600 s | the Wi-Fi page sets the board's clock only if it was set longer ago than this, so that a page opened twice keeps the first setting and the drift it measured |
| `CONFIG_STANDBY` | on for `atom`, off for `feather` | light sleep once no frame has arrived for `CONFIG_STANDBY_TIMEOUT_S`; the first frame on CAN RX, or the board file's button (`BUTTON_PIN`), wakes the board, which then starts from the top. For a board powered from the diagnostic connector's unswitched 12 V. Not with the display. `--no-standby` (`CONFIG_NO_STANDBY`) turns it off |
| `CONFIG_STANDBY_TIMEOUT_S` | 60 s | the silence before it sleeps. The R9 sends over 500 frames a second with the key on, so a second would do; the rest is time to use the board after waking it with the button, and on the bench |
| `CONFIG_WIFI` | on | the Wi-Fi mode: an access point and a web page with the bus's statistics and the card's files, run in place of Bluetooth when the board's button is held ([usage.md](usage.md#the-wi-fi-mode)). Needs `BUTTON_PIN` in the board file. It costs the normal mode about 21 KB of RAM, which the Wi-Fi driver sets aside whether it runs or not; `CONFIG_NO_WIFI` leaves it out |
| `CONFIG_WIFI_PASSWORD` | random, in `src/web/secret.hpp` | the access point's WPA2 password. `reflash-bluecan.sh` writes the file with a random twelve characters the first time it builds, and prints them; git ignores the file, so the password stays on the computer that built the board. Edit the file to change it, eight characters or more |
| `CONFIG_BUTTON_HOLD_S` | 5 s | how long the button is held to switch between the normal and the Wi-Fi mode |
| `CONFIG_WIFI_IDLE_S` | 300 s | the time without a request to the page after which the Wi-Fi mode gives way to the normal one. An open page asks every second |
| `CONFIG_WIFI_MOVING_KMH_X100`, `CONFIG_WIFI_MOVING_MS` | 1000 (10 km/h), 1000 ms | the rear wheel speed, held that long, that ends the Wi-Fi mode: the bike is being ridden |
| `CONFIG_WIFI_CHANNEL` | 1 | the access point's channel |
| `CONFIG_RC_BENCH_RECORDING` | off | bench only: whatever RaceChrono asks for, apply the filter it sends when it records the R9 profile, the six ids with channels at 50 ms. A phone left in the channel editor then stands in for a recording session. `--recording` sets it |
| `DEBUG` | off | see [A debug build](#a-debug-build) |
| `RC_WRAPPER_NOTIFY` | off | sends frames through the slower Arduino BLE wrapper. Only for comparing the two paths |
| `CONFIG_RC_STATS_TIMEOUT` | 5 000 000 µs | how often a debug build prints its statistics |

## Flashing

The ESP32-S3's USB is part of the chip, and a Bluecan build runs on its own USB serial
port: the board appears as `303a:1001` while it runs and in download mode alike. The
Feather's UF2 bootloader is `239a:0123`, and the bench sketches, built with TinyUSB, run
as `239a:8123`.

### Over USB, on Linux

```bash
arduino-cli upload --fqbn "esp32:esp32:adafruit_feather_esp32s3_reversetft:PartitionScheme=tinyuf2_noota,PSRAM=disabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M" --input-dir build/bluecan-feather --port /dev/ttyACM0 bluecan/firmware/bluecan
```

The board resets into download mode by itself. If the upload cannot find it, put it there
by hand — on the Feather hold **D0**, press and release **RESET**, release D0 — and run the
upload again.

### By UF2, with no tools at all

The Feather's bootloader can appear as a USB drive. `reflash-bluecan.sh` has already made
the `.uf2` for the Feather profiles; from a plain `arduino-cli` build, convert it:

```bash
python3 bluecan/firmware/tools/uf2conv.py build/bluecan-feather/bluecan.ino.bin --base 0x10000 --family 0xc47e5767 --convert --output bluecan.uf2
```

Then **double-tap RESET**. A drive named `FTHRS3BOOT` appears; copy `bluecan.uf2` onto it,
and the board flashes itself and restarts. This works from any operating system, and is
the way back if a serial upload goes wrong. It depends on the `tinyuf2_noota` partition
scheme. If the bootloader has been overwritten anyway, Adafruit's
[factory reset](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/factory-reset)
restores it through the chip's ROM bootloader, which cannot be erased.

### Under WSL2

WSL2 has no USB of its own. [usbipd-win](https://github.com/dorssel/usbipd-win) passes USB
devices through from Windows.

1. Install it once, from an administrator PowerShell:
   `winget install --exact dorssel.usbipd-win`. In WSL, install the client side and put
   `usbip` on the path:

   ```bash
   sudo apt install -y linux-tools-virtual hwdata
   ```

   ```bash
   sudo update-alternatives --install /usr/local/bin/usbip usbip "$(ls /usr/lib/linux-tools/*/usbip | tail -1)" 20
   ```

   The WSL kernel needs `CONFIG_USB_ACM` for the serial port to appear. The 6.18 kernel
   used here has it; some older WSL2 kernels were built without it.
2. **Bind the board's identities**, from an administrator PowerShell. `usbipd list` shows
   the bus id; `usbipd bind --busid <BUSID>` binds it. Bind it while it runs Bluecan or
   sits in download mode (`303a:1001`), and, for the bench sketches, once more while it
   runs one of them (`239a:8123`). Bindings survive reboots.
3. Attach it after every Windows restart, and after every flash:
   [firmware/tools/usb-attach.sh](firmware/tools/usb-attach.sh). It needs no administrator.

[firmware/tools/reflash-bluecan.sh](firmware/tools/reflash-bluecan.sh) does the build and
the upload together and re-attaches the board around its resets; an attempt that fails
while the board is being attached again is retried.

```bash
bluecan/firmware/tools/reflash-bluecan.sh feather
```

```bash
bluecan/firmware/tools/reflash-bluecan.sh atom --debug
```

The UF2 drive is visible from Windows; copy the `.uf2` there from Explorer if WSL does not
see it under `/mnt`.

## The serial console

115 200 baud on `/dev/ttyACM0` (a COM port on Windows), the chip's own USB serial port.

- **Read it from Linux, not Windows**:
  [firmware/tools/console-log.sh](firmware/tools/console-log.sh) keeps it in
  `build/console.log` across resets, sleeps and wakes, and
  [firmware/tools/bench.py](firmware/tools/bench.py) reads it for a while:
  `bluecan/firmware/tools/bench.py 10` for 10 seconds. A plain `cat /dev/ttyACM0` works
  too. None of them resets the board, and the port can be held open indefinitely.
  Opened from Windows, it reset the board every time on 2026-10-19 (`rst:0x15`),
  whatever DTR and RTS were set to.
- **A restart shows.** The chip's ROM prints its boot lines on this port, with the reason
  for the reset (`rst:0x15 (USB_UART_CHIP_RESET)`, for one). Whether a crash report gets
  there too has not been tried; on the Feather, a board that restarts also shows its
  uptime on the BUS page going back to zero.
- A computer that is plugged in and not reading holds a write for at most about 20 ms; see
  `setup()` in [bluecan.ino](firmware/bluecan/bluecan.ino). With nothing plugged in, as on
  the bike, a write returns at once.

## Checking the decoder

The values on the LIVE page come from `src/canbus/r9.hpp`, written by hand from the DBC
file in this repository, [analysis/r9.dbc](../analysis/r9.dbc). After changing either,
run [firmware/tools/r9-decode-check.py](firmware/tools/r9-decode-check.py): it compiles the
header on the computer, decodes every frame in the captures in
[analysis/captures/](../analysis/captures/) with both, and reports any disagreement. It
needs `g++` and the analysis Python environment, which `analysis/tools/0-setup.sh` creates:

```bash
cd bluecan/firmware && ../../analysis/.venv/bin/python tools/r9-decode-check.py
```
