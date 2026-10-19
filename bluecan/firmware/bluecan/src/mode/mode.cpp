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

#include "mode.hpp"

#if defined(CONFIG_WIFI)

#include "../logging/logging.hpp"
#include "../sdlog/sdlog.hpp"

#include <esp_attr.h>
#include <esp_system.h>

namespace mode
{

namespace
{

// Not initialised by anything, so only a word written by restart_into() counts: a
// power-on leaves whatever the memory came up with.
constexpr uint32_t next_wifi = 0x57494649U;   // "WIFI"
RTC_NOINIT_ATTR uint32_t next;

run this_run = run::normal;

} // namespace

run on_boot() noexcept
{
    this_run = next == next_wifi ? run::wifi : run::normal;
    next = 0U;
    if (this_run == run::wifi)
    {
        bootln("Mode: Wi-Fi");
    }
    return this_run;
}

run current() noexcept
{
    return this_run;
}

void restart_into(run r) noexcept
{
    bootln("Mode: restarting into %s", r == run::wifi ? "Wi-Fi" : "normal");

    // What is in the card's ring goes out before the restart. Nothing to do in the
    // Wi-Fi mode, which does not write the card.
    SDLOG.stop();

    next = r == run::wifi ? next_wifi : 0U;
    Serial.flush();
    esp_restart();
}

} // namespace mode

#endif // CONFIG_WIFI
