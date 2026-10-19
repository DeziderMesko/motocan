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

/// What a start of the board runs as: the normal mode, Bluetooth to RaceChrono and the
/// card's log, or the Wi-Fi mode of CONFIG_WIFI, a web page with the bus's statistics
/// and the card's files. Never both: the chip has one 2.4 GHz radio, and the frame path
/// to the phone is built to get every frame through it.
///
/// Switching is a restart, with the mode for the next start left in RTC memory. The
/// start reads it and clears it, so any other restart -- the watchdog, a crash, a power
/// cut -- comes back in the normal mode.
namespace mode
{

enum class run : uint8_t
{
    normal = 0,
    wifi = 1,
};

#if defined(CONFIG_WIFI)
/**
 * In setup(), after standby::on_boot(): what this start runs as.
 */
run on_boot() noexcept;

/**
 * What on_boot() found. Any task.
 */
run current() noexcept;

/**
 * Write out the card, leave \p r for the next start, and restart. From loop().
 */
[[noreturn]] void restart_into(run r) noexcept;
#else
inline run on_boot() noexcept { return run::normal; }
inline run current() noexcept { return run::normal; }
#endif

} // namespace mode
