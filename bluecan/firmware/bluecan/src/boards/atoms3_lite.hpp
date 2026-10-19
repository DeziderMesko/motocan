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

// M5Stack AtomS3 Lite (ESP32-S3FN8: 8 MB flash, no PSRAM), powered from the diagnostic
// connector's unswitched 12 V through a Unit Mini CAN. Profile `atom` in
// tools/reflash-bluecan.sh. Built, not yet run: no Lite has been on the bench.
//
// The core has one variant for the Lite and the AtomS3 with a display; nothing here
// drives either's extras. Its RGB LED (G35) is dark for now: the status light lives in
// the display's task, which this board does not run.

/// CAN through the Unit Mini CAN (TJA1051T/3) on the Grove port, as M5Stack's own
/// MiniCAN_TJA1051T example has it; the Atomic CAN Base would be RX G6, TX G5 instead
#define CAN_RX_PIN GPIO_NUM_1    // Grove, white
#define CAN_TX_PIN GPIO_NUM_2    // Grove, yellow
#define BLE_PWR_LVL ESP_PWR_LVL_P12

/// the Atomic TF-Card Base, as M5Stack's Arduino example for it has it: no chip
/// select, the base holds the card selected
#define SDLOG_SCK  7
#define SDLOG_MOSI 6
#define SDLOG_MISO 8
#define SDLOG_CS   -1

/// on unswitched 12 V it would draw from the bike's battery all the time the key is
/// off, so it sleeps while the bus is silent; CONFIG_NO_STANDBY keeps it awake
#if !defined(CONFIG_NO_STANDBY)
#define CONFIG_STANDBY 1
#endif
