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
#include "../logging/logging.hpp"

#include <atomic>

#include <esp_timer.h>

#include "decoder.hpp"
#include "r9.hpp"

namespace canbus
{

/**
 * decoder for the Yamaha R9 (2026).
 *
 * Unlike upstream's BMW decoder this one has no static id table and no per-id rates of its
 * own: it was written before anything had been read off an R9, and what crosses the
 * radio is still the app's decision, not a table's. What it has instead is an allow-list
 * filled in at run time from RaceChrono's own requests: the app sends a deny-all and
 * then one allow per id it has a channel for, each with the notify interval it wants
 * that id at. Both are honoured here.
 *
 * Honouring them is what makes the BLE link's ceiling stop mattering. The link starts
 * refusing frames somewhere around 590 msg/s (see motocan/bluecan/usage.md, Limits),
 * but a handful of allowed ids at the intervals a phone asks for is tens of messages a
 * second, not hundreds. The ids the app never asked
 * for are dropped in the CAN ISR, before they cost a queue slot or an mbuf.
 *
 * Two things are deliberately *not* done:
 *
 * - **Until an app says otherwise, everything is forwarded.** That is the boot state,
 *   it is what makes the board usable as a bench sniffer with no phone in the room,
 *   and it is what every measurement in bluecan/ was taken against. A deny-by-default
 *   would have quietly changed what those tools measure.
 *
 * - **An allow-all request is not rate-limited.** There is no per-id slot to hold a
 *   timestamp for an id nobody has named, and learning ids inside the ISR would mean
 *   writing this table from two contexts at once. Allow-all is the sniffer case
 *   anyway; the interval it carries is read, logged and then ignored.
 *
 * Seven ids are known now (r9.hpp, from motocan/analysis/r9-signals.md), and the one thing
 * done with that knowledge here is to keep their latest payloads for the panel --
 * observe() and latest(). That is beside the allow-list, not part of it: it sees every
 * frame before the filter does, so the panel shows the bike whatever the phone asked
 * for, and it puts nothing on the radio that the app did not request.
 */
class decoder_yamahar9
    : public decoder
{
    CPP_NOCOPY(decoder_yamahar9);
    CPP_NOMOVE(decoder_yamahar9);

    /// One allowed id. RaceChrono asks for the ids it has channels for, so this is
    /// sized off the bus rather than off the protocol: the R1-family bus carries about
    /// sixteen arbitration ids in total (motocan/analysis/background/pre-bike-mapping.md),
    /// and a phone cannot usefully ask for more ids than the bike broadcasts.
    struct slot
    {
        uint32_t id;           ///< arbitration id, written by the BLE task
        uint32_t interval_us;  ///< 0 means every frame, written by the BLE task
        int64_t  last_us;      ///< last frame forwarded, written by the ISR only
    };

    static constexpr size_t max_slots = 32;

    /// The latest payload of one id the panel decodes, written by the ISR on core 1 and
    /// read by the display task on core 0. A sequence lock rather than a mutex or a
    /// bare copy: the ISR must never wait, and a reader that caught the two bytes of
    /// the engine speed from different frames would paint a number the bike never sent.
    /// seq is odd while the ISR is writing and steps by two per frame; a reader that
    /// saw it odd, or saw it move, reads again. Zero means no such frame since boot.
    struct live
    {
        uint32_t id;
        std::atomic<uint32_t> seq;
        uint8_t data[8];
        int64_t us;  ///< esp_timer_get_time() at arrival
    };

    static constexpr size_t live_slots = 7;

public:
    static decoder_yamahar9& get() noexcept
    {
        static decoder_yamahar9 instance;
        return instance;
    }

    ~decoder_yamahar9() noexcept override
    {
    }

    twai_timing_config_t timing() const noexcept override
    {
        return TWAI_TIMING_CONFIG_500KBITS();
    }

    twai_filter_config_t filter() const noexcept override
    {
        // No hardware filter. It is configured once in controller::install(), long
        // before the app connects and says what it wants, and its code/mask pair
        // cannot express an arbitrary set of ids in any case. The allow-list below
        // does the job a few instructions later, in the same ISR.
        return TWAI_FILTER_CONFIG_ACCEPT_ALL();
    }

    /**
     * called from the CAN ISR for every standard frame received.
     *
     * No allocation and no blocking lock: a linear scan of at most max_slots aligned
     * comparisons, and at most one systimer read. esp_timer_get_time() is safe here --
     * it is in IRAM and reads a counter register.
     */
    bool should_decode(uint32_t id) noexcept override
    {
        if (_allow_all.load(std::memory_order_relaxed))
        {
            return true;
        }

        // acquire against the release store in allow_id(): a slot is only visible
        // once every one of its fields has been written.
        const size_t count = _count.load(std::memory_order_acquire);

        for (size_t i = 0; i < count; i++)
        {
            slot& s = _slots[i];

            if (s.id != id)
            {
                continue;
            }

            if (s.interval_us == 0U)
            {
                return true;
            }

            const int64_t now = esp_timer_get_time();

            // >= rather than >, so an interval the bus happens to land exactly on is
            // forwarded rather than held back a whole period.
            if (now - s.last_us >= static_cast<int64_t>(s.interval_us))
            {
                s.last_us = now;
                return true;
            }

            return false;
        }

        return false;
    }

    /**
     * called from the CAN ISR for every standard data frame, before should_decode().
     *
     * Seven comparisons for the frames the panel does not decode, which is most of
     * them; eight bytes and one systimer read for the ~173 a second that it does.
     */
    void observe(uint32_t id, const uint8_t* data, uint8_t dlc) noexcept override
    {
        for (size_t i = 0; i < live_slots; i++)
        {
            live& l = _live[i];

            if (l.id != id)
            {
                continue;
            }

            const uint32_t seq = l.seq.load(std::memory_order_relaxed);

            l.seq.store(seq + 1U, std::memory_order_release);
            // Zero-padded: 0x27C is seven bytes and 0x512 four, and r9.hpp only reads
            // the bytes a frame has, but a short frame must not leave the tail of a
            // longer one behind for anything that looks.
            for (uint8_t b = 0; b < 8; b++)
            {
                l.data[b] = b < dlc ? data[b] : 0U;
            }
            l.us = esp_timer_get_time();
            l.seq.store(seq + 2U, std::memory_order_release);
            return;
        }
    }

    bool latest(uint32_t id, uint8_t data[8], uint32_t& age_ms) const noexcept override
    {
        for (size_t i = 0; i < live_slots; i++)
        {
            const live& l = _live[i];

            if (l.id != id)
            {
                continue;
            }

            // The ISR holds a slot for well under a microsecond and writes one at most
            // 100 times a second, so a second attempt is already unlikely. A reader
            // that loses every one of these reports nothing rather than a torn frame;
            // the panel repaints in 200 ms and asks again.
            for (int attempt = 0; attempt < 4; attempt++)
            {
                const uint32_t before = l.seq.load(std::memory_order_acquire);

                if (before == 0U)
                {
                    return false;
                }
                if (before & 1U)
                {
                    continue;
                }

                uint8_t copy[8];
                for (uint8_t b = 0; b < 8; b++)
                {
                    copy[b] = l.data[b];
                }
                const int64_t us = l.us;

                if (l.seq.load(std::memory_order_acquire) == before)
                {
                    for (uint8_t b = 0; b < 8; b++)
                    {
                        data[b] = copy[b];
                    }
                    age_ms = static_cast<uint32_t>((esp_timer_get_time() - us) / 1000);
                    return true;
                }
            }

            return false;
        }

        return false;
    }

    int filter_size() const noexcept override
    {
        if (_allow_all.load(std::memory_order_relaxed))
        {
            return -1;
        }

        return static_cast<int>(_count.load(std::memory_order_relaxed));
    }

    uint32_t filter_overflow() const noexcept override
    {
        return _overflow.load(std::memory_order_relaxed);
    }

protected:
    uint16_t rate(uint32_t) const noexcept override
    {
        // the base class's frame-count divider is unused here: this decoder gates on
        // elapsed time instead, because the app states its wish in milliseconds and
        // the native rate of an id on this bike is exactly what is not known yet.
        return rate_default;
    }

    void deny_all() noexcept override
    {
        _allow_all.store(false, std::memory_order_relaxed);
        // The overflow count describes the list that is being thrown away, so it goes
        // with it. Left standing it would mark every later session -- the panel drawing
        // "flt 3!" in red and the fault latch re-arming every second -- for a channel
        // that fits and is not being dropped. The fault itself stays latched from the
        // moment it was first seen, which is the part that is meant to survive.
        _overflow.store(0U, std::memory_order_relaxed);
        // Emptying the list is one release store. A concurrent ISR either sees the old
        // count and forwards one more frame, or sees zero and forwards none; both are
        // correct answers to a deny that arrived mid-frame.
        _count.store(0U, std::memory_order_release);
    }

    void allow_all(uint16_t interval_ms) noexcept override
    {
        // read and logged by the caller, then ignored on purpose -- see the class
        // comment. There is nowhere to keep a timestamp for an id nobody has named.
        (void) interval_ms;
        _allow_all.store(true, std::memory_order_relaxed);
    }

    void allow_id(uint32_t id, uint16_t interval_ms) noexcept override
    {
        const uint32_t interval_us = static_cast<uint32_t>(interval_ms) * 1000U;
        const size_t count = _count.load(std::memory_order_relaxed);

        // already listed: update the interval in place. The ISR reading the old value
        // for one more frame is not worth ordering against.
        for (size_t i = 0; i < count; i++)
        {
            if (_slots[i].id == id)
            {
                _slots[i].interval_us = interval_us;
                return;
            }
        }

        if (count >= max_slots)
        {
            // Counted, not just logged. A release build has no console, and an id the
            // rider defined a channel for that silently never arrives is the worst
            // possible way for this to fail.
            _overflow.fetch_add(1, std::memory_order_relaxed);
            warnln("ID request ALLOW ID 0x%03x DROPPED, allow-list full (%u)", id, max_slots);
            return;
        }

        _slots[count].id = id;
        _slots[count].interval_us = interval_us;
        _slots[count].last_us = 0;

        // Publish last, and with release ordering: the ISR must never see a slot whose
        // id is set but whose interval still holds whatever was there before.
        _count.store(count + 1U, std::memory_order_release);
    }

private:
    explicit decoder_yamahar9() noexcept
        : decoder(0)
        , _slots{}
        , _count(0U)
        , _allow_all(true)
        , _overflow(0U)
        , _live{}
    {
        // in order of arrival rate, so the scan in observe() stops early most often
        _live[0].id = r9::id_engine;
        _live[1].id = r9::id_throttle;
        _live[2].id = r9::id_valve_cmd;
        _live[3].id = r9::id_rear;
        _live[4].id = r9::id_front;
        _live[5].id = r9::id_display;
        _live[6].id = r9::id_temps;
    }

    /// written by the BLE task, read by the CAN ISR
    slot _slots[max_slots];
    std::atomic<size_t> _count;
    std::atomic<bool> _allow_all;
    std::atomic<uint32_t> _overflow;

    /// written by the CAN ISR, read by the display task; ids set once, here
    live _live[live_slots];
};

} // namespace canbus

#if defined(CONFIG_CANBUS_DECODER_YAMAHAR9)
canbus::decoder& CANDEC = canbus::decoder_yamahar9::get();
#endif
