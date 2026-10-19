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

/// CONFIG_STANDBY puts the chip into light sleep once the bus has been silent for
/// CONFIG_STANDBY_TIMEOUT_S. The first dominant bit on CAN RX wakes it, and so does the
/// board's button where the board file gives one (BUTTON_PIN). It is for a board powered
/// from the diagnostic connector's unswitched 12 V: with the key off the bus is silent,
/// and with the key on the engine control unit repeats its first frame until something
/// acknowledges it, so the frame that wakes the board is sent again once it is listening.
///
/// Going to sleep is a restart: the chip sleeps at the top of setup(), before anything
/// else is running, and restarts again when it wakes. Light sleep rather than
/// deep because the Lite's button, G41, cannot wake the chip from deep sleep; what that
/// costs is in bluecan/hardware.md, "Current while asleep".
#if defined(CONFIG_STANDBY)

/// seconds without a received frame before the chip goes to sleep. The R9 sends over
/// 500 frames a second with the key on, so a second of nothing is the key off; the
/// rest is time to use the board after waking it with the button, or on the bench
#ifndef CONFIG_STANDBY_TIMEOUT_S
#define CONFIG_STANDBY_TIMEOUT_S 60
#endif

#endif // CONFIG_STANDBY

namespace standby
{

/// what this start is. Kept through a restart as a number, so the numbers are fixed,
/// and power_on, the one that means no wake, is 0
enum class start : uint8_t
{
    power_on = 0,   //!< a power-on or a reset; not a wake
    bus = 1,        //!< woken by CAN RX
    button = 2,     //!< woken by the button
};

#if defined(CONFIG_STANDBY)
/**
 * First thing in setup(). If the last start asked to sleep, sleep here and restart when
 * woken; does not return then. Otherwise give the CAN pins back to the GPIO matrix and
 * return what this start is: a power-on, or the start after a wake and what woke it.
 */
start on_boot() noexcept;

/**
 * From loop(): sleep once no frame has arrived for CONFIG_STANDBY_TIMEOUT_S.
 * Does not return if it sleeps.
 */
void check() noexcept;
#else
inline start on_boot() noexcept { return start::power_on; }
inline void check() noexcept {}
#endif

} // namespace standby
