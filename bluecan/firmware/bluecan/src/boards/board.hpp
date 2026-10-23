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

// Which board this build is for. The core names the board (ARDUINO_*, from the FQBN).
// A board file gives the pins and says what is on the board; the modules ask only for
// the CONFIG_* switches it sets, never for a board.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_REVTFT)
#include "feather_revtft.hpp"
#elif defined(ARDUINO_M5STACK_ATOMS3)
#include "atoms3_lite.hpp"
#else
#error "Bluecan is built for the Adafruit Feather ESP32-S3 Reverse TFT or the M5Stack AtomS3 Lite; see bluecan/build.md"
#endif

#include "check.hpp"
