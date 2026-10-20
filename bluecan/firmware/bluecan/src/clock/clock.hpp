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

#include <cstddef>
#include <cstdint>
#include <ctime>

/// The time zone the card's file times are written in, as POSIX TZ: Prague's, CET and
/// CEST. FAT keeps local time with no zone, and a computer reading the card takes it as
/// its own.
#ifndef CONFIG_CLOCK_TZ
#define CONFIG_CLOCK_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

/// The Wi-Fi page sets the clock only if it was last set longer ago than this, in
/// seconds: a page opened again soon after keeps the setting before, and with it the
/// drift that setting measured
#ifndef CONFIG_CLOCK_KEEP_S
#define CONFIG_CLOCK_KEEP_S 600
#endif

/// The wall-clock time, as the chip's system clock keeps it. It runs through light and
/// deep sleep and survives a restart -- the switch between modes, the watchdog -- and
/// is lost only with the power: a power-on starts it at 1970. It is set from the Wi-Fi
/// page, by the phone that opens it.
///
/// Asleep it runs on the chip's internal RC oscillator, CONFIG_RTC_CLK_SRC_INT_RC; awake
/// on the crystal. How far it drifts in a night asleep is what each setting measures:
/// before it sets the clock, set() takes the difference from the phone's time and how
/// long it has been since the last setting.
///
/// The card's file times come from this clock through FatFs, in CONFIG_CLOCK_TZ. Until
/// it is set they read 1980-01-01, the earliest FAT has.
namespace wallclock
{

/// 2026-01-01T00:00:00Z: a clock earlier than this has not been set since the power came
/// on
constexpr int64_t earliest_s = 1767225600;

/**
 * In setup(): the time zone, and a console line with the time and the last setting.
 */
void on_boot() noexcept;

/**
 * Whether the clock has been set since the power came on. Any task.
 */
bool known() noexcept;

/**
 * Milliseconds since the epoch, by this clock. Any task.
 */
int64_t now_ms() noexcept;

/// one setting, as set() measured it
struct setting
{
    bool was_known;   //!< the clock had been set before; the rest is zero if not
    int64_t error_ms; //!< this clock minus the phone's, just before this setting
    int64_t since_s;  //!< the phone's time since the setting before
};

/**
 * Set the clock to \p unix_ms, the phone's time, and say how far off it was.
 * @return what it measured
 */
setting set(int64_t unix_ms) noexcept;

/**
 * When the clock was last set, in seconds since the epoch, and what that setting
 * measured; false if it has not been set since the power came on.
 */
bool last(int64_t& at_s, setting& measured) noexcept;

/**
 * \p t as local time for a file name, 2026-10-21_17-42-05+0200: sortable, and with its
 * offset from UTC, so that the hour of a change to or from summer time reads one way.
 * @return false, with \p out empty, for a time from before the clock was set
 */
bool stamp(time_t t, char* out, size_t size) noexcept;

} // namespace wallclock
