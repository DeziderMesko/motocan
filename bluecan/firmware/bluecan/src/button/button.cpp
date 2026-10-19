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

#include "button.hpp"

#if defined(BUTTON_PIN)

#include "../logging/logging.hpp"
#include "../utils/timer.hpp"

#include <atomic>

namespace button
{

namespace
{

/// how often the pin is read; a press or a release must read the same twice in a row
/// to count, which is all the debouncing a five-second hold needs
constexpr unsigned long sample_us = 10000UL;

utils::timer sample_timer;
bool last_read = false;     //!< the previous sample
bool down = false;          //!< the debounced state
uint32_t down_since_ms = 0U;
std::atomic<bool> is_ready{false};

} // namespace

void start() noexcept
{
    pinMode(BUTTON_PIN, BUTTON_PRESSED ? INPUT_PULLDOWN : INPUT_PULLUP);
}

bool poll() noexcept
{
    if (sample_timer.elapsed(sample_us) == 0UL)
    {
        return false;
    }

    const bool read = digitalRead(BUTTON_PIN) == BUTTON_PRESSED;
    const bool steady = read == last_read;
    last_read = read;
    if (!steady || read == down)
    {
        if (down && !is_ready.load(std::memory_order_relaxed)
            && millis() - down_since_ms >= CONFIG_BUTTON_HOLD_S * 1000U)
        {
            is_ready.store(true, std::memory_order_relaxed);
            bootln("Button: held %u s, let go to switch", (unsigned)CONFIG_BUTTON_HOLD_S);
        }
        return false;
    }

    down = read;
    if (down)
    {
        down_since_ms = millis();
        return false;
    }

    const bool was_ready = is_ready.load(std::memory_order_relaxed);
    is_ready.store(false, std::memory_order_relaxed);
    return was_ready;
}

bool ready() noexcept
{
    return is_ready.load(std::memory_order_relaxed);
}

} // namespace button

#endif // BUTTON_PIN
