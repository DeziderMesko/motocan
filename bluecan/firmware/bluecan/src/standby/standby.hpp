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

#include "../racechrono-canbus.hpp"

#include <cstdint>

/// CONFIG_STANDBY puts the chip into deep sleep once the bus has been silent for
/// CONFIG_STANDBY_TIMEOUT_S, and wakes it on the first dominant bit on CAN RX. It is for
/// a board powered from the diagnostic connector's unswitched 12 V: with the key off
/// the bus is silent, and with the key on the engine control unit repeats its first
/// frame until something acknowledges it, so the frame that wakes the board is sent
/// again once it is listening. Waking is a restart: setup() runs from the top.
#if defined(CONFIG_STANDBY)

#if defined(CONFIG_DISPLAY_TFT)
#error "CONFIG_STANDBY does not put the display to sleep; build it for the AtomS3 Lite or with CONFIG_LIKE_ATOMS3_LITE"
#endif

/// seconds without a received frame before the chip goes to sleep. The R9 sends over
/// 500 frames a second with the key on, so five seconds of nothing is the key off
#ifndef CONFIG_STANDBY_TIMEOUT_S
#define CONFIG_STANDBY_TIMEOUT_S 5
#endif

#endif // CONFIG_STANDBY

namespace standby
{

#if defined(CONFIG_STANDBY)
/**
 * First thing in setup(): give the CAN pins back to the GPIO matrix if the chip has
 * just woken from standby, and say so.
 * @return true when this start is a wake from standby rather than a power-on
 */
bool on_boot() noexcept;

/**
 * From loop(): sleep once no frame has arrived for CONFIG_STANDBY_TIMEOUT_S.
 * Does not return if it sleeps.
 */
void check() noexcept;
#else
inline bool on_boot() noexcept { return false; }
inline void check() noexcept {}
#endif

} // namespace standby
