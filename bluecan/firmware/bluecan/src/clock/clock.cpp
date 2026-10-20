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
#include "../racechrono-canbus.hpp"

#include "clock.hpp"
#include "../logging/logging.hpp"

#include <cstdlib>
#include <sys/time.h>

#include <esp_attr.h>

namespace wallclock
{

namespace
{

/// The last setting, in RTC memory, which a restart and a sleep keep and a power-on
/// fills with whatever it came up with: the word says it was written. A clock that
/// reads before earliest_s says the power has been off since, whatever the word.
constexpr uint32_t kept_magic = 0x434C4F4BU;   // "CLOK"

struct kept_t
{
    uint32_t magic;
    int64_t at_ms;       //!< when, by the phone
    setting measured;
};

RTC_NOINIT_ATTR kept_t kept;

} // namespace

void on_boot() noexcept
{
    setenv("TZ", CONFIG_CLOCK_TZ, 1);
    tzset();

    if (!known())
    {
        bootln("Clock: not set since the power came on");
        return;
    }

    const time_t t = static_cast<time_t>(now_ms() / 1000);
    struct tm local;
    localtime_r(&t, &local);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);

    int64_t at_s = 0;
    setting measured{};
    if (last(at_s, measured))
    {
        bootln("Clock: %s, set %lld min ago", text, static_cast<long long>((t - at_s) / 60));
    }
    else
    {
        bootln("Clock: %s", text);
    }
}

bool known() noexcept
{
    return now_ms() >= earliest_s * 1000;
}

int64_t now_ms() noexcept
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

setting set(int64_t unix_ms) noexcept
{
    const int64_t before = now_ms();

    setting s{};
    int64_t at_s = 0;
    setting previous{};
    if (known() && last(at_s, previous))
    {
        s.was_known = true;
        s.error_ms = before - unix_ms;
        s.since_s = (unix_ms - kept.at_ms) / 1000;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(unix_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((unix_ms % 1000) * 1000);
    settimeofday(&tv, nullptr);

    kept = {kept_magic, unix_ms, s};
    return s;
}

bool last(int64_t& at_s, setting& measured) noexcept
{
    if (kept.magic != kept_magic || !known())
    {
        return false;
    }
    at_s = kept.at_ms / 1000;
    measured = kept.measured;
    return true;
}

bool stamp(time_t t, char* out, size_t size) noexcept
{
    if (size > 0U)
    {
        out[0] = '\0';
    }
    if (static_cast<int64_t>(t) < earliest_s)
    {
        return false;
    }
    struct tm local;
    localtime_r(&t, &local);
    return strftime(out, size, "%Y-%m-%d_%H-%M-%S%z", &local) > 0U;
}

} // namespace wallclock
