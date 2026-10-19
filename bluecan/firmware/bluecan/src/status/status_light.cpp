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

#include "status_light.hpp"

#if defined(CONFIG_STATUS_LED)

#include "../button/button.hpp"
#include "../canbus/controller.hpp"
#include "../canbus/decoder.hpp"
#include "../led/led.hpp"
#include "../logging/logging.hpp"
#include "../mode/mode.hpp"
#include "../racechrono/device.hpp"
#include "../web/web.hpp"

#include <hal/twai_ll.h>

namespace
{

/// the animation's frame time
constexpr uint32_t frame_ms = 25;
/// how often the fault latch and the forwarded rate are brought up to date
constexpr uint32_t sample_ms = 1000;

/// bytes: StackType_t is a byte on ESP-IDF. The deepest thing the task does is a
/// debug line, which formats into the logger's 128-byte buffer
constexpr uint32_t stack_size = 3072;
StaticTask_t task_buffer;
StackType_t task_stack[stack_size];

/// how long the forwarded rate must read zero before the light calls it stopped
/// rather than slow. Two seconds is four samples of a rate that updates once a
/// second, and forty times the gap between frames at the ~20 msg/s per id a
/// recording session actually produces.
constexpr uint32_t flow_dead_ms = 2000;
/// how often the flatline's pair of flashes repeats, how long each one lasts, and
/// how far the second trails the first
constexpr uint32_t flatline_ms = 2000;
constexpr uint32_t flatline_flash_ms = 90;
constexpr uint32_t flatline_gap_ms = 240;
/// how long a blip, a flatline pulse or a fault flash lasts
constexpr uint32_t flash_ms = 60;
/// what a blip falls back to between flashes. Not zero: the hue is the state, and
/// dropping it entirely would make a slow link indistinguishable from a dark board.
constexpr uint8_t blip_floor = 64;
/// the fault overlay's period
constexpr uint32_t fault_ms = 1000;

/// 0-255 triangle over one period. A triangle rather than a sine because it costs
/// two branches and a multiply, and at this brightness the difference is invisible.
uint8_t breathe(uint32_t now, uint32_t period_ms) noexcept
{
    const uint32_t phase = now % period_ms;
    const uint32_t half = period_ms / 2U;
    const uint32_t up = phase < half ? phase : period_ms - phase;
    return static_cast<uint8_t>((up * 255U) / half);
}

/// square wave, on for the first half of the period
__always_inline uint8_t blink(uint32_t now, uint32_t period_ms) noexcept
{
    return (now % period_ms) < (period_ms / 2U) ? 255U : 0U;
}

/// the blip rate for a forwarded-frame rate. Clamped at both ends: below 1 Hz it
/// would be indistinguishable from the flatline it exists to contrast with, and
/// above 5 Hz from a solid colour.
__always_inline uint8_t blip_hz(uint32_t rate) noexcept
{
    uint32_t hz = rate / 8U;
    if (hz < 1U) { hz = 1U; }
    if (hz > 5U) { hz = 5U; }
    return static_cast<uint8_t>(hz);
}

/// a short flash on a dim floor, at that rate
__always_inline uint8_t blip(uint32_t now, uint8_t hz) noexcept
{
    const uint32_t period = 1000U / hz;
    return (now % period) < flash_ms ? 255U : blip_floor;
}

/// scale one component of a full-scale colour by a 0-255 pattern level
__always_inline uint8_t level(uint8_t component, uint8_t lvl) noexcept
{
    return static_cast<uint8_t>((static_cast<uint32_t>(component) * lvl) / 255U);
}

/// the flatline: two flashes, at full level, every flatline_ms.
///
/// The first version of this was one flash_ms blip at the blips' dim floor, which is
/// a 3% duty cycle at a quarter of a brightness already capped at 24/255 -- on the
/// bench it read as a pixel that was simply off. That is the worst possible way for
/// this rung to fail, because "connected and nothing is arriving" is the state that
/// means the diagnostic connector fell off the bike. Two flashes rather than one
/// because a single tick is hard to tell from a slow heartbeat, and this rung exists
/// precisely to be told apart from one.
__always_inline uint8_t flatline(uint32_t now) noexcept
{
    const uint32_t phase = now % flatline_ms;
    const bool lit = phase < flatline_flash_ms
                  || (phase >= flatline_gap_ms && phase < flatline_gap_ms + flatline_flash_ms);
    return lit ? 255U : 0U;
}

#if defined(DEBUG)
/// what the light is on, for the DEBUG line that reports it
const char* rung_name(uint8_t rung) noexcept
{
    switch (rung)
    {
    case 0:  return "white breathe  (CAN controller not up -- booting)";
    case 1:  return "red solid      (bus-off)";
    case 2:  return "amber blink    (error-passive)";
    case 3:  return "blue breathe   (nothing subscribed)";
    case 4:  return "yellow         (subscribed, flt ALL -- configuring)";
    case 5:  return "green          (subscribed, filtered -- recording)";
    case 6:  return "white solid    (button held long enough -- let go to switch)";
    case 7:  return "purple breathe (Wi-Fi mode, nobody on the network)";
    default: return "purple solid   (Wi-Fi mode, someone on the network)";
    }
}
#endif

} // namespace

namespace ui
{

status_light& status_light::get() noexcept
{
    static status_light instance;
    return instance;
}

status_light::status_light() noexcept
    : _handle(nullptr)
    , _dark(false)
    , _stop(false)
    , _stopped(false)
    , _fault(false)
    , _is_dark(false)
    , _flow_ms(0U)
    , _fwd_rate(0U)
    , _fwd_prev(0U)
    , _sample_ms(0U)
    , _lost_prev(0U)
    , _state(0xFFFFFFFFU)
{
}

bool status_light::start() noexcept
{
    // Level with the display task and the Bluetooth drain task, under the NimBLE
    // host. It sleeps between frames and an update costs ~30 us, so it takes nothing
    // from either worth measuring.
    _handle = xTaskCreateStaticPinnedToCore(
        task,
        "light",
        stack_size,
        this,
        tskIDLE_PRIORITY + 1,
        task_stack,
        &task_buffer,
        0
    );

    if (_handle == nullptr)
    {
        errorln("ERROR: status light task creation failed!");
        return false;
    }

    return true;
}

void status_light::set_dark(bool dark) noexcept
{
    _dark.store(dark, std::memory_order_relaxed);
}

void status_light::stop() noexcept
{
    _stop.store(true, std::memory_order_release);
    for (int i = 0; i < 10 && !_stopped.load(std::memory_order_acquire); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void status_light::task(void* arg)
{
    static_cast<status_light*>(arg)->run();
}

void status_light::run() noexcept
{
    // Here rather than lazily on the first frame: this is the call that allocates the
    // RMT channel behind the pixel, and start-up is where an allocation belongs on a
    // board whose free heap is a measurement.
    LED.status_begin();

    _sample_ms = millis();
    _flow_ms = _sample_ms;

    for (;;)
    {
        if (_stop.load(std::memory_order_acquire))
        {
            LED.status_end();
            _stopped.store(true, std::memory_order_release);
            for (;;)
            {
                vTaskDelay(portMAX_DELAY);
            }
        }

        if (millis() - _sample_ms >= sample_ms)
        {
            sample();
        }

        update();

#if defined(DEBUG)
        if (_stats_timer.elapsed(CONFIG_RC_STATS_TIMEOUT) > 0UL
            && logging::logger::get().level() >= logging::log_level::info)
        {
            report();
            infoln("      Status stack: %lu bytes free",
                   (unsigned long)uxTaskGetStackHighWaterMark(_handle));
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(frame_ms));
    }
}

void status_light::sample() noexcept
{
    const uint32_t now = millis();
    const uint32_t elapsed = now - _sample_ms;
    _sample_ms = now;

    const canbus::controller::counters_t c = CANCTLR.counters();
    _fwd_rate = elapsed == 0U ? 0U : ((c.forwarded - _fwd_prev) * 1000UL) / elapsed;
    _fwd_prev = c.forwarded;

    // The fault and the flow are sampled once a second rather than on every frame.
    // The fault latches, so a second's delay in noticing costs nothing, and this
    // keeps the frame off RCDEV.peers() -- the one term here that reaches into the
    // NimBLE server rather than reading an atomic.
    const uint32_t lost = RCDEV.lost();

    // A dropped frame, an allow-list too small for what the app asked for and a
    // second subscriber are faults whenever they happen: the first is always ours,
    // the second silently loses a channel the rider defined, the third doubles the
    // radio's work for the same bus.
    if (!_fault
        && (c.dropped != 0U || CANDEC.filter_overflow() != 0U || RCDEV.peers() > 1U))
    {
        _fault = true;
    }

    // So is the controller giving up acknowledging. On the R9 that takes the bus from
    // 23 ids to 2, and the hue alone would go on saying frames are moving.
    if (!_fault && CANCTLR.ack() > canbus::controller::ack_state::acknowledging)
    {
        _fault = true;
    }

    // A refusal is only a fault while the allow-list is in force. An allow-all --
    // what RaceChrono sends while channels are being configured -- has no per-id slot
    // to rate-limit against, so it offers the whole bus to a radio measured at about
    // 590 msg/s and refusals are the expected result. Measured on the bench the day
    // this landed: a test connection at ~919 msg/s left 15001 behind. Latching on the
    // total would light the fault for every session after that one, so this latches
    // on growth, and only once the filter is active.
    if (!_fault && CANDEC.filter_size() >= 0 && lost > _lost_prev)
    {
        _fault = true;
    }

    _lost_prev = lost;

    if (_fwd_rate != 0U)
    {
        _flow_ms = now;
    }
}

void status_light::update() noexcept
{
    const bool dark = _dark.load(std::memory_order_relaxed);
    if (dark)
    {
        // Dark, and on the Feather its supply off with it.
        if (!_is_dark)
        {
            _is_dark = true;
            LED.status_end();
#if defined(DEBUG)
            _state = 0xFFFFFFFFU;
            report();
#endif
        }
        return;
    }
    _is_dark = false;

    const uint32_t now = millis();
    const bool running = CANCTLR.running();
    const uint32_t status = running ? CANCTLR.status() : 0U;

    // One LED, so precedence is strict. Hue says where the chain is, motion says
    // frames are moving, and every term below is a counter the BUS page already
    // reads -- nothing is measured for the light's sake.
    //
    // What is deliberately absent is vehicle data. The light's subject is the chain
    // from the bus to the phone, and the one fault a rider can act on is that chain
    // breaking. The panel does decode a few signals now (canbus/r9.hpp), to be held
    // up against the dash at a standstill; a shift light would be a second meaning
    // on the only indicator readable at speed, and it is not here for that reason.
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t lvl;
    uint8_t rung;
    /// blip rate actually used, or 0 for a rung that is not blipping
    uint8_t hz = 0U;

    if (button::ready())
    {
        // The button has been held long enough: letting go switches the mode. Above
        // everything, since it is the answer to what the hand on the button is doing.
        r = 255; g = 255; b = 255;
        lvl = 255;
        rung = 6U;
    }
    else if (mode::current() == mode::run::wifi)
    {
        // The Wi-Fi mode: no phone link to report on, so one colour for the mode,
        // breathing until something joins the network and steady while it is on it.
        r = 160; g = 0; b = 255;
        if (WEB.stations() == 0U)
        {
            lvl = breathe(now, 2000U);
            rung = 7U;
        }
        else
        {
            lvl = 255;
            rung = 8U;
        }
    }
    else if (!running)
    {
        // The controller is not up. In practice this is the second and a half
        // between the display task starting and setup() reaching CANCTLR.start(),
        // because a failure there restarts the board -- so read it as booting.
        r = 255; g = 255; b = 255;
        lvl = breathe(now, 2000U);
        rung = 0U;
    }
    else if (status & TWAI_LL_STATUS_BS)
    {
        // Bus-off. Nearly unreachable: in listen-only this node's error counters
        // are frozen, and acknowledging it still never transmits a frame, which is
        // what bus-off counts. Kept for the day that assumption is wrong: two
        // comparisons is a fair price.
        r = 255; g = 0; b = 0;
        lvl = 255;
        rung = 1U;
    }
    else if (status & TWAI_LL_STATUS_ES)
    {
        r = 255; g = 140; b = 0;
        lvl = blink(now, 1000U);
        rung = 2U;
    }
    else if (!RCDEV.subscribed())
    {
        // Nobody is taking frames -- either nothing is connected, or something is
        // connected and has not subscribed. One colour for both: they differ in what
        // the rider would do about it not at all, and the header on the BUS page
        // separates them for anyone standing over the board.
        r = 0; g = 0; b = 255;
        lvl = breathe(now, 2000U);
        rung = 3U;
    }
    else
    {
        // Yellow while RaceChrono is configuring channels and asking for every id,
        // green once it is recording and asking for the handful it has channels for.
        // The difference matters: an allow-all is not rate-limited, so it is also the
        // one state in which this link is expected to refuse frames.
        const bool unfiltered = CANDEC.filter_size() < 0;

        r = unfiltered ? 255 : 0;
        g = 255;
        b = 0;
        rung = unfiltered ? 4U : 5U;

        // Slow and stopped are the two states no other indicator on this board can
        // tell apart, and a diagnostic connector that fell off is the second one.
        if ((now - _flow_ms) >= flow_dead_ms)
        {
            lvl = flatline(now);
        }
        else
        {
            hz = blip_hz(_fwd_rate);
            lvl = blip(now, hz);
        }
    }

    // The fault is an overlay, not a rung. Made a rung it would replace the hue and
    // hide whether frames were still flowing at the moment that is most worth
    // knowing; flashed over it, "recording, and something was lost" is one glance
    // from "recording".
    if (_fault && (now % fault_ms) < flash_ms)
    {
        r = 255; g = 0; b = 0;
        lvl = 255;
    }

    LED.status(level(r, lvl), level(g, lvl), level(b, lvl));

    const uint32_t state = (static_cast<uint32_t>(rung) << 16)
                         | (static_cast<uint32_t>(hz) << 8)
                         | (_fault ? 1U : 0U);

    if (state != _state)
    {
        _state = state;
#if defined(DEBUG)
        report();
#endif
    }
}

#if defined(DEBUG)
void status_light::report() noexcept
{
    if (_is_dark)
    {
        infoln("      Status light: off -- set dark by the light button");
        return;
    }

    const uint8_t rung = static_cast<uint8_t>((_state >> 16) & 0xFFU);
    const uint8_t hz = static_cast<uint8_t>((_state >> 8) & 0xFFU);

    char pattern[32];
    if (rung < 4U || rung > 5U)
    {
        pattern[0] = '\0';
    }
    else if (hz == 0U)
    {
        snprintf(pattern, sizeof(pattern), " FLATLINE -- no frames 2 s+");
    }
    else
    {
        snprintf(pattern, sizeof(pattern), " blips %u Hz", (unsigned)hz);
    }

    infoln("      Status light: %s%s%s", rung_name(rung), pattern,
           _fault ? "  + FAULT flash" : "");
}
#endif

} // namespace ui

#endif // CONFIG_STATUS_LED

namespace ui
{

#if !defined(CONFIG_STATUS_LED)
status_light& status_light::get() noexcept
{
    static status_light instance;
    return instance;
}

status_light::status_light() noexcept
{
}
#endif

} // namespace ui

ui::status_light& LIGHT = ui::status_light::get();
