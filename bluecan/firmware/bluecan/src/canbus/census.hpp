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
#include "frame.hpp"

#include <atomic>
#include <cstdint>

namespace canbus
{

/**
 * Which ids are on the bus, and for each how many frames, how many a second, its last
 * payload and when it came. For the display's IDS page and the Wi-Fi mode's page.
 *
 * One task writes, note(), with every frame it takes off the queue; any task on the
 * same core reads, and one of them calls sample() once a second for the rates. There
 * is no lock: every field is a counter or a byte of payload, and a torn read is one
 * stale number for a second. The count of slots in use is the exception, and is atomic:
 * it publishes a slot only once every field of it has been written.
 */
class census final
{
public:
    /// The R9's diagnostic connector carries 23 ids (motocan/analysis/r9-signals.md);
    /// 32 is room to spare, and as many as the allow-list holds.
    static constexpr int slots = 32;

    census() noexcept;

    /// Count one frame. The writer only.
    void note(frame const& f) noexcept;

    /// Bring every rate up to date, over the \p elapsed_ms since the last call.
    void sample(uint32_t elapsed_ms) noexcept;

    /// slots in use: ids seen, up to slots
    int used() const noexcept { return _used.load(std::memory_order_acquire); }
    /// frames of ids that found no slot
    uint32_t overflow() const noexcept { return _overflow; }

    uint32_t id(int i) const noexcept { return _ids[i]; }
    uint32_t hits(int i) const noexcept { return _hits[i]; }
    uint16_t rate(int i) const noexcept { return _rates[i]; }
    uint8_t dlc(int i) const noexcept { return _dlc[i]; }
    const uint8_t* data(int i) const noexcept { return _data[i]; }
    /// millis() of the last frame
    uint32_t seen_ms(int i) const noexcept { return _seen_ms[i]; }

private:
    uint32_t _ids[slots];
    uint32_t _hits[slots];
    uint32_t _hits_prev[slots];
    uint16_t _rates[slots];
    uint8_t _dlc[slots];
    uint8_t _data[slots][8];
    uint32_t _seen_ms[slots];
    std::atomic<int> _used;
    uint32_t _overflow;
};

} // namespace canbus
