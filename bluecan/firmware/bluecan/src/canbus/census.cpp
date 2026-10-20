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

#include "census.hpp"

#include <cstring>

namespace canbus
{

census::census() noexcept
    : _ids{}
    , _hits{}
    , _hits_prev{}
    , _rates{}
    , _dlc{}
    , _data{}
    , _seen_ms{}
    , _used(0)
    , _overflow(0U)
{
}

void census::note(frame const& f) noexcept
{
    // Relaxed: the writer is the only one that moves it.
    const int used = _used.load(std::memory_order_relaxed);
    int i = 0;
    while (i < used && _ids[i] != f.id)
    {
        i++;
    }

    if (i == used)
    {
        if (used == slots)
        {
            _overflow++;
            return;
        }
        // Filled before it is published, so that no reader sees half a slot: the
        // release is what stops these stores being sunk past it.
        _ids[i] = f.id;
        _hits[i] = 0U;
        _hits_prev[i] = 0U;
        _rates[i] = 0U;
        _used.store(used + 1, std::memory_order_release);
    }

    _hits[i]++;
    _dlc[i] = f.info.dlc > 8U ? 8U : f.info.dlc;
    memcpy(_data[i], f.data.u8, 8);
    _seen_ms[i] = millis();
}

void census::sample(uint32_t elapsed_ms) noexcept
{
    if (elapsed_ms == 0U)
    {
        return;
    }
    const int n = used();
    for (int i = 0; i < n; i++)
    {
        const uint32_t hits = _hits[i];
        _rates[i] = static_cast<uint16_t>(((hits - _hits_prev[i]) * 1000U + elapsed_ms / 2U) / elapsed_ms);
        _hits_prev[i] = hits;
    }
}

} // namespace canbus
