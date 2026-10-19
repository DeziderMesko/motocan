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
#include "../utils/timer.hpp"

#include <atomic>

#include <freertos/task.h>

namespace ui
{

/**
 * The status light: one RGB pixel that says, readable from a moving bike, how far the
 * chain from the bus to the phone works. Hue is where the chain is, motion is whether
 * frames are moving, and a red flash over it is a fault that has happened since the
 * start. Built with CONFIG_STATUS_LED, on whatever pixel the board file names
 * (STATUS_LED_PIN); src/led drives the pixel, this decides what it shows.
 *
 * It has a task of its own on core 0, so that it works the same on a board with a
 * display and one without. The pixel is written from that task and no other: one
 * WS2812 write blocks for ~30 us, which is nothing on core 0 and would be the whole
 * budget next to the CAN interrupt on core 1.
 */
class status_light final
{
    CPP_NOCOPY(status_light);
    CPP_NOMOVE(status_light);

public:
    static status_light& get() noexcept;

    ~status_light() noexcept = default;

#if defined(CONFIG_STATUS_LED)
    /**
     * Start the light's task on core 0. From setup(), before the CAN controller, so
     * that the light says "booting" while the rest comes up.
     */
    bool start() noexcept;

    /**
     * Dark, or lit again. For the Feather's light button, whose third position turns
     * the pixel off for riding at night. Any task.
     */
    void set_dark(bool dark) noexcept;

    /**
     * Dark for good, before deep sleep: a WS2812 keeps the last colour it was sent for
     * as long as it has power, and the Lite's always has. Returns once the task has
     * written the pixel dark, or after 100 ms.
     */
    void stop() noexcept;
#else
    bool start() noexcept { return true; }
    void set_dark(bool) noexcept {}
    void stop() noexcept {}
#endif

private:
    explicit status_light() noexcept;

#if defined(CONFIG_STATUS_LED)
    static void task(void*);
    void run() noexcept;

    /// the fault latch and the forwarded rate, from the counters, once a second
    void sample() noexcept;
    /// one animation frame
    void update() noexcept;

#if defined(DEBUG)
    /// say on the console what colour the light is on, and why: on the stats cadence,
    /// and the moment it changes. See tools/led-state.py.
    void report() noexcept;
#endif

    TaskHandle_t _handle;
    std::atomic<bool> _dark;
    std::atomic<bool> _stop;
    std::atomic<bool> _stopped;

    // --- the light's task only
    /// the fault overlay, once armed. Latching, because dropped and lost are
    /// cumulative counters and the moment one of them moves is the moment nobody is
    /// looking. Only a restart clears it; the Feather's BUS page says which it was.
    bool _fault;
    /// whether the pixel is dark now, by set_dark()
    bool _is_dark;
    /// millis() of the last second in which a frame was forwarded. What separates a
    /// slow link from a stopped one, which nothing else on the board can do.
    uint32_t _flow_ms;
    /// frames forwarded per second, over the last second
    uint32_t _fwd_rate;
    /// the forwarded count and millis() at the last sample
    uint32_t _fwd_prev;
    uint32_t _sample_ms;
    /// RCDEV.lost() at the last sample. The fault latch wants the growth, not the
    /// total: a test connection asks for every id and is not rate-limited, so
    /// refusals during one are the documented behaviour rather than a fault, and the
    /// count they leave behind must not arm the light for the session after it.
    uint32_t _lost_prev;
    /// what the light is doing, packed so a change is one comparison:
    /// rung << 16 | hz << 8 | fault, or all ones while dark
    uint32_t _state;
#if defined(DEBUG)
    utils::timer _stats_timer;
#endif
#endif
};

} // namespace ui

extern ui::status_light& LIGHT;
