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

## Board settings

Board: **Adafruit Feather ESP32-S3 Reverse TFT**, FQBN
`esp32:esp32:adafruit_feather_esp32s3_reversetft`, with these options:

| Option | Value | Arduino IDE, Tools menu | Why |
|---|---|---|---|
| `PartitionScheme` | `tinyuf2_noota` | Partition Scheme: TinyUF2 4MB No OTA | 2816 KB for the application, and it keeps the board's UF2 bootloader. **Not `huge_app`**, which overwrites the bootloader |
| `LoopCore` | `1` | Arduino Runs On: Core 1 | the firmware asserts that `setup()` runs on core 1 |
| `EventsCore` | `1` | Events Run On: Core 1 | keeps core 0 for Bluetooth and the display |
| `CDCOnBoot` | `cdc` | USB CDC On Boot: Enabled | the serial console is on the USB port |
| `PSRAM` | `enabled` | PSRAM: QSPI PSRAM | |
| `FlashSize` | `4M` | Flash Size: 4MB (32Mb) | |

## Building

From `bluecan/firmware/`:

```bash
./build.sh bluecan
```

It compiles with the settings above and writes `build/bluecan/bluecan.uf2` beside the
binaries; `./build.sh bluecan --upload` also uploads it over USB. The same with
`arduino-cli` alone, from the repository root:

```bash
arduino-cli compile --fqbn "esp32:esp32:adafruit_feather_esp32s3_reversetft:PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M" --build-path build/bluecan bluecan/firmware/bluecan
```

In the Arduino IDE, open `bluecan/firmware/bluecan/bluecan.ino` and set the Tools menu as
in the table above.

A clean build takes about half a minute and ends with about 710 KB of program storage
(24%) and 106 KB of static RAM (32%).

### A debug build

A release build writes only a few lines to the serial console: start-up, warnings and
errors. A debug build adds a block of statistics every 5 seconds — frame counts, queue,
Bluetooth, bus state, the LED's state in words, display cost, heap — and a line for each
change of bus state. Add this to the compile command:

```bash
--build-property "compiler.cpp.extra_flags=-DDEBUG"
```

Under WSL2, `tools/reflash-bluecan.sh --debug` builds and uploads one in a step.

A debug build waits 5 seconds at start-up before doing anything, so that a console can be
attached in time. Use a release build on the bike.

## Build-time settings

In [firmware/bluecan/src/racechrono-canbus.hpp](firmware/bluecan/src/racechrono-canbus.hpp). The defaults are what the R9 and an
Android phone need.

| Setting | Default | What it does |
|---|---|---|
| `CONFIG_CANBUS_DECODER_YAMAHAR9` | on | the R9 decoder: forwards frames by the app's filter, and decodes the values for the LIVE page |
| `CONFIG_CANBUS_DECODER_BMWG8X` | off | upstream's BMW decoder. Define one decoder, not both |
| `CONFIG_CANBUS_ACK` | on | lets the controller acknowledge once the bit rate is proved. Undefined, the board only ever listens, which on the R9's diagnostic connector gets almost nothing |
| `CONFIG_BLE_CONN_ITVL_MIN` / `_MAX` | `6` / `10` (7.5–12.5 ms) | the Bluetooth connection interval asked of the phone, in 1.25 ms units. **For an iPhone, change these to `12` / `24`** (15–30 ms): iOS rejects a request below 15 ms or with less than 15 ms between minimum and maximum, and then uses an interval of its own choosing. `12` / `24` works on Android too |
| `CONFIG_CELL_FITTED` | off | show the battery's state of charge and whether it is charging. Define it only with a cell plugged in; with none, the gauge reports a full, discharging battery that is not there. Untested with a cell |
| `DEBUG` | off | see [A debug build](#a-debug-build) |
| `RC_WRAPPER_NOTIFY` | off | sends frames through the slower Arduino BLE wrapper. Only for comparing the two paths |
| `CONFIG_RC_STATS_TIMEOUT` | 5 000 000 µs | how often a debug build prints its statistics |

## Flashing

The ESP32-S3's USB is part of the chip, so the board changes USB identity while it is being
flashed: `239a:8123` while running, `303a:1001` in download mode, `239a:0123` in the UF2
bootloader. That is normal, and it is what makes flashing under WSL2 need extra steps.

### Over USB, on Linux

```bash
arduino-cli upload --fqbn "esp32:esp32:adafruit_feather_esp32s3_reversetft:PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M" --input-dir build/bluecan --port /dev/ttyACM0 bluecan/firmware/bluecan
```

The board resets into download mode by itself. If the upload cannot find it, put it there
by hand — hold **D0**, press and release **RESET**, release D0 — and run the upload again.

### By UF2, with no tools at all

The board's bootloader can appear as a USB drive. `./build.sh bluecan` has already made
the `.uf2`; from a plain `arduino-cli` build, convert it:

```bash
python3 bluecan/firmware/tools/uf2conv.py build/bluecan/bluecan.ino.bin --base 0x10000 --family 0xc47e5767 --convert --output bluecan.uf2
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
2. **Bind both of the board's identities**, from an administrator PowerShell. `usbipd list`
   shows the bus id. Bind the board while it is running a sketch (`239a:8123`), then put it
   into download mode — hold D0, tap RESET — and bind it again as `303a:1001`:
   `usbipd bind --busid <BUSID>`. Bindings survive reboots.
3. Attach it after every Windows restart, and after every flash:
   [firmware/tools/usb-attach.sh](firmware/tools/usb-attach.sh). It needs no administrator.

[firmware/tools/reflash-bluecan.sh](firmware/tools/reflash-bluecan.sh) does the build and
the upload together and re-attaches the board around its identity changes. The first
attempt usually fails while the new identity is being attached; the script retries.

```bash
bluecan/firmware/tools/reflash-bluecan.sh
```

```bash
bluecan/firmware/tools/reflash-bluecan.sh --debug
```

The UF2 drive is visible from Windows; copy the `.uf2` there from Explorer if WSL does not
see it under `/mnt`.

## The serial console

115 200 baud on `/dev/ttyACM0` (a COM port on Windows). Two things make it look dead when
it is not:

- **The board prints only when the host asserts DTR.** `cat /dev/ttyACM0` shows nothing.
  Use a terminal that sets DTR, or [firmware/tools/bench.py](firmware/tools/bench.py),
  which works as a plain reader for any sketch: `bluecan/firmware/tools/bench.py 10` reads
  for 10 seconds. Asserting DTR does not reset this board.
- **A crash does not appear on it.** The ESP32's crash report goes to a UART that is not
  wired to anything; what the computer sees is the USB device disappearing. A board that
  restarts on its own shows its uptime on the BUS page going back to zero.

Opening the console does not reset the board, and it can be held open indefinitely.

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
