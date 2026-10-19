// MIT License
//
// Copyright (c) 2026 Dezider Mesko <dezo.mesko@pm.me>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once

// Adafruit Feather ESP32-S3 Reverse TFT (#5691): the board Bluecan was developed on.
// Profile `feather` in tools/reflash-bluecan.sh.
//
// Built as the AtomS3 Lite is, on purpose: PSRAM disabled, though this board has 2 MB,
// and Serial on the chip's own USB serial port (USBMode=hwcdc) instead of TinyUSB.
// Both are build options, set in the script's profile and nowhere else. They are
// there so that what is measured on this board on the bench is what the Lite will
// run: the same heap, with nothing over 4 KB quietly placed in PSRAM, and the same USB
// stack, console and resets. Nothing in the firmware depends on either. Nothing
// allocates from PSRAM by name, the debug statistics print its free size, 0 without
// it, and the one place that knows the USB stacks apart (the serial timeout in
// setup()) follows the build option by itself. Changing them back -- PSRAM=enabled,
// and the USBMode option left out for TinyUSB -- needs no other change. The tools
// find the board under either USB identity, 239a:8123 with TinyUSB, 303a:1001 without.

#define CAN_RX_PIN GPIO_NUM_18   // A0
#define CAN_TX_PIN GPIO_NUM_17   // A1
#define BLE_PWR_LVL ESP_PWR_LVL_P12

/// a microSD card for CONFIG_SDLOG, on header pins nothing else here uses. Not the
/// board's own SPI pins: those are the display's, and its supply is off without it
#define SDLOG_SCK  16   // A2
#define SDLOG_MOSI 15   // A3
#define SDLOG_MISO 14   // A4
#define SDLOG_CS    8   // A5

/// the 240x135 ST7789 and the three buttons; src/display drives them
#define CONFIG_DISPLAY_TFT 1

/// one WS2812 on PIN_NEOPIXEL, with its supply switched by NEOPIXEL_POWER. It is the
/// only instrument on this board readable from a moving bike, so src/status drives it
/// as a status light for the chain -- bus, queue, radio, phone. See
/// motocan/bluecan/history/firmware.md, patch 16.
#define CONFIG_STATUS_LED 1
#define STATUS_LED_PIN      PIN_NEOPIXEL
#define STATUS_LED_POWER    NEOPIXEL_POWER
#define STATUS_LED_POWER_ON NEOPIXEL_POWER_ON

/// the red LED on D13, lit once the CAN controller is up
#define BUILTIN_LED_PIN LED_BUILTIN

/// a MAX17048 fuel gauge on the I2C bus at 0x36, watching the BAT rail. It is the only
/// supply voltage anything on this board can measure -- VBUS goes to a pad and nowhere
/// else -- so it is what the panel reports, and the sign of its charge rate is what
/// says whether the bike is still feeding the device. See
/// motocan/bluecan/history/firmware.md, patch 17.
#define CONFIG_FUEL_GAUGE 1

/// define when a cell is actually plugged into the JST jack.
///
/// The gauge has no battery-presence bit, and with the jack empty it models the
/// charger's rail as though it were a cell: measured on the bench, 4.10 V, 100.0%
/// charged, discharging at 22%/hr, with the voltage not moving. Taken at face value
/// that reads "running on battery" while the board sits on USB -- backwards, and the
/// configuration the bike uses today. Two runtime tests for it were tried and both
/// failed; see motocan/bluecan/history/firmware.md, patch 17.
///
/// So the panel shows the voltage, which is true either way, and shows the state of
/// charge and the charge/discharge marker only when this says there is something to
/// report them about. **Untested**: nothing has been measured with a cell fitted yet.
// #define CONFIG_CELL_FITTED 1
