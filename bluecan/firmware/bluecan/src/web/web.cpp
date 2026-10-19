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

#include "web.hpp"

#if defined(CONFIG_WIFI)

#if __has_include("secret.hpp")
#include "secret.hpp"
#endif
#if !defined(CONFIG_WIFI_PASSWORD)
#error "CONFIG_WIFI_PASSWORD is not set: tools/reflash-bluecan.sh writes it to src/web/secret.hpp, see bluecan/build.md"
#endif
static_assert(sizeof(CONFIG_WIFI_PASSWORD) - 1U >= 8U, "a WPA2 password has 8 characters or more");

#include "../canbus/controller.hpp"
#include "../canbus/decoder.hpp"
#include "../canbus/frame.hpp"
#if defined(CONFIG_CANBUS_DECODER_YAMAHAR9)
#include "../canbus/r9.hpp"
#endif
#include "../display/display.hpp"
#include "../logging/logging.hpp"
#include "../mode/mode.hpp"
#include "../sdlog/sdlog.hpp"
#include "../utils/timer.hpp"
#include "page.hpp"

#include <WebServer.h>
#include <WiFi.h>
#if defined(CONFIG_SDLOG)
#include <SD.h>
#endif

#include <cstdarg>
#include <cstring>

#include <esp_mac.h>
#include <esp_task_wdt.h>

namespace net
{

namespace
{

// Everything this mode needs is taken from the heap when it starts, not set aside
// statically: the normal mode never runs it, and Bluetooth wants that memory there.

/// bytes: StackType_t is a byte on ESP-IDF. The server's deepest path is the
/// WebServer library parsing a request, with lwIP and the SD library under it
constexpr uint32_t server_stack_size = 8 * 1024;
constexpr uint32_t census_stack_size = 3 * 1024;

/// how long the census task blocks on an empty queue, and how many frames it takes
/// before it yields a tick: as the Bluetooth drain task does, so that core 0's idle
/// task always runs and the task watchdog is fed
constexpr TickType_t census_wait = pdMS_TO_TICKS(20);
constexpr uint32_t census_batch = 128U;

/// how long after the page asked to leave the board restarts: time for the answer
/// to reach the phone
constexpr uint32_t leave_delay_ms = 500U;

/// the server's, made on its own task the first time it runs
WebServer* server = nullptr;

char ssid[16];

/// the statistics as JSON, built by the server task alone
constexpr size_t json_size = 4096U;
char* json = nullptr;
size_t json_len = 0U;

/// one block of a file on its way to the phone. A multiple of the card's 512-byte
/// sectors, so that the SD library reads whole ones
constexpr size_t block_size = 4096U;
uint8_t* block = nullptr;

#if defined(DEBUG)
TaskHandle_t server_handle = nullptr;
TaskHandle_t census_handle = nullptr;
#endif

/// the board's own total frame rate, frames a second, from the census task
std::atomic<uint32_t> bus_rate{0U};

/// append to json[], never past its end
void put(const char* fmt, ...) noexcept __attribute__((format(printf, 1, 2)));
void put(const char* fmt, ...) noexcept
{
    if (json_len >= json_size - 1U)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(json + json_len, json_size - json_len, fmt, args);
    va_end(args);
    if (n > 0)
    {
        json_len += static_cast<size_t>(n);
        if (json_len > json_size - 1U)
        {
            json_len = json_size - 1U;
        }
    }
}

const char* ack_name(canbus::controller::ack_state s) noexcept
{
    switch (s)
    {
    case canbus::controller::ack_state::listening:     return "listening";
    case canbus::controller::ack_state::acknowledging: return "acknowledging";
    case canbus::controller::ack_state::tripped_state: return "tripped, controller state";
    case canbus::controller::ack_state::tripped_rec:   return "tripped, receive errors";
    case canbus::controller::ack_state::tripped_tec:   return "tripped, transmit errors";
    }
    return "unknown";
}

/// a log file's name as the card has it: bluecan-NNNN.bin, and nothing else, so that
/// no request can reach another path
bool log_name(const char* name) noexcept
{
    if (strlen(name) != 16U || strncmp(name, "bluecan-", 8) != 0 || strcmp(name + 12, ".bin") != 0)
    {
        return false;
    }
    for (int i = 8; i < 12; i++)
    {
        if (name[i] < '0' || name[i] > '9')
        {
            return false;
        }
    }
    return true;
}

} // namespace

web::web() noexcept
    : _last_ms(0U)
    , _leave_ms(0U)
    , _stations(0U)
    , _ids{}
    , _hits{}
    , _hits_prev{}
    , _rates{}
    , _data{}
    , _dlc{}
    , _seen_ms{}
    , _id_used(0)
    , _id_overflow(0U)
    , _moving_since_ms(0U)
{
}

bool web::start() noexcept
{
    bootln("Wi-Fi starting...");

    json = static_cast<char*>(malloc(json_size));
    block = static_cast<uint8_t*>(malloc(block_size));
    if (json == nullptr || block == nullptr)
    {
        errorln("ERROR: no heap for the Wi-Fi mode's buffers");
        return false;
    }

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "bluecan-%02X%02X", mac[4], mac[5]);

    // Not written to flash: the settings are the same every time, and each entry to
    // this mode would otherwise be a write to NVS.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid, CONFIG_WIFI_PASSWORD, CONFIG_WIFI_CHANNEL, 0, 2))
    {
        errorln("ERROR: Wi-Fi access point %s did not start", ssid);
        return false;
    }
    bootln("Wi-Fi: network %s, page http://%s/", ssid, WiFi.softAPIP().toString().c_str());

    _last_ms.store(millis(), std::memory_order_relaxed);

    TaskHandle_t census = nullptr;
    TaskHandle_t serve = nullptr;
    xTaskCreatePinnedToCore(census_task, "census", census_stack_size, this,
                            tskIDLE_PRIORITY + 1, &census, 0);
    xTaskCreatePinnedToCore(server_task, "web", server_stack_size, this,
                            tskIDLE_PRIORITY + 1, &serve, 0);
#if defined(DEBUG)
    census_handle = census;
    server_handle = serve;
#endif
    return census != nullptr && serve != nullptr;
}

void web::check() noexcept
{
    // Each moment read before millis(), so that it can never be later than now.
    const uint32_t last = _last_ms.load(std::memory_order_relaxed);
    const uint32_t leave = _leave_ms.load(std::memory_order_relaxed);
    const uint32_t now = millis();

    if (leave != 0U && now - leave >= leave_delay_ms)
    {
        bootln("Wi-Fi: the page asked for the normal mode");
        mode::restart_into(mode::run::normal);
    }

    if (now - last >= CONFIG_WIFI_IDLE_S * 1000U)
    {
        bootln("Wi-Fi: no request for %u s", (unsigned)CONFIG_WIFI_IDLE_S);
        mode::restart_into(mode::run::normal);
    }

#if defined(CONFIG_CANBUS_DECODER_YAMAHAR9)
    // The bike is moving: the rear wheel over CONFIG_WIFI_MOVING_KMH_X100 in every frame
    // for CONFIG_WIFI_MOVING_MS. A frame that is old counts as standing still.
    uint8_t d[8];
    uint32_t age_ms = 0U;
    const bool moving = CANDEC.latest(canbus::r9::id_rear, d, age_ms) && age_ms < 500U
                     && canbus::r9::rear_kmh_x100(d) > CONFIG_WIFI_MOVING_KMH_X100;
    if (!moving)
    {
        _moving_since_ms = 0U;
    }
    else if (_moving_since_ms == 0U)
    {
        _moving_since_ms = now == 0U ? 1U : now;
    }
    else if (now - _moving_since_ms >= CONFIG_WIFI_MOVING_MS)
    {
        bootln("Wi-Fi: the rear wheel above %u.%02u km/h for %u ms",
               (unsigned)(CONFIG_WIFI_MOVING_KMH_X100 / 100), (unsigned)(CONFIG_WIFI_MOVING_KMH_X100 % 100),
               (unsigned)CONFIG_WIFI_MOVING_MS);
        mode::restart_into(mode::run::normal);
    }
#endif
}

void web::census_task(void* self)
{
    static_cast<web*>(self)->count();
}

void web::count() noexcept
{
    static canbus::frame f;
    uint32_t drained = 0U;
    uint32_t sample_ms = millis();
    uint32_t frames_prev = CANCTLR.counters().frames;
#if defined(DEBUG)
    utils::timer stats_timer;
#endif

    esp_task_wdt_add(nullptr);

    while (true)
    {
        esp_task_wdt_reset();

        while (CANCTLR.recv(f, census_wait))
        {
            note(f);
            DISP.note(f);
            if (++drained >= census_batch)
            {
                drained = 0U;
                vTaskDelay(1);
                break;
            }
        }

        // Rates once a second, by difference, as the display's census does.
        const uint32_t now = millis();
        const uint32_t elapsed = now - sample_ms;
        if (elapsed >= 1000U)
        {
            const int used = _id_used.load(std::memory_order_acquire);
            for (int i = 0; i < used; i++)
            {
                const uint32_t hits = _hits[i];
                _rates[i] = static_cast<uint16_t>(((hits - _hits_prev[i]) * 1000U + elapsed / 2U) / elapsed);
                _hits_prev[i] = hits;
            }
            const uint32_t frames = CANCTLR.counters().frames;
            bus_rate.store(((frames - frames_prev) * 1000U + elapsed / 2U) / elapsed, std::memory_order_relaxed);
            frames_prev = frames;
            sample_ms = now;
        }

#if defined(DEBUG)
        if (stats_timer.elapsed(CONFIG_RC_STATS_TIMEOUT) > 0UL)
        {
            infoln("              Wi-Fi: %s at %s, %lu on it, last request %lu s ago, %d ids",
                   ssid, WiFi.softAPIP().toString().c_str(),
                   (unsigned long)_stations.load(std::memory_order_relaxed),
                   (unsigned long)((now - _last_ms.load(std::memory_order_relaxed)) / 1000U),
                   _id_used.load(std::memory_order_relaxed));
            infoln("        Wi-Fi heap: %lu bytes free, least %lu; stacks free: web %lu, census %lu",
                   (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                   (unsigned long)uxTaskGetStackHighWaterMark(server_handle),
                   (unsigned long)uxTaskGetStackHighWaterMark(census_handle));
        }
#endif
    }
}

void web::note(canbus::frame const& f) noexcept
{
    const int used = _id_used.load(std::memory_order_relaxed);
    int i = 0;
    while (i < used && _ids[i] != f.id)
    {
        i++;
    }
    if (i == used)
    {
        if (used == id_slots)
        {
            _id_overflow++;
            return;
        }
        _ids[i] = f.id;
        _hits[i] = 0U;
        _hits_prev[i] = 0U;
        _rates[i] = 0U;
        _id_used.store(used + 1, std::memory_order_release);
    }
    _hits[i]++;
    _dlc[i] = f.info.dlc;
    memcpy(_data[i], f.data.u8, 8);
    _seen_ms[i] = millis();
}

void web::server_task(void* self)
{
    static_cast<web*>(self)->serve();
}

void web::serve() noexcept
{
    static WebServer s(80);
    server = &s;

    s.on("/", HTTP_GET, [this]() { page(); });
    s.on("/stats.json", HTTP_GET, [this]() { stats(); });
    s.on("/files.json", HTTP_GET, [this]() { files(); });
    s.on("/file", HTTP_GET, [this]() { file(); });
    s.on("/leave", HTTP_POST, [this]() { leave(); });
    s.onNotFound([]() { server->send(404, "text/plain", "not found\n"); });
    s.begin();

    uint32_t stations_ms = 0U;
    while (true)
    {
        s.handleClient();

        const uint32_t now = millis();
        if (now - stations_ms >= 500U)
        {
            stations_ms = now;
            _stations.store(WiFi.softAPgetStationNum(), std::memory_order_relaxed);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void web::page() noexcept
{
    _last_ms.store(millis(), std::memory_order_relaxed);
    server->send_P(200, "text/html", page_html, sizeof(page_html) - 1U);
}

void web::stats() noexcept
{
    const uint32_t now = millis();
    _last_ms.store(now, std::memory_order_relaxed);

    const canbus::controller::counters_t c = CANCTLR.counters();

    json_len = 0U;
    put("{\"ssid\":\"%s\",\"up_s\":%lu,\"stations\":%lu,\"idle_s\":%u,"
        "\"heap\":%lu,\"heap_min\":%lu,",
        ssid, (unsigned long)(now / 1000U), (unsigned long)_stations.load(std::memory_order_relaxed),
        (unsigned)CONFIG_WIFI_IDLE_S,
        (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
    put("\"bus\":{\"state\":\"%s\",\"tec\":%lu,\"rec\":%lu,\"trips\":%lu,"
        "\"frames\":%lu,\"rate\":%lu,\"errors\":%lu,\"extended\":%lu,\"remote\":%lu,"
        "\"dropped\":%lu,\"overflow\":%lu},",
        ack_name(CANCTLR.ack()), (unsigned long)CANCTLR.tec(), (unsigned long)CANCTLR.rec(),
        (unsigned long)CANCTLR.trips(), (unsigned long)c.frames,
        (unsigned long)bus_rate.load(std::memory_order_relaxed), (unsigned long)c.errors,
        (unsigned long)c.extended, (unsigned long)c.remote, (unsigned long)c.dropped,
        (unsigned long)_id_overflow);

    put("\"bike\":{");
#if defined(CONFIG_CANBUS_DECODER_YAMAHAR9)
    {
        // Each value null until its frame has arrived, or once it is over two seconds
        // old: the bus has gone quiet, and a number from before would read as now.
        uint8_t d[8];
        uint32_t age_ms = 0U;
        const bool engine = CANDEC.latest(canbus::r9::id_engine, d, age_ms) && age_ms < 2000U;
        if (engine)
        {
            put("\"rpm\":%lu,\"gear\":%u,", (unsigned long)canbus::r9::rpm(d), (unsigned)canbus::r9::gear(d));
        }
        else
        {
            put("\"rpm\":null,\"gear\":null,");
        }
        if (CANDEC.latest(canbus::r9::id_rear, d, age_ms) && age_ms < 2000U)
        {
            put("\"rear_x100\":%u,", (unsigned)canbus::r9::rear_kmh_x100(d));
        }
        else
        {
            put("\"rear_x100\":null,");
        }
        if (CANDEC.latest(canbus::r9::id_front, d, age_ms) && age_ms < 2000U)
        {
            put("\"front_x100\":%u,", (unsigned)canbus::r9::front_kmh_x100(d));
        }
        else
        {
            put("\"front_x100\":null,");
        }
        if (CANDEC.latest(canbus::r9::id_temps, d, age_ms) && age_ms < 2000U)
        {
            put("\"water_x10\":%d,\"air_x10\":%d", (int)canbus::r9::water_tenths(d), (int)canbus::r9::air_tenths(d));
        }
        else
        {
            put("\"water_x10\":null,\"air_x10\":null");
        }
    }
#endif
    put("},\"ids\":[");

    const int used = _id_used.load(std::memory_order_acquire);
    for (int i = 0; i < used; i++)
    {
        char hex[17];
        const uint8_t dlc = _dlc[i] > 8U ? 8U : _dlc[i];
        for (uint8_t b = 0; b < dlc; b++)
        {
            snprintf(hex + 2 * b, 3, "%02X", _data[i][b]);
        }
        hex[2 * dlc] = '\0';
        put("%s{\"id\":%lu,\"n\":%lu,\"hz\":%u,\"data\":\"%s\",\"age_ms\":%lu}",
            i == 0 ? "" : ",", (unsigned long)_ids[i], (unsigned long)_hits[i],
            (unsigned)_rates[i], hex, (unsigned long)(now - _seen_ms[i]));
    }
    put("]}");

    server->send_P(200, "application/json", json, json_len);
}

void web::files() noexcept
{
    _last_ms.store(millis(), std::memory_order_relaxed);

#if defined(CONFIG_SDLOG)
    if (!SDLOG.mount_for_reading())
    {
        server->send(200, "application/json", "{\"card\":false,\"files\":[]}");
        return;
    }

    // Sent in pieces: a card can hold thousands of files, and the list has no fixed size.
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "application/json", "");
    server->sendContent("{\"card\":true,\"files\":[");

    bool first = true;
    File root = SD.open("/");
    if (root)
    {
        for (File f = root.openNextFile(); f; f = root.openNextFile())
        {
            // The SD library gives names with or without the leading slash
            // depending on its version; the check wants them without.
            const char* name = f.name();
            if (name[0] == '/')
            {
                name++;
            }
            if (!f.isDirectory() && log_name(name))
            {
                char entry[64];
                snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"size\":%lu}",
                         first ? "" : ",", name, (unsigned long)f.size());
                server->sendContent(entry);
                first = false;
            }
            f.close();
        }
        root.close();
    }
    server->sendContent("]}");
    server->sendContent("");
#else
    server->send(200, "application/json", "{\"card\":null,\"files\":[]}");
#endif
}

void web::file() noexcept
{
    _last_ms.store(millis(), std::memory_order_relaxed);

#if defined(CONFIG_SDLOG)
    const String name = server->arg("name");
    if (!log_name(name.c_str()))
    {
        server->send(400, "text/plain", "not a log file's name\n");
        return;
    }
    if (!SDLOG.mount_for_reading())
    {
        server->send(503, "text/plain", "no card\n");
        return;
    }

    char path[24];
    snprintf(path, sizeof(path), "/%s", name.c_str());
    File f = SD.open(path, FILE_READ);
    if (!f)
    {
        server->send(404, "text/plain", "no such file\n");
        return;
    }

    const size_t size = f.size();
    char disposition[48];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name.c_str());
    server->setContentLength(size);
    server->sendHeader("Content-Disposition", disposition);
    server->send(200, "application/octet-stream", "");

    // Block by block rather than the library's streamFile(), which goes on reading
    // the whole file after the phone has gone away. Each block counts as a request,
    // so a long download is not taken for an idle page.
    NetworkClient& client = server->client();
    size_t sent = 0U;
    while (sent < size)
    {
        const size_t want = size - sent < block_size ? size - sent : block_size;
        const size_t got = f.read(block, want);
        if (got == 0U || client.write(block, got) != got)
        {
            break;
        }
        sent += got;
        _last_ms.store(millis(), std::memory_order_relaxed);
    }
    f.close();
    if (sent != size)
    {
        warnln("Wi-Fi: %s stopped at %lu of %lu bytes", name.c_str(),
               (unsigned long)sent, (unsigned long)size);
    }
#else
    server->send(404, "text/plain", "this build has no card log\n");
#endif
}

void web::leave() noexcept
{
    _last_ms.store(millis(), std::memory_order_relaxed);
    server->send(200, "text/plain", "back to the normal mode\n");
    const uint32_t now = millis();
    _leave_ms.store(now == 0U ? 1U : now, std::memory_order_relaxed);
}

} // namespace net

#endif // CONFIG_WIFI

namespace net
{

web& web::get() noexcept
{
    static web instance;
    return instance;
}

#if !defined(CONFIG_WIFI)
web::web() noexcept
{
}
#endif

} // namespace net

net::web& WEB = net::web::get();
