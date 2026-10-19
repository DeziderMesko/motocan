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

// TEMPORARY: the Feather ESP32-S3 Reverse TFT standing in for an M5Stack AtomS3 Lite on
// the bench, until a Lite is there itself. Profile `feather-as-atom` in
// tools/reflash-bluecan.sh. When the Lite arrives, delete this file, its line in
// board.hpp and the profile in the script; nothing else refers to it.
//
// The Feather's pins, and only what the Lite has. The build options are the Feather's
// own, which are already the Lite's (see feather_revtft.hpp), so what is left
// different is the pins, the flash (4 MB here, 8 MB there) and the antenna -- and the
// status light's pixel, the Feather's NeoPixel standing in for the Lite's G35, which
// has a supply switch the Lite's does not.

#include "feather_revtft.hpp"

// not on the Lite
#undef CONFIG_DISPLAY_TFT
#undef CONFIG_FUEL_GAUGE
#undef BUILTIN_LED_PIN

// the Lite's one button, G41, played by D2: high when pressed, as the display reads it
// in the feather profile, where it freezes the screen
#define BUTTON_PIN     GPIO_NUM_2
#define BUTTON_PRESSED 1

// as on the Lite: see atoms3_lite.hpp
#if !defined(CONFIG_NO_STANDBY)
#define CONFIG_STANDBY 1
#endif
