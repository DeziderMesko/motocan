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

#include "standby.hpp"

#if defined(CONFIG_STANDBY)

#include "../canbus/controller.hpp"
#include "../logging/logging.hpp"
#include "../racechrono/device.hpp"
#include "../sdlog/sdlog.hpp"
#include "../utils/timer.hpp"

#include <driver/rtc_io.h>
#include <esp_sleep.h>

namespace standby
{

namespace
{

/// survives deep sleep; zeroed only at power-on
RTC_DATA_ATTR uint32_t sleeps = 0U;

uint32_t frames_last = 0U;
uint32_t quiet_since_ms = 0U;

// When this start reached each step, in ms since the chip came out of reset -- which
// for a wake is the wake, since millis() counts from there and not from setup(). For
// the debug statistics: a woken board's boot lines are gone before a console attaches.
uint32_t setup_ms = 0U;
uint32_t ack_ms = 0U;
uint32_t sub_ms = 0U;
#if defined(DEBUG)
utils::timer stats_timer;
#endif

} // namespace

bool on_boot() noexcept
{
    setup_ms = millis();

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
    {
        return false;
    }

    // Both pads were handed to the RTC domain to sleep: TX held recessive, RX as the
    // wake source. They stay there across the wake until released, and a pad still
    // under hold ignores every level the CAN controller sets on it.
    rtc_gpio_hold_dis(CAN_TX_PIN);
    rtc_gpio_deinit(CAN_TX_PIN);
    rtc_gpio_deinit(CAN_RX_PIN);

    bootln("Standby: woken by the CAN bus, wake %lu, setup() at %lu ms",
           (unsigned long)sleeps, (unsigned long)setup_ms);
    return true;
}

void check() noexcept
{
    const uint32_t now = millis();
    const uint32_t frames = CANCTLR.counters().frames;

    if (ack_ms == 0U && CANCTLR.ack() == canbus::controller::ack_state::acknowledging)
    {
        ack_ms = now;
    }
    if (sub_ms == 0U && RCDEV.subscribed())
    {
        sub_ms = now;
    }
#if defined(DEBUG)
    if (stats_timer.elapsed(CONFIG_RC_STATS_TIMEOUT) > 0UL)
    {
        infoln("           Standby: %s %lu, setup() at %lu ms, acknowledging at %lu ms, phone at %lu ms",
               esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? "wake" : "power-on, sleeps",
               (unsigned long)sleeps, (unsigned long)setup_ms,
               (unsigned long)ack_ms, (unsigned long)sub_ms);
    }
#endif

    if (frames != frames_last || quiet_since_ms == 0U)
    {
        frames_last = frames;
        quiet_since_ms = now == 0U ? 1U : now;
        return;
    }

    if (now - quiet_since_ms < CONFIG_STANDBY_TIMEOUT_S * 1000U)
    {
        return;
    }

    bootln("Standby: no frame for %u s, sleeping", (unsigned)CONFIG_STANDBY_TIMEOUT_S);

    // What is in the card's ring goes out before the power does.
    SDLOG.stop();

    // TX recessive for as long as the chip sleeps: the digital pads lose their state
    // in deep sleep, and a transmit input left to float can hold the bus dominant.
    rtc_gpio_init(CAN_TX_PIN);
    rtc_gpio_set_direction(CAN_TX_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(CAN_TX_PIN, 1);
    rtc_gpio_hold_en(CAN_TX_PIN);

    // Wake on the first dominant bit. The transceiver drives RX either way while it is
    // powered; the pull-up only keeps a board whose transceiver is not from waking on
    // a floating pin.
    esp_sleep_enable_ext0_wakeup(CAN_RX_PIN, 0);
    rtc_gpio_pullup_en(CAN_RX_PIN);
    rtc_gpio_pulldown_dis(CAN_RX_PIN);

    sleeps++;
    Serial.flush();
    esp_deep_sleep_start();
}

} // namespace standby

#endif // CONFIG_STANDBY
