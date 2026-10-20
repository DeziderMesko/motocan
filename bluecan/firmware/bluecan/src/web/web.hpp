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
#include "../canbus/census.hpp"

#include <atomic>
#include <cstdint>

/// CONFIG_WIFI builds in the Wi-Fi mode (src/mode): an access point of its own, and a
/// web page on it with the bus's statistics and the microSD card's files to download.
/// The board runs it instead of Bluetooth, never beside it.
///
/// The page is at http://192.168.4.1/ on the network bluecan-XXXX, the last four hex
/// digits of the chip's MAC. The password is CONFIG_WIFI_PASSWORD, from src/web/secret.hpp,
/// which tools/reflash-bluecan.sh writes with a random one the first time and git
/// ignores.
#if defined(CONFIG_WIFI)

/// seconds without a request to the page before the board goes back to the normal mode.
/// An open page asks for the statistics every second, so this is the page closed, or a
/// phone left on the network with the browser shut
#ifndef CONFIG_WIFI_IDLE_S
#define CONFIG_WIFI_IDLE_S 300
#endif

/// the rear wheel speed, in hundredths of a km/h, that sends the board back to the
/// normal mode once it has lasted CONFIG_WIFI_MOVING_MS: the bike is being ridden.
/// Above what the rear has been seen to read on a stand with the clutch dragging,
/// 2.6 km/h, and with the front pushed, 2.7-4 km/h (analysis/r9-signals.md)
#ifndef CONFIG_WIFI_MOVING_KMH_X100
#define CONFIG_WIFI_MOVING_KMH_X100 1000
#endif
#ifndef CONFIG_WIFI_MOVING_MS
#define CONFIG_WIFI_MOVING_MS 1000
#endif

#ifndef CONFIG_WIFI_CHANNEL
#define CONFIG_WIFI_CHANNEL 1
#endif

#endif // CONFIG_WIFI

namespace net
{

class web final
{
    CPP_NOCOPY(web);
    CPP_NOMOVE(web);

public:
    static web& get() noexcept;

    ~web() noexcept = default;

#if defined(CONFIG_WIFI)
    /**
     * Bring up the access point and start the web server and the frame census, each
     * on a task of its own on core 0. From setup(), in the Wi-Fi mode, in place of
     * Bluetooth and the card's log.
     */
    bool start() noexcept;

    /**
     * From loop(): restart into the normal mode when the page asks for it, when nobody
     * has asked for anything for CONFIG_WIFI_IDLE_S, or when the bike is moving. Does
     * not return then.
     */
    void check() noexcept;

    /**
     * devices on the access point now. For the status light; any task.
     */
    uint32_t stations() const noexcept { return _stations.load(std::memory_order_relaxed); }
#else
    bool start() noexcept { return false; }
    void check() noexcept {}
    uint32_t stations() const noexcept { return 0U; }
#endif

private:
    explicit web() noexcept;

#if defined(CONFIG_WIFI)
    static void server_task(void*);
    static void census_task(void*);
    void serve() noexcept;
    void count() noexcept;
#if defined(DEBUG)
    void stats_line(uint32_t now) noexcept;
#endif

    void page() noexcept;
    void stats() noexcept;
    void files() noexcept;
    void file() noexcept;
    void leave() noexcept;

    /// millis() of the last request, or of the start; a download in progress keeps
    /// moving it
    std::atomic<uint32_t> _last_ms;
    /// millis() when the page asked to leave, or 0
    std::atomic<uint32_t> _leave_ms;
    std::atomic<uint32_t> _stations;

    /// the ids on the bus: written by the census task, read by the server task, both on
    /// core 0
    canbus::census _census;

    // --- loop() only
    uint32_t _moving_since_ms;
#endif
};

} // namespace net

extern net::web& WEB;
