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
#include "../status/status_light.hpp"
#include "../utils/timer.hpp"

#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <hal/usb_serial_jtag_ll.h>

namespace standby
{

namespace
{

// Kept in RTC memory through a restart, which is how check() asks the next start to
// sleep. Not initialised by anything: after a power-on it holds whatever the memory
// came up with, so it counts only once its magic number is in place.
constexpr uint32_t kept_magic = 0x42435342U;   // "BCSB"
constexpr uint32_t sleep_asked = 0x534C5050U;  // "SLPP"
struct kept_t
{
    uint32_t magic;
    uint32_t sleep;    //!< sleep_asked when the next start is to sleep
    uint32_t woke;     //!< what woke the chip, for the start after the wake; 0 otherwise
    uint32_t sleeps;   //!< sleeps since power-on
};
RTC_NOINIT_ATTR kept_t kept;

start this_start = start::power_on;
uint32_t frames_last = 0U;
uint32_t quiet_since_ms = 0U;

// When this start reached each step, in ms of millis(): for the debug statistics, since
// a woken board's boot lines are gone before a console attaches.
uint32_t ack_ms = 0U;
uint32_t sub_ms = 0U;
#if defined(DEBUG)
utils::timer stats_timer;
#endif

#if defined(BUTTON_PIN)
constexpr gpio_int_type_t button_wake = BUTTON_PRESSED ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL;
#endif

const char* name(start s) noexcept
{
    switch (s)
    {
    case start::bus:    return "woken by the bus";
    case start::button: return "woken by the button";
    default:            return "power-on or reset";
    }
}

/// Light sleep until CAN RX goes dominant or the button is pressed, with nothing else
/// running yet. Returns what woke the chip.
start sleep() noexcept
{
    // TX recessive for as long as the chip sleeps, held so that nothing can change it.
    // A plain GPIO, level before routing, as controller.cpp does it: the restart into
    // this sleep need not have taken the pad off the controller's output.
    gpio_set_level(CAN_TX_PIN, 1);
    esp_rom_gpio_pad_select_gpio(CAN_TX_PIN);
    esp_rom_gpio_connect_out_signal(CAN_TX_PIN, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(CAN_TX_PIN, GPIO_MODE_OUTPUT);
    gpio_hold_en(CAN_TX_PIN);

    // Wake on the first dominant bit. The transceiver drives RX either way while it is
    // powered; the pull-up only keeps a board whose transceiver is not from waking on
    // a floating pin.
    gpio_set_direction(CAN_RX_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CAN_RX_PIN, GPIO_PULLUP_ONLY);
    gpio_wakeup_enable(CAN_RX_PIN, GPIO_INTR_LOW_LEVEL);

#if defined(BUTTON_PIN)
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, BUTTON_PRESSED ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
    gpio_wakeup_enable(BUTTON_PIN, button_wake);
#endif

    esp_sleep_enable_gpio_wakeup();
    Serial.flush();

    // Off the USB bus for the sleep and until the restart after it; see on_boot(). The
    // pull-up on D+ goes too, or the computer goes on seeing a device that is plugged in
    // and does not answer: Windows then reports a failed USB device at every sleep.
    const usb_serial_jtag_pull_override_vals_t no_pulls = {};
    usb_serial_jtag_ll_phy_enable_pull_override(&no_pulls);
    usb_serial_jtag_ll_phy_enable_pad(false);

    // A sleep refused, because a wake came while it was being prepared, is a wake all
    // the same. Nothing is said on the console: the USB port is off until the restart.
    esp_light_sleep_start();

    // Which pin it was, read while a press that woke the chip is still being held: a
    // light-sleep wake is a millisecond or so.
    start woke = start::bus;
#if defined(BUTTON_PIN)
    if (gpio_get_level(BUTTON_PIN) == BUTTON_PRESSED)
    {
        woke = start::button;
    }
    gpio_wakeup_disable(BUTTON_PIN);
#endif
    gpio_wakeup_disable(CAN_RX_PIN);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    // TX stays held: on_boot() releases it after the restart that follows.

    return woke;
}

} // namespace

start on_boot() noexcept
{
    // TX is under a hold after a wake, kept by sleep() through the restart, and may be
    // after a reset that cut a sleep short, or in the RTC domain where an earlier
    // build's deep sleep left it. A held TX pad would keep every acknowledgement off the
    // bus, so it is released on every start, and both pads given back to the GPIO
    // matrix. Harmless when there is nothing to release.
    gpio_hold_dis(CAN_TX_PIN);
    rtc_gpio_deinit(CAN_TX_PIN);
    rtc_gpio_deinit(CAN_RX_PIN);

    if (kept.magic != kept_magic)
    {
        kept = {kept_magic, 0U, 0U, 0U};
    }

    // The start after a wake.
    if (kept.woke != 0U)
    {
        this_start = static_cast<start>(kept.woke);
        kept.woke = 0U;
        bootln("Standby: %s, sleep %lu", name(this_start), (unsigned long)kept.sleeps);
        return this_start;
    }

    if (kept.sleep != sleep_asked)
    {
        return start::power_on;
    }

    // Cleared before sleeping, so that a reset while asleep starts the board normally.
    kept.sleep = 0U;
    kept.sleeps++;
    kept.woke = static_cast<uint32_t>(sleep());

    // And woken, it restarts once more, through a deep sleep of a millisecond, so that
    // the start after a wake is the start after a deep sleep, as it was before the button
    // could wake the board. The chip's own USB port is why. Continuing after the light
    // sleep, the port came back unusable: the board woke and acknowledged, but no
    // console returned, and Windows reported a failed USB device. A system reset
    // straight after the wake, with sleep() keeping the port off the bus until it,
    // worked twice and then failed the same way on 2026-10-20, Windows listing
    // "Device Descriptor Request Failed". A wake from deep sleep never did, over patch
    // 35's runs. RTC memory, with what woke the chip, and the hold on TX last through it.
    esp_sleep_enable_timer_wakeup(1000U);
    esp_deep_sleep_start();
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
        infoln("           Standby: %s, %lu sleeps, acknowledging at %lu ms, phone at %lu ms",
               name(this_start), (unsigned long)kept.sleeps,
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

    // Dark: the pixel keeps its last colour for as long as it has power, and on the
    // Lite that is the whole time the bike stands.
    LIGHT.stop();

    // The next start sleeps before it brings anything up: no Bluetooth, no CAN
    // controller and no task to stop here first.
    kept.sleep = sleep_asked;
    Serial.flush();
    esp_restart();
}

} // namespace standby

#endif // CONFIG_STANDBY
