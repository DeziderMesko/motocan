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

#include "../canbus/controller.hpp"
#include "../canbus/decoder.hpp"
#include "../canbus/r9.hpp"
#include "../logging/logging.hpp"
#include "../mode/mode.hpp"
#include "../power/power.hpp"
#include "../racechrono/device.hpp"
#include "../status/status_light.hpp"

#include "display.hpp"

#if defined(CONFIG_DISPLAY_TFT)

#include <cstdarg>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <hal/twai_ll.h>

namespace
{

// The panel has exactly one owner: ui::display's task. It is a file-static rather
// than a member so that no header outside this file pulls in Adafruit_GFX, and so
// that nothing else can reach it.
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// 240x135 in landscape, 6x8 pixels per character at size 1: 40 columns, and enough
// rows for a header, nine lines of content and a footer.
constexpr int screen_w = 240;
constexpr int screen_h = 135;
constexpr int cols = 40;
// header is drawn at text size 2, so half as many columns fit across the glass
constexpr int head_cols = 20;
constexpr int content_y = 20;
constexpr int row_h = 11;
constexpr int rows = 9;
constexpr int footer_y = 122;

/// how often the BUS page is repainted. Five hertz is faster than anyone reads and
/// nearly free: with nothing changing a pass costs ~420 us on the panel as it stands
/// (the ~280 us this used to say was measured on a smaller page), and each character
/// that does change costs ~650 us on top.
constexpr uint32_t refresh_bus_ms = 200;
/// how often the census and frame pages are repainted. Deliberately slower: nearly
/// every character on them changes every time, so at 5 Hz the LAST page alone would
/// spend half of core 0 on SPI to tell a human something they cannot read that fast.
constexpr uint32_t refresh_slow_ms = 1000;
/// how often the buttons are sampled. The refresh interval is far too coarse for a
/// button -- a quick press between two repaints would simply not exist.
constexpr uint32_t poll_ms = 25;

/// how long the panel stays lit after boot before it says it is going to sleep,
/// and how long it says so before it goes dark. The rider's numbers, 2026-10-17.
constexpr uint32_t doze_lit_ms = 10000;
constexpr uint32_t doze_zzz_ms = 5000;

/// bytes, not words: ESP-IDF's StackType_t is a byte. This panel runs in 4 KB with
/// **1824 B** still free, measured on the board 2026-10-13 after every page had been
/// visited -- 1912 B before the LIVE page, and the ~2.2 KB this once quoted was the
/// bench sketch's simpler page. Current values live in motocan/bluecan/history/numbers.md.
constexpr uint32_t stack_size = 4096;
StaticTask_t task_buffer;
StackType_t task_stack[stack_size];

/// D0 is the BOOT strapping pin: pulled up, and reads LOW when pressed. D1 and D2
/// are pulled down and read HIGH. They do not share a polarity -- see
/// motocan/bluecan/hardware.md.
constexpr int btn_light = 0;
constexpr int btn_page = 1;
constexpr int btn_hold = 2;

const char* bus_state(uint32_t status)
{
    if (status & TWAI_LL_STATUS_BS) return "BUS-OFF";
    if (status & TWAI_LL_STATUS_ES) return "ERRORS";
    return "RUN";
}

/// the "bluecan" wordmark's colour. Darker than ST77XX_BLUE (0x001F), which at full
/// saturation reads closer to electric blue than the calmer navy the name is meant
/// to evoke sitting next to a white or yellow status word.
constexpr uint16_t color_blue_dim = 0x1A96;

/// "we do not know", as a colour. Grey rather than yellow: a missing fuel gauge is
/// not a warning about the supply, it is the absence of a reading, and a yellow one
/// would read as a low battery from across a garage.
constexpr uint16_t color_grey_dim = 0x8410;

/// where the supply readout stops being green, and where it turns red. A 1S LiPo is
/// full at 4.2 V, spends most of a session between 3.9 and 3.7, and is close to done
/// at 3.5. On USB with no battery fitted the same rail reads the charger's own
/// output, comfortably above both, so the green band covers "fed by the bike" as
/// well as "battery with plenty left" -- which is the point of showing it at all.
constexpr uint16_t supply_mv_warn = 3700;
constexpr uint16_t supply_mv_low = 3500;

} // namespace

namespace ui
{

display& display::get() noexcept
{
    static display instance;
    return instance;
}

display::display() noexcept
    : _ids{}
    , _hits{}
    , _hits_prev{}
    , _rates{}
    , _id_used(0)
    , _id_overflow(0U)
    , _ring{}
    , _ring_pos(0U)
    , _handle(nullptr)
    , _page(page_live_id)
    , _light(light_both)
    , _hold(false)
    , _doze(doze_awake)
    , _doze_ms(0U)
    , _draw_us(0U)
    , _draw_us_max(0U)
    , _draw_us_shown(0U)
    , _dirty(0U)
    , _min_heap(0xFFFFFFFFU)
    , _rx_prev(0U)
    , _fwd_prev(0U)
    , _ble_prev(0U)
    , _rx_rate(0U)
    , _fwd_rate(0U)
    , _ble_rate(0U)
    , _rate_ms(0U)
    , _stats_timer{}
    , _cache{}
    , _cache_color{}
    , _cache_col{}
    , _slot_y(-1)
    , _slot_n(0)
{
}

bool display::start() noexcept
{
    bootln("Display starting...");

    _handle = xTaskCreateStaticPinnedToCore(
        task,
        "display",
        stack_size,
        this,
        tskIDLE_PRIORITY + 1,
        task_stack,
        &task_buffer,
        0
    );

    if (_handle == nullptr)
    {
        errorln("ERROR: display task creation failed!");
        return false;
    }

    bootln("Display started!");

    return true;
}

void display::note(canbus::frame const& f) noexcept
{
    uint32_t id = f.id;
    // Relaxed: this task is the only writer of either index.
    int used = _id_used.load(std::memory_order_relaxed);
    bool known = false;

    for (int i = 0; i < used; i++)
    {
        if (_ids[i] == id)
        {
            _hits[i]++;
            known = true;
            break;
        }
    }

    if (!known)
    {
        if (used < id_slots)
        {
            // fill the slot before publishing it, so the display task cannot read a
            // half-written entry: _id_used is what makes the slot visible, and the
            // release is what stops the four stores above being sunk past it
            _ids[used] = id;
            _hits[used] = 1;
            _hits_prev[used] = 0;
            _rates[used] = 0;
            _id_used.store(used + 1, std::memory_order_release);
        }
        else
        {
            _id_overflow++;
        }
    }

    uint32_t pos = _ring_pos.load(std::memory_order_relaxed);
    _ring[pos % ring_slots] = f;
    _ring_pos.store(pos + 1, std::memory_order_release);
}

void display::task(void* arg)
{
    static_cast<display*>(arg)->run();
}

void display::run() noexcept
{
    // The panel is initialised here rather than in start(), which runs on core 1.
    // One task owns the SPI bus to it, and this is that task.
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);

    pinMode(btn_light, INPUT_PULLUP);
    pinMode(btn_page, INPUT_PULLDOWN);
    pinMode(btn_hold, INPUT_PULLDOWN);

    tft.init(screen_h, screen_w);
    // Adafruit_ST7789 defaults conservatively. The panel is good for 40 MHz and a
    // repaint is almost entirely SPI clocking, so this halves it for nothing. It is
    // not the fix -- change detection is -- just cheaper work.
    tft.setSPISpeed(40000000);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);
    wipe();

    // The fuel gauge, for the same reason the panel is initialised here and not in
    // start(): it hangs off TFT_I2C_POWER, which this task has just driven high, and
    // one task owning every peripheral on core 0 is the rule this module exists to
    // keep. Sampled from sample_rates() on the 1 Hz tick thereafter.
    PWR.begin();

    _rate_ms = millis();
    _doze_ms = _rate_ms;

    uint32_t last_paint = 0;

    for (;;)
    {
        poll_buttons();

        uint32_t now = millis();

        if (_doze == doze_awake && now - _doze_ms >= doze_lit_ms)
        {
            // The whole glass, header and footer too: the message is the only thing
            // on it. Nine characters at size 3 are 162 x 24 pixels.
            tft.fillScreen(ST77XX_BLACK);
            tft.setTextSize(3);
            tft.setTextColor(color_blue_dim, ST77XX_BLACK);
            tft.setCursor((screen_w - 9 * 18) / 2, (screen_h - 24) / 2);
            tft.print("Zzzzz....");
            _doze = doze_zzz;
            _doze_ms = now;
        }
        else if (_doze == doze_zzz && now - _doze_ms >= doze_zzz_ms)
        {
            // LED only, not dark: the light is what stays useful on the bike.
            _light = light_pixel;
            digitalWrite(TFT_BACKLITE, LOW);
            _doze = doze_asleep;
        }

        // Sampled even while the panel is dark or held: the rates are a measurement
        // of the bus, not of the display, and a held screen that resumed with a
        // minute-long average would be reporting the wrong thing.
        if (now - _rate_ms >= 1000)
        {
            sample_rates();
        }

        uint32_t interval = (_page == page_live_id || _page == page_bus_id) ? refresh_bus_ms
                                                                            : refresh_slow_ms;

        if (_light == light_both && !_hold && _doze != doze_zzz && (now - last_paint) >= interval)
        {
            last_paint = now;
            uint32_t t0 = micros();
            _dirty = 0;
            refresh();
            _draw_us = micros() - t0;
            if (_draw_us > _draw_us_max)
            {
                _draw_us_max = _draw_us;
            }
        }

        stats();

        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

#if defined(DEBUG)
void display::stats() noexcept
{
    if (logging::logger::get().level() >= logging::log_level::info)
    {
        if (_stats_timer.elapsed(CONFIG_RC_STATS_TIMEOUT) > 0UL)
        {
            const char* name = _page == page_live_id ? "LIVE"
                             : _page == page_ids_id  ? "IDS"
                             : _page == page_last_id ? "LAST"
                                                     : "BUS";

            const char* lightness = _light == light_both ? ""
                                  : _light == light_pixel ? " (panel off)"
                                                          : " (dark)";

            infoln("      Display page: %s%s%s", name, lightness,
                   _hold ? " (held)" : "");
            infoln("      Display draw: %lu us, worst %lu us, %lu chars",
                   (unsigned long)_draw_us, (unsigned long)_draw_us_max,
                   (unsigned long)_dirty);
            infoln("     Display stack: %lu bytes free",
                   (unsigned long)(_handle ? uxTaskGetStackHighWaterMark(_handle) : 0));
            infoln("        Census ids: %d, %lu over",
                   _id_used.load(std::memory_order_acquire),
                   (unsigned long)_id_overflow);

#if defined(CONFIG_CANBUS_DECODER_YAMAHAR9)
            // What the decoder is holding for the panel, decoded, with its age. The
            // age is the part a shell needs: a value that stopped arriving looks
            // exactly like one that stopped changing.
            {
                using namespace canbus;
                uint8_t d[8];
                uint32_t age;

                if (CANDEC.latest(r9::id_engine, d, age))
                {
                    infoln("          R9 0x27C: rpm %lu, gear %u, TCS %u, PWR %u (%lu ms old)",
                           (unsigned long)r9::rpm(d), (unsigned)r9::gear(d), (unsigned)r9::tcs(d),
                           (unsigned)r9::pwr(d), (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_rear, d, age))
                {
                    infoln("          R9 0x756: rear %u hundredths km/h (%lu ms old)",
                           (unsigned)r9::rear_kmh_x100(d), (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_front, d, age))
                {
                    infoln("          R9 0x765: front %u hundredths km/h (%lu ms old)",
                           (unsigned)r9::front_kmh_x100(d), (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_display, d, age))
                {
                    infoln("          R9 0x76E: %s display (%lu ms old)",
                           r9::track_display(d) ? "Track" : "normal", (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_temps, d, age))
                {
                    infoln("          R9 0x512: water %d, air %d tenths C (%lu ms old)",
                           (int)r9::water_tenths(d), (int)r9::air_tenths(d), (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_throttle, d, age))
                {
                    infoln("          R9 0x758: grip %u, valve %u tenths %% (%lu ms old)",
                           (unsigned)r9::grip_tenths(d), (unsigned)r9::valve_tenths(d), (unsigned long)age);
                }
                if (CANDEC.latest(r9::id_valve_cmd, d, age))
                {
                    infoln("          R9 0x759: valve commanded %u tenths %% (%lu ms old)",
                           (unsigned)r9::valve_cmd_tenths(d), (unsigned long)age);
                }
            }
#endif

            // The supply, on the console for the same reason the repaint cost is:
            // the panel is the one instrument a shell cannot read, and this is the
            // number that explains a board that rebooted while nobody was watching.
            if (PWR.present())
            {
                const unsigned mv = PWR.millivolts();
                const int rate = PWR.rate_tenths();
                // Raw, both halves, whatever cell() thinks: the console is the
                // instrument the display's own rule is checked against, and a
                // percentage suppressed on the glass is exactly the number worth
                // reading here.
                infoln("            Supply: %u.%03u V, %u.%u%%, %s%d.%d%%/hr%s",
                       mv / 1000U, mv % 1000U,
                       (unsigned)(PWR.soc_tenths() / 10U),
                       (unsigned)(PWR.soc_tenths() % 10U),
                       rate < 0 ? "-" : "",
                       (rate < 0 ? -rate : rate) / 10,
                       (rate < 0 ? -rate : rate) % 10,
                       PWR.cell() ? "" : "  (no CONFIG_CELL_FITTED: charge and rate are the charger's rail)");
            }
            else
            {
                infoln("            Supply: no fuel gauge answering");
            }
        }
    }
}
#endif

void display::poll_buttons() noexcept
{
    bool light = digitalRead(btn_light) == LOW;
    bool next = digitalRead(btn_page) == HIGH;
    bool hold = digitalRead(btn_hold) == HIGH;

    static bool p_light = false;
    static bool p_next = false;
    static bool p_hold = false;

    if (light && !p_light && _doze != doze_over)
    {
        // The first press after boot lights the panel, whatever the doze has got to.
        // The message and the dark glass have to go, and with them every cached
        // character, header and footer included, or the next pass would decide
        // nothing had changed.
        if (_doze != doze_awake)
        {
            tft.fillScreen(ST77XX_BLACK);
            wipe();
        }
        _doze = doze_over;
        _light = light_both;
        digitalWrite(TFT_BACKLITE, HIGH);
        LIGHT.set_dark(false);
    }
    else if (light && !p_light)
    {
        // Three positions: panel and pixel, pixel alone, dark. Backlight off also
        // stops the repaint, which makes the middle position the cheapest experiment
        // on the board -- everything else keeps running, and the pixel costs ~30 us
        // an update, so any difference in the frame counters afterwards is the
        // panel's cost and nothing else. The third is for riding at night.
        _light = (_light + 1U) % light_count;
        digitalWrite(TFT_BACKLITE, _light == light_both ? HIGH : LOW);
        LIGHT.set_dark(_light == light_dark);
    }

    // Not while the message is up: wipe() would take half of it off the glass.
    if (next && !p_next && _doze != doze_zzz)
    {
        _page = (_page + 1) % page_count;
        _hold = false;
        wipe();
    }

    if (hold && !p_hold && _doze != doze_zzz)
    {
        _hold = !_hold;
        // Freezes the glass, not the counting: the ISR, the queue and the census all
        // carry on, so a held screen is a snapshot rather than a pause.
        if (_hold)
        {
            refresh();
        }
    }

    p_light = light;
    p_next = next;
    p_hold = hold;
}

void display::sample_rates() noexcept
{
    uint32_t now = millis();
    uint32_t elapsed = now - _rate_ms;
    _rate_ms = now;

    if (elapsed == 0)
    {
        return;
    }

    canbus::controller::counters_t c = CANCTLR.counters();
    uint32_t ble = RCDEV.frames();

    _rx_rate = ((c.frames - _rx_prev) * 1000UL) / elapsed;
    _fwd_rate = ((c.forwarded - _fwd_prev) * 1000UL) / elapsed;
    _ble_rate = ((ble - _ble_prev) * 1000UL) / elapsed;
    _rx_prev = c.frames;
    _fwd_prev = c.forwarded;
    _ble_prev = ble;

    int used = _id_used.load(std::memory_order_acquire);
    for (int i = 0; i < used; i++)
    {
        uint32_t hits = _hits[i];
        _rates[i] = static_cast<uint16_t>(((hits - _hits_prev[i]) * 1000UL) / elapsed);
        _hits_prev[i] = hits;
    }

    _draw_us_shown = _draw_us;

    // Three 2-byte I2C reads, ~150 us at 400 kHz, on the tick that is already the
    // slow one. The cell moves over minutes, so anything faster would be spending
    // core 0 to watch a number that has not changed.
    PWR.sample();

    uint32_t heap = ESP.getFreeHeap();
    if (heap < _min_heap)
    {
        _min_heap = heap;
    }
}

void display::field(int y, uint8_t size, uint16_t color, int slot, const char* text, int col) noexcept
{
    // Every drawable line comes through here, so this is the one place a slot can be
    // checked at all. slot_for() already bounds the seg() region row by row; this
    // catches a hand-assigned slot that has drifted out of range, which would memcpy
    // off the end of _cache into _cache_color and _cache_col and corrupt the change
    // detection rather than failing where anyone would look. An abort on the first
    // repaint is the cheaper way to find that out.
    RCASSERT(slot >= 0 && slot < cache_slots);

    char* cache = _cache[slot];

    size_t len = strlen(text);
    if (len > cache_len - 1)
    {
        len = cache_len - 1;
    }

    // Character-level diffing, not line-level. A character costs ~600 us to draw:
    // Adafruit_GFX renders text a pixel at a time, so a 40-column line is 24 ms --
    // more than a tenth of the refresh interval, spent on core 0 next to the BLE
    // stack. Line-level detection was enough on the bench sketch and is not enough
    // here, because the line that reports the repaint cost changes on every pass and
    // would then be paying 24 ms to report that it had paid 24 ms.
    //
    // A length change, a colour change or a column change forces the whole slot to
    // redraw. Length, because a differently-shaped line (or the sentinel wipe() leaves
    // behind) cannot be diffed character-by-character. Colour, because two draws can
    // share every character at some position while wanting a different hue there --
    // e.g. the header's "bluecan " prefix is identical whether the state word after it
    // is "SUB" or "connected" -- and a plain character diff would then skip repainting
    // characters that are still showing the previous call's colour, leaving the line
    // part one hue and part the other. Column, because seg()/seg_fill() chain several
    // slots left to right on one row: if an earlier slot's text changes width (the
    // census's _min_heap starts at a sentinel and shrinks hugely on the first sample,
    // for instance), every slot after it is asked to draw at a new column even though
    // its own text did not change -- and a plain diff would then skip it, leaving that
    // slot's glyphs stranded at the old column, overlapping whatever is now drawn
    // before it.
    bool whole = strlen(cache) != len || _cache_color[slot] != color || _cache_col[slot] != col;

    for (size_t i = 0; i < len; )
    {
        if (!whole && cache[i] == text[i])
        {
            i++;
            continue;
        }

        size_t j = i;
        while (j < len && (whole || cache[j] != text[j]))
        {
            j++;
        }

        tft.setTextSize(size);
        tft.setTextColor(color, ST77XX_BLACK);
        tft.setCursor(static_cast<int16_t>((col + i) * 6 * size), y);
        tft.write(reinterpret_cast<const uint8_t*>(text) + i, j - i);
        _dirty += j - i;

        i = j;
    }

    memcpy(cache, text, len);
    cache[len] = '\0';
    _cache_color[slot] = color;
    _cache_col[slot] = col;
}

void display::row(int idx, uint16_t color, const char* fmt, ...) noexcept
{
    char buf[cache_len];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        n = 0;
    }

    // Pad to the full line. Characters are drawn over their own background, so a
    // shorter line would leave the tail of the longer one it replaced on the glass.
    for (int i = (n < cols ? n : cols); i < cols; i++)
    {
        buf[i] = ' ';
    }
    buf[cols] = '\0';

    field(content_y + idx * row_h, 1, color, idx + 1, buf);
}

int display::slot_for(int y) noexcept
{
    // A row owns segs_per_row consecutive slots, indexed by the order its segments are
    // drawn in. The alternative -- one counter running across the whole page -- meant a
    // row that grew or lost a segment renumbered every row after it, and the change
    // detection then compared each of those rows against a neighbour's text. Nothing
    // reports that: the counters stay healthy and the glass goes quietly wrong.
    if (y != _slot_y)
    {
        _slot_y = y;
        _slot_n = 0;
    }

    int idx = (y - content_y) / row_h;

    // Only content rows reach here; the header and the footer have hand-assigned slots.
    // Both asserts are the shape-change alarm this scheme exists to provide: one for a
    // segment drawn outside the content area, one for a row that has outgrown its range.
    RCASSERT(idx >= 0 && idx < ui::display::cache_rows);
    RCASSERT(_slot_n < ui::display::segs_per_row);

    return slot_row_base + idx * segs_per_row + _slot_n++;
}

void display::seg(int y, int& col, uint16_t color, const char* fmt, ...) noexcept
{
    char buf[cache_len];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    field(y, 1, color, slot_for(y), buf, col);
    col += static_cast<int>(strlen(buf));
}

void display::seg_fill(int y, int& col, uint16_t color, const char* fmt, ...) noexcept
{
    char buf[cache_len];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        n = 0;
    }

    int avail = cols - col;
    if (avail < 0)
    {
        avail = 0;
    }
    if (avail > static_cast<int>(sizeof(buf)) - 1)
    {
        avail = static_cast<int>(sizeof(buf)) - 1;
    }

    // Same reasoning as row()'s padding: this is the last segment on the row, so it
    // has to erase whatever a longer previous value left behind.
    for (int i = (n < avail ? n : avail); i < avail; i++)
    {
        buf[i] = ' ';
    }
    buf[avail] = '\0';

    field(y, 1, color, slot_for(y), buf, col);
    col += avail;
}

void display::cell(int y, uint8_t size, int col, uint16_t color, const char* fmt, ...) noexcept
{
    char buf[cache_len];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    field(y, size, color, slot_for(y), buf, col);
}

void display::wipe() noexcept
{
    tft.fillRect(0, content_y - 2, screen_w, screen_h - (content_y - 2), ST77XX_BLACK);

    // Blank the caches too, or the next pass compares against text that is no longer
    // on the screen, decides nothing changed, and leaves the area empty. The colour
    // cache is reset alongside it for the same reason field() checks it at all: a
    // sentinel colour no real call ever uses forces the first post-wipe draw to be
    // whole, regardless of what text or hue happened to be cached there before.
    for (int i = 1; i < cache_slots; i++)
    {
        _cache[i][0] = '\x01';
        _cache[i][1] = '\0';
        _cache_color[i] = ST77XX_BLACK;
        _cache_col[i] = -1;
    }
}

void display::refresh() noexcept
{
    // display.cpp owns the row geometry and display.hpp owns the cache map; they have to
    // agree, and this is the cheapest place to say so.
    static_assert(rows <= cache_rows, "a content row has no cache slots of its own");

    _slot_y = -1;
    _slot_n = 0;

    // Header: the one line that is the same on every page, because it answers the
    // question asked most often -- is the phone actually getting anything.
    //
    // Three states, not two. A phone that is connected but has not subscribed to the
    // frame characteristic looks exactly like a working link on the phone, in the
    // advertising name, and in every other counter here, and receives nothing. It is
    // the failure worth spending a colour on.
    //
    // Drawn as two segments rather than one string: "bluecan" is the wordmark and
    // stays a fixed navy regardless of state, while the state word after it carries
    // the status colour. One field() call for the whole line would work too, but only
    // because field() now forces a whole redraw on any colour change -- see the
    // comment there. Splitting it keeps the wordmark's slot static across every
    // repaint, which is one fewer redraw of glass that never changes.
    bool up = RCDEV.connected();
    bool sub = RCDEV.subscribed();
    const char* state = !up ? "advertising" : sub ? "SUB" : "connected";
    uint16_t state_c = !up ? ST77XX_YELLOW : sub ? ST77XX_GREEN : ST77XX_RED;
    // In the Wi-Fi mode there is no Bluetooth to report on, and the mode is the news.
    if (mode::current() == mode::run::wifi)
    {
        state = "Wi-Fi";
        state_c = ST77XX_MAGENTA;
    }

    // Connection interval, in tenths of a millisecond: the link reports it in 1.25 ms
    // units, and it is the ceiling on everything downstream, since the radio only
    // speaks at connection events. 7.5 ms is as fast as BLE goes; 30 ms is a phone
    // that will not keep up with the bus whatever the firmware does.
    unsigned long tenths = RCDEV.interval() * 125UL / 10UL;
    char state_buf[cache_len];
    int n;
    // Only once subscribed: "connected 7.5ms" plus "bluecan " no longer fits the
    // header's 13-column budget for the state word, and SUB is the state this number
    // actually matters for -- it is the ceiling on the data RaceChrono is now getting.
    if (sub && tenths != 0UL)
    {
        n = snprintf(state_buf, sizeof(state_buf), " %s %lu.%lums",
                     state, tenths / 10UL, tenths % 10UL);
    }
    else
    {
        n = snprintf(state_buf, sizeof(state_buf), " %s", state);
    }

    if (n < 0)
    {
        n = 0;
    }

    // "bluecan" is 7 columns wide at text size 2; the state segment starts right
    // after it and is padded out to the header's full head_cols width so a shorter
    // state word cannot leave the tail of a longer one on the glass.
    constexpr int wordmark_cols = 7;
    for (; n < head_cols - wordmark_cols && n < static_cast<int>(sizeof(state_buf)) - 1; n++)
    {
        state_buf[n] = ' ';
    }
    state_buf[n] = '\0';

    field(0, 2, color_blue_dim, slot_header_base, "bluecan", 0);
    field(0, 2, state_c, slot_header_base + 1, state_buf, wordmark_cols);

    switch (_page)
    {
    case page_live_id:
        page_live();
        break;
    case page_ids_id:
        page_ids();
        break;
    case page_last_id:
        page_last();
        break;
    default:
        page_bus();
        break;
    }

    char foot[cache_len];
    const char* name = _page == page_live_id ? "LIVE"
                     : _page == page_ids_id  ? "IDS"
                     : _page == page_last_id ? "LAST"
                                             : "BUS";
    snprintf(foot, sizeof(foot), "%-4s %-9s D0 lit D1 page D2 hold",
             name, _hold ? "**HOLD**" : "");
    field(footer_y, 1, _hold ? ST77XX_YELLOW : ST77XX_CYAN, cache_slots - 1, foot);
}

void display::page_bus() noexcept
{
    canbus::controller::counters_t c = CANCTLR.counters();
    // Nothing is read off the peripheral until install() has ungated its clock: this
    // task starts several seconds before that and would otherwise be sampling a
    // controller still held in reset. Zero is the honest reading meanwhile -- both
    // error counters are frozen there in listen-only mode anyway.
    bool running = CANCTLR.running();
    uint32_t status = running ? CANCTLR.status() : 0U;
    const char* state = running ? bus_state(status) : "DOWN";
    uint16_t state_color = !running || (status & TWAI_LL_STATUS_BS) ? ST77XX_RED
                         : (status & TWAI_LL_STATUS_ES)            ? ST77XX_YELLOW
                                                                   : ST77XX_GREEN;

    // Every row below is a sequence of seg()/seg_fill() calls rather than one row()
    // call: labels (rx, fwd, flt, ...) are always yellow, values are white unless
    // they are the one number on the row worth a warning colour, and the last call on
    // each row is seg_fill() so a value that shrinks cannot leave a longer previous
    // one's tail on the glass. See the slot_dynamic_base comment in display.hpp for
    // why none of these calls need an explicit slot number.
    //
    // Each row's leading label is printed "%-6s": 6 is one past "queue", the longest
    // of them, so every row's first value starts at the same column regardless of its
    // own label's length. Without this, rx/fwd (2- and 3-letter labels, a %9lu field)
    // and ble (a 3-letter label that used a narrower %8lu field) put their leading
    // digit one column apart, which is what made the BLE row's value look shifted.
    constexpr int label_w = 6;

    // The supply, at the one end of a row with room for it. Every other row on this
    // page runs to the edge, and the footer's three spare columns are not twelve.
    // It belongs here anyway: the rest of this row is "what state is the hardware
    // in", and so is this.
    //
    // What the gauge reads is the BAT rail -- the cell if one is fitted, the
    // charger's output if not -- and never the 5 V the bike's USB outlet supplies;
    // this board brings VBUS out to a pad and to nothing else. The trailing
    // character is what covers that: '+' is a cell being charged, so the outlet is
    // live, and '-' is the device running on its own battery, which is what the
    // ignition going off looks like from in here. See power::gauge.
    char supply[16];
    uint16_t supply_color;

    if (PWR.present())
    {
        const unsigned mv = PWR.millivolts();

        // Exactly twelve columns wide either way -- "       4.10V" with no cell
        // fitted, " 4.06V  82%+" with one. Nothing here may grow, because the state
        // word to its left is padded to a fixed width precisely so this lands on the
        // same column every pass.
        //
        // The voltage is unconditional and the percentage is not, because with an
        // empty battery jack only one of them is about anything. The gauge cannot
        // tell whether a cell is fitted -- on the bench it reports 100.0% and
        // discharging at 22%/hr for a rail that is not moving -- so it is not asked
        // to. See CONFIG_CELL_FITTED, and power::gauge for the two runtime tests that
        // were tried against the board and did not survive it.
        if (PWR.cell())
        {
            snprintf(supply, sizeof(supply), " %u.%02uV%4u%%%c",
                     mv / 1000U, (mv % 1000U) / 10U,
                     static_cast<unsigned>(PWR.soc_tenths() / 10U), PWR.flow());
        }
        else
        {
            // Right-aligned, so it ends on column 39 with the values on the rows
            // below it rather than floating in the middle of the row.
            char volts[8];
            snprintf(volts, sizeof(volts), "%u.%02uV", mv / 1000U, (mv % 1000U) / 10U);
            snprintf(supply, sizeof(supply), "%12s", volts);
        }

        supply_color = mv < supply_mv_low  ? ST77XX_RED
                     : mv < supply_mv_warn ? ST77XX_YELLOW
                                           : ST77XX_GREEN;
    }
    else
    {
        snprintf(supply, sizeof(supply), " bat n/a");
        supply_color = color_grey_dim;
    }

    // Whether this node is acknowledging, which on the R9 is the difference between
    // 23 ids and 2. Every word is padded to the 11 columns of LISTEN-ONLY, and a trip
    // takes the row's colour whatever the controller's own state says: it has been
    // put back in listen-only with its counters cleared, so that state reads RUN.
    const canbus::controller::ack_state ack = CANCTLR.ack();
    const char* mode = ack == canbus::controller::ack_state::listening     ? "LISTEN-ONLY"
                     : ack == canbus::controller::ack_state::acknowledging ? "ACK"
                     : ack == canbus::controller::ack_state::tripped_state ? "TRIP-STATE"
                     : ack == canbus::controller::ack_state::tripped_rec   ? "TRIP-REC"
                                                                           : "TRIP-TEC";
    if (ack > canbus::controller::ack_state::acknowledging)
    {
        state_color = ST77XX_RED;
    }

    // where the supply readout starts: "CAN 500k LISTEN-ONLY " is 21 columns and the
    // state word after it is padded to 7, the width of BUS-OFF
    constexpr int supply_col = 28;

    int col = 0;
    int y = content_y + 0 * row_h;
    // "%-7s" rather than "%s": the readout after it has to start at the same column
    // whatever the bus is doing -- see the column-change comment on field() for what
    // a moving anchor does to the slot that follows it.
    seg(y, col, state_color, "CAN 500k %-11s %-7s", mode, state);
    col = supply_col;
    seg_fill(y, col, supply_color, "%s", supply);

    col = 0;
    y = content_y + 1 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "rx");
    seg_fill(y, col, ST77XX_WHITE, "%9lu %5lu/s", (unsigned long)c.frames, (unsigned long)_rx_rate);

    // What the app asked for, next to what it got. The IDS page says which ids crossed
    // the filter, but not whether the board is filtering at all -- and "the phone never
    // sent its allow-list" and "the phone asked for three ids" look identical from
    // every other counter on this panel. ALL is the boot state and the bench state.
    int flt = CANDEC.filter_size();
    // An allow-list too small for what the app asked for means a channel the rider
    // defined never arrives, and every other counter here would look healthy while it
    // happened. It cannot fit its own line, so it takes a character.
    uint32_t flt_over = CANDEC.filter_overflow();
    char flt_txt[8];
    if (flt < 0)
    {
        snprintf(flt_txt, sizeof(flt_txt), "ALL");
    }
    else
    {
        snprintf(flt_txt, sizeof(flt_txt), "%d%s", flt, flt_over ? "!" : "");
    }
    uint16_t flt_color = flt_over ? ST77XX_RED : flt == 0 ? ST77XX_YELLOW : ST77XX_WHITE;

    col = 0;
    y = content_y + 2 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "fwd");
    seg(y, col, ST77XX_WHITE, "%9lu %5lu/s", (unsigned long)c.forwarded, (unsigned long)_fwd_rate);
    seg(y, col, ST77XX_YELLOW, " flt");
    seg_fill(y, col, flt_color, " %s", flt_txt);

    // lost is the BLE half of the drop counter two lines down, and it exists for the
    // same reason: BLECharacteristic::notify() returns void and logs a refusal at a
    // level a release build never prints, so a link too slow for the bus would
    // otherwise lose frames in complete silence. It can run into the thousands over a
    // long session, which is why it is the last thing on this row -- nothing after it
    // has to hold a column steady while it grows.
    unsigned long ble_lost = RCDEV.lost();
    // This row's label stays the wordmark's light blue rather than yellow, on
    // request -- it is the BLE identity line, not a generic counter row -- but its
    // values are plain white like every other row's, with red reserved for lost
    // actually being nonzero.
    col = 0;
    y = content_y + 3 * row_h;
    seg(y, col, ST77XX_CYAN, "%-*s", label_w, "ble");
    // Same %9lu/%5lu widths as rx/fwd above -- this used to be %8lu/%4lu, one column
    // narrower, which is exactly what put this row's own value one column to the left
    // of theirs.
    seg(y, col, ST77XX_WHITE, "%9lu %5lu/s", (unsigned long)RCDEV.frames(), (unsigned long)_ble_rate);
    seg(y, col, ST77XX_CYAN, " lost");
    seg_fill(y, col, ble_lost ? ST77XX_RED : ST77XX_WHITE, " %lu", ble_lost);

    // peers should never exceed one. The frame path notifies once per subscriber, so a
    // second connection doubles the BLE traffic for the same bus while every other
    // counter here keeps looking healthy. It lives on the queue row rather than the
    // ble row above: that freed the columns lost needed to grow into the thousands
    // without crowding anything else off the edge, and queue is at least the same
    // kind of thing -- both are "how much of this link's capacity is in use".
    unsigned long peers = RCDEV.peers();

    col = 0;
    y = content_y + 4 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "queue");
    seg(y, col, c.queued > (c.capacity / 2) ? ST77XX_YELLOW : ST77XX_WHITE, "%4lu", (unsigned long)c.queued);
    seg(y, col, ST77XX_YELLOW, " peak");
    seg(y, col, ST77XX_WHITE, " %4lu/%lu", (unsigned long)c.peak, (unsigned long)c.capacity);
    seg(y, col, ST77XX_YELLOW, " peer");
    seg_fill(y, col, peers > 1 ? ST77XX_RED : ST77XX_WHITE, " %lu", peers);

    // The line that matters most and is easiest to miss. A dropped frame is silent
    // everywhere else: the bus shows no error, because the loss was ours.
    uint16_t drop_color = c.dropped ? ST77XX_RED : ST77XX_GREEN;

    col = 0;
    y = content_y + 5 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "drop");
    seg(y, col, drop_color, "%lu", (unsigned long)c.dropped);
    seg(y, col, ST77XX_YELLOW, "  err");
    seg_fill(y, col, drop_color, " %lu", (unsigned long)c.errors);

    // Frame classes the ISR throws away. A sniffer cannot claim a class is empty by
    // discarding it, so they are counted and shown -- every mapped R9 id is 11-bit,
    // and this is what would say otherwise.
    uint16_t anomaly_color = (c.extended || c.remote) ? ST77XX_YELLOW : ST77XX_WHITE;

    col = 0;
    y = content_y + 6 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "ext");
    seg(y, col, anomaly_color, "%lu", (unsigned long)c.extended);
    seg(y, col, ST77XX_YELLOW, " rtr");
    seg(y, col, anomaly_color, " %lu", (unsigned long)c.remote);
    seg(y, col, ST77XX_YELLOW, " tec");
    seg(y, col, ST77XX_WHITE, " %lu", (unsigned long)(running ? CANCTLR.tec() : 0U));
    seg(y, col, ST77XX_YELLOW, " rec");
    seg_fill(y, col, ST77XX_WHITE, " %lu", (unsigned long)(running ? CANCTLR.rec() : 0U));

    UBaseType_t stack = _handle ? uxTaskGetStackHighWaterMark(_handle) : 0;

    // "free", not "heap" -- "heap" reads as the size of an allocation, not as how much
    // of it is left. "disp stck" instead of "stk" so it can't be misread as anything
    // to do with the bike. Two spaces before each, not one: this is the busiest row on
    // the page (three label/value pairs in 40 columns) and the tighter spacing used to
    // read as a run-on rather than three distinguishable fields.
    col = 0;
    y = content_y + 7 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "free");
    seg(y, col, ST77XX_WHITE, "%luk", (unsigned long)(ESP.getFreeHeap() / 1024));
    seg(y, col, ST77XX_YELLOW, "  min");
    seg(y, col, ST77XX_WHITE, " %luk", (unsigned long)(_min_heap / 1024));
    seg(y, col, ST77XX_YELLOW, "  disp stck");
    seg_fill(y, col, ST77XX_WHITE, " %lu", (unsigned long)stack);

    // What a repaint costs, on the glass it costs it on. A full repaint of this panel
    // is ~198 ms and a change-detected one ~160 us; if max ever climbs back towards
    // the former, the change detection has stopped working. Shown in milliseconds --
    // microsecond precision is noise nobody standing over the board can read.
    unsigned long draw_shown_ds = (unsigned long)_draw_us_shown / 100UL;
    unsigned long draw_max_ds = (unsigned long)_draw_us_max / 100UL;
    // mm:ss, not hhh:mm:ss -- this row has no room left for hours once draw and max
    // are spelled out in full, and a ride is over long before uptime needs a third
    // field anyway.
    unsigned long up_s = millis() / 1000UL;
    unsigned long up_m = up_s / 60UL;
    unsigned long up_sec = up_s % 60UL;

    col = 0;
    y = content_y + 8 * row_h;
    seg(y, col, ST77XX_YELLOW, "%-*s", label_w, "draw");
    seg(y, col, ST77XX_WHITE, "%lu.%lums", draw_shown_ds / 10UL, draw_shown_ds % 10UL);
    seg(y, col, ST77XX_YELLOW, " max");
    seg(y, col, ST77XX_WHITE, " %lu.%lums", draw_max_ds / 10UL, draw_max_ds % 10UL);

    // "up" is anchored at a fixed column instead of following on from max's value,
    // because draw/max's rendered width changes (more digits once a repaint spikes),
    // and chasing that with a running column is what let a shrinking value strand "up"
    // wherever it last was -- see the column-change comment on field(). Column 30
    // leaves room for "up " plus "%3lu:%02lu" (999:59, as far as an uptime is expected
    // to run) out to column 39 with none to spare, and margin below it for draw and
    // max to have grown before "up" starts.
    constexpr int up_col = 30;
    col = up_col;
    seg(y, col, ST77XX_YELLOW, "up ");
    seg_fill(y, col, ST77XX_WHITE, "%3lu:%02lu", up_m, up_sec);
}

void display::page_live() noexcept
{
    using namespace canbus;

    // What the R9 is sending, decoded -- the page to hold up against the dash. Every
    // value comes from the decoder's own copy of the latest frame, taken ahead of the
    // allow-list, so this page reads the same whether a phone has asked for these ids
    // or not.
    //
    // A frame older than this is shown as dashes rather than as its last value. A
    // number that has stopped arriving looks exactly like one that has stopped
    // changing, and with the key off that would leave the last engine speed standing
    // on the glass. Half a second is 50 frames of 0x27C, 10 of 0x756, 0x758 and 0x759,
    // and 5 of 0x765; 0x512 comes once a second and 0x76E twice, so they get three.
    constexpr uint32_t stale_fast_ms = 500;
    constexpr uint32_t stale_slow_ms = 3000;

    // Four lines at text size 2 -- twenty columns, two values a line -- on every
    // second content row, and one line at size 1 under them: what changes as the bike
    // moves is big, the temperatures and the commanded valve are small. Labels sit at fixed
    // columns and never change, so they are drawn once; each value is padded to a
    // fixed width so a shorter one cannot leave the tail of a longer one behind.
    // The right-hand label starts at column 11 rather than 10 so that a three-digit
    // value on the left still has a space after it.
    constexpr int big = 2;
    constexpr int left_col = 0;
    constexpr int right_col = 11;

    uint8_t d[8];
    uint32_t age;
    char a[8];
    char b[8];

    // --- 0x27C: engine speed and gear, and further down PWR and TCS. Right after PWR's
    // number, "T" while 0x76E says the dash is on its Track display. Which mode it is cannot
    // be shown: the bus carries the settings a mode makes, not its number.
    const bool engine = CANDEC.latest(r9::id_engine, d, age) && age < stale_fast_ms;
    char tcs[8];
    char pwr[8];

    if (engine)
    {
        const uint8_t g = r9::gear(d);
        snprintf(a, sizeof(a), " %c", g == 0U ? 'N' : g == r9::gear_between ? '-' : static_cast<char>('0' + g));
        snprintf(b, sizeof(b), "%5lu", (unsigned long)r9::rpm(d));
        snprintf(tcs, sizeof(tcs), "%2u", (unsigned)r9::tcs(d));
        const uint8_t p = r9::pwr(d);
        const bool track = CANDEC.latest(r9::id_display, d, age) && age < stale_slow_ms && r9::track_display(d);
        // right-aligned like every other value: the number moves left a column to make
        // room for the 'T', the rider's choice of 2026-10-17
        if (track)
        {
            snprintf(pwr, sizeof(pwr), "%uT", (unsigned)p);
        }
        else
        {
            snprintf(pwr, sizeof(pwr), "%2u", (unsigned)p);
        }
    }
    else
    {
        snprintf(a, sizeof(a), "--");
        snprintf(b, sizeof(b), "   --");
        snprintf(tcs, sizeof(tcs), "--");
        snprintf(pwr, sizeof(pwr), "--");
    }

    const uint16_t engine_color = engine ? ST77XX_WHITE : color_grey_dim;

    // the rider's order of 2026-10-16: engine speed, throttle, wheels, settings; the
    // engine speed and PWR on the left. The values are right-aligned in both columns and
    // end in column 9 on the left and 19 on the right, '%' and PWR's 'T' included, so
    // nothing sticks out past the others.
    int y = content_y + 0 * row_h;
    cell(y, big, left_col, ST77XX_YELLOW, "RPM");
    cell(y, big, left_col + 5, engine_color, "%s", b);
    cell(y, big, right_col, ST77XX_YELLOW, "Gear");
    cell(y, big, right_col + 7, engine_color, "%s", a);

    // --- 0x758: the grip and the throttle valve, in whole percent
    const bool throttle = CANDEC.latest(r9::id_throttle, d, age) && age < stale_fast_ms;

    if (throttle)
    {
        snprintf(a, sizeof(a), "%3u%%", (unsigned)((r9::grip_tenths(d) + 5U) / 10U));
        snprintf(b, sizeof(b), "%3u%%", (unsigned)((r9::valve_tenths(d) + 5U) / 10U));
    }
    else
    {
        snprintf(a, sizeof(a), "  --");
        snprintf(b, sizeof(b), "  --");
    }

    const uint16_t throttle_color = throttle ? ST77XX_WHITE : color_grey_dim;

    y = content_y + 2 * row_h;
    cell(y, big, left_col, ST77XX_YELLOW, "Grip");
    cell(y, big, left_col + 6, throttle_color, "%s", a);
    cell(y, big, right_col, ST77XX_YELLOW, "TPS");
    cell(y, big, right_col + 5, throttle_color, "%s", b);

    // --- 0x765 and 0x756: the wheel speeds, in whole km/h as the dash shows speed;
    // front on the left, the rider's choice of 2026-10-16
    const bool front = CANDEC.latest(r9::id_front, d, age) && age < stale_fast_ms;

    if (front)
    {
        snprintf(a, sizeof(a), "%3u", (unsigned)((r9::front_kmh_x100(d) + 50U) / 100U));
    }
    else
    {
        snprintf(a, sizeof(a), " --");
    }

    const bool rear = CANDEC.latest(r9::id_rear, d, age) && age < stale_fast_ms;

    if (rear)
    {
        snprintf(b, sizeof(b), "%3u", (unsigned)((r9::rear_kmh_x100(d) + 50U) / 100U));
    }
    else
    {
        snprintf(b, sizeof(b), " --");
    }

    y = content_y + 4 * row_h;
    cell(y, big, left_col, ST77XX_YELLOW, "Front");
    cell(y, big, left_col + 7, front ? ST77XX_WHITE : color_grey_dim, "%s", a);
    cell(y, big, right_col, ST77XX_YELLOW, "Rear");
    cell(y, big, right_col + 6, rear ? ST77XX_WHITE : color_grey_dim, "%s", b);

    // --- 0x27C again
    y = content_y + 6 * row_h;
    cell(y, big, left_col, ST77XX_YELLOW, "PWR");
    cell(y, big, left_col + 8, engine_color, "%s", pwr);
    cell(y, big, right_col, ST77XX_YELLOW, "TCS");
    cell(y, big, right_col + 7, engine_color, "%s", tcs);

    // --- the size-1 line: 0x512's two temperatures in whole degrees, and 0x759's
    // valve as commanded, with its tenth -- it leads the measured one by a sample and is
    // only interesting next to it.
    const bool temps = CANDEC.latest(r9::id_temps, d, age) && age < stale_slow_ms;

    if (temps)
    {
        const int water = r9::water_tenths(d);
        const int air = r9::air_tenths(d);
        // rounded to nearest, away from zero: the sensor reads down to -30
        snprintf(a, sizeof(a), "%3dC", (water + (water < 0 ? -5 : 5)) / 10);
        snprintf(b, sizeof(b), "%3dC", (air + (air < 0 ? -5 : 5)) / 10);
    }
    else
    {
        snprintf(a, sizeof(a), " --C");
        snprintf(b, sizeof(b), " --C");
    }

    const uint16_t temps_color = temps ? ST77XX_WHITE : color_grey_dim;

    const bool cmd = CANDEC.latest(r9::id_valve_cmd, d, age) && age < stale_fast_ms;
    char c[12];

    if (cmd)
    {
        const unsigned t = r9::valve_cmd_tenths(d);
        snprintf(c, sizeof(c), "%3u.%u%%", t / 10U, t % 10U);
    }
    else
    {
        snprintf(c, sizeof(c), "  --  ");
    }

    y = content_y + 8 * row_h;
    cell(y, 1, 0, ST77XX_YELLOW, "Water");
    cell(y, 1, 6, temps_color, "%s", a);
    cell(y, 1, 12, ST77XX_YELLOW, "Air");
    cell(y, 1, 16, temps_color, "%s", b);
    cell(y, 1, 22, ST77XX_YELLOW, "TPS cmd");
    cell(y, 1, 30, cmd ? ST77XX_WHITE : color_grey_dim, "%s", c);
}

void display::page_ids() noexcept
{
    int used = _id_used.load(std::memory_order_acquire);

    // The census holds id_slots entries and this page has room for fewer, so there are
    // two ways to be hiding an id. Both have to raise the marker, or a truncated list
    // reads as the whole bus -- which is the one question this page exists to answer.
    //
    // Three columns of eight, which is 24: one more than the R9 sends. It was two
    // columns of sixteen with each id's running total beside its rate, and that was
    // seven ids short of this bike. The total went, because the count is what this
    // page is read for now: 23 means the controller is acknowledging, 2 means it is
    // not.
    static constexpr int id_columns = 3;
    static constexpr int id_cells = (rows - 1) * id_columns;

    row(0, ST77XX_CYAN, "ids seen %d%s", used,
        (_id_overflow || used > id_cells) ? " (+more)" : "");

    // Newest-discovered last, filling across each row.
    for (int r = 0; r < rows - 1; r++)
    {
        char cells[id_columns][14];

        for (int col = 0; col < id_columns; col++)
        {
            int i = r * id_columns + col;
            if (i < used)
            {
                snprintf(cells[col], sizeof(cells[col]), "%03lX %4u/s",
                         (unsigned long)_ids[i], _rates[i]);
            }
            else
            {
                cells[col][0] = '\0';
            }
        }

        row(r + 1, ST77XX_WHITE, "%-13s%-13s%-13s", cells[0], cells[1], cells[2]);
    }
}

void display::page_last() noexcept
{
    uint32_t pos = _ring_pos.load(std::memory_order_acquire);

    row(0, ST77XX_CYAN, "last frames  (newest first)");

    for (int r = 0; r < ring_slots; r++)
    {
        if (pos == 0 || static_cast<uint32_t>(r) >= pos)
        {
            row(r + 1, ST77XX_WHITE, "%s", "");
            continue;
        }

        canbus::frame const& f = _ring[(pos - 1 - r) % ring_slots];

        char bytes[24];
        int n = 0;
        for (uint8_t b = 0; b < f.info.dlc && b < 8; b++)
        {
            n += snprintf(bytes + n, sizeof(bytes) - n, "%02X", f.data.u8[b]);
        }
        bytes[n] = '\0';

        row(r + 1, ST77XX_WHITE, "%03lX [%u] %s",
            (unsigned long)f.id, static_cast<unsigned>(f.info.dlc), bytes);
    }
}

} // namespace ui

#else // !CONFIG_DISPLAY_TFT

namespace ui
{

// Boards without the TFT still compile and run; they simply have nowhere to draw.

display& display::get() noexcept
{
    static display instance;
    return instance;
}

display::display() noexcept
    : _ids{}
    , _hits{}
    , _hits_prev{}
    , _rates{}
    , _id_used(0)
    , _id_overflow(0U)
    , _ring{}
    , _ring_pos(0U)
    , _handle(nullptr)
    , _page(page_live_id)
    , _light(light_dark)
    , _hold(false)
    , _doze(doze_over)
    , _doze_ms(0U)
    , _draw_us(0U)
    , _draw_us_max(0U)
    , _draw_us_shown(0U)
    , _dirty(0U)
    , _min_heap(0xFFFFFFFFU)
    , _rx_prev(0U)
    , _fwd_prev(0U)
    , _ble_prev(0U)
    , _rx_rate(0U)
    , _fwd_rate(0U)
    , _ble_rate(0U)
    , _rate_ms(0U)
    , _stats_timer{}
    , _cache{}
    , _cache_color{}
    , _cache_col{}
    , _slot_y(-1)
    , _slot_n(0)
{
}

bool display::start() noexcept { return true; }
void display::note(canbus::frame const&) noexcept {}
#if defined(DEBUG)
void display::stats() noexcept {}
#endif
void display::task(void*) {}
void display::run() noexcept {}
void display::refresh() noexcept {}
void display::poll_buttons() noexcept {}
void display::sample_rates() noexcept {}
void display::field(int, uint8_t, uint16_t, int, const char*, int) noexcept {}
void display::row(int, uint16_t, const char*, ...) noexcept {}
void display::seg(int, int&, uint16_t, const char*, ...) noexcept {}
void display::seg_fill(int, int&, uint16_t, const char*, ...) noexcept {}
void display::cell(int, uint8_t, int, uint16_t, const char*, ...) noexcept {}
void display::wipe() noexcept {}
void display::page_live() noexcept {}
void display::page_bus() noexcept {}
void display::page_ids() noexcept {}
void display::page_last() noexcept {}

} // namespace ui

#endif // CONFIG_DISPLAY_TFT

ui::display& DISP = ui::display::get();
