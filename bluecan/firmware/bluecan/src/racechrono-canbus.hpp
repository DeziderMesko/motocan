// MIT License
//
// Copyright (c) 2022 Joe Roback <joe.roback@gmail.com>
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

#include <Arduino.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

/// the decoder for the Yamaha R9
#define CONFIG_CANBUS_DECODER_YAMAHAR9 1

/// Let the controller acknowledge frames, once it has proved the bit rate by
/// receiving in listen-only. Undefined, the controller stays listen-only for good and
/// the TX pad is never connected to it, which is what every board ran until the R9.
///
/// The R9 needs it: its diagnostic connector is a pair with only the ECU on it, and
/// with nothing acknowledging the ECU resends one frame ~3800 times a second and 2 ids
/// of 23 get through (motocan/analysis/r9-signals.md). What goes on the wire is the
/// ACK bit and, over a frame received broken, an error flag. No frame is ever sent --
/// nothing in this firmware writes the TX buffer -- and controller::supervise() takes
/// the controller back to listen-only if the bus stops looking healthy.
#define CONFIG_CANBUS_ACK 1

/// The Wi-Fi mode, entered by holding the board's button for five seconds: the bus's
/// statistics and the card's files on a web page, in place of Bluetooth, never beside
/// it. See src/mode/ and src/web/. CONFIG_NO_WIFI leaves it out.
#if !defined(CONFIG_NO_WIFI)
#define CONFIG_WIFI 1
#endif

/// statistics timeout in microseconds
#define CONFIG_RC_STATS_TIMEOUT 5000000

// Connection parameters to request from the phone once it connects, in the units the
// link layer uses: intervals in 1.25 ms, timeout in 10 ms.
//
// The interval is the ceiling on throughput -- the radio only speaks at connection
// events -- so the R9's ~1050 msg/s wants this as short as it will go, and 7.5 ms is
// as short as BLE goes.
//
// This is the Android window. **Change it before pairing an iPhone**: iOS rejects a
// connection-parameter request outright unless min >= 15 ms, max >= min + 15 ms,
// latency <= 30 and timeout <= 6 s, and a rejected request leaves whatever the phone
// picked on its own -- worse than asking for less. The iOS-safe pair is 12/24
// (15-30 ms), legal on both platforms.
//
// Either way the phone has the final say, so the negotiated interval is read back in
// onConnParamsUpdate() and shown in the display header rather than assumed.
#define CONFIG_BLE_CONN_ITVL_MIN 6    // 7.5 ms  (iOS: 12, 15.0 ms)
#define CONFIG_BLE_CONN_ITVL_MAX 10   // 12.5 ms (iOS: 24, 30.0 ms)
#define CONFIG_BLE_CONN_LATENCY  0
#define CONFIG_BLE_CONN_TIMEOUT  400  // 4 s

/// Send frames through BLECharacteristic::notify() instead of straight at the NimBLE
/// host. Slower by three heap allocations and a mutex per frame, and kept only so the
/// two paths can be measured against each other on one afternoon's connection -- the
/// phone renegotiates the connection interval on every reconnect, so an A/B that needs
/// two firmware builds needs them within the same negotiated interval to mean anything.
// #define RC_WRAPPER_NOTIFY

/// if DEBUG is defined, logger will be enabled and print to serial console
// #define DEBUG

/// _x branch is likely to be true
#define RCLIKELY(_x)    __builtin_expect(!!(_x), 1)

/// _x branch is unlikely to be true
#define RCUNLIKELY(_x)  __builtin_expect(!!(_x), 0)

/// assert _expr is true (always not just DEBUG mode)
#define RCASSERT(_expr) do {                  \
    if (RCUNLIKELY(!(_expr))) { abort(); }    \
} while (0)

// the board: its pins, and what it has. One file per board, under src/boards/
#include "boards/board.hpp"

/// disable copy
#define CPP_NOCOPY(_name)                     \
    _name(_name const&) = delete;             \
    _name& operator=(_name const&) = delete

/// disable move
#define CPP_NOMOVE(_name)                     \
    _name(_name&&) = delete;                  \
    _name& operator=(_name&&) = delete

// C++11 ugh
template <typename T, typename U = T>
inline T exchange(T& obj, U&& new_val)
{
    T old_val = std::move(obj);
    obj = std::forward<U>(new_val);
    return old_val;
}
