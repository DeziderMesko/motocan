// bluecan monitor sketch
//
// A bench test of RaceChrono's DIY Monitor API: the direction in which the app writes
// values back to the device. The question is whether the phone's time can reach the
// board that way, so it asks RaceChrono for the channel `timestamp` and prints every
// value the app writes. It answers four things:
//
//   1. the unit of `timestamp`: monitor 0 is the channel itself, monitor 1 the same
//      divided by 1000, so seconds and milliseconds read differently in one of them
//   2. its resolution, and how the app turns it into an integer: once the unit is
//      known, monitor 3 is added at run time as milliseconds since a recent second,
//      which fits in the API's int32 where the epoch in ms does not
//   3. whether a value comes without a GPS fix: monitor 2, the GPS speed, is the
//      control that says the Monitor path works at all
//   4. whether RaceChrono, with CAN-Bus and Monitor both ticked, still subscribes to
//      the frame characteristic and still writes its allow-list. Both are printed
//
// It is the RaceChrono DIY service, 0x1FF8, with 0x0001 and 0x0002 as in Bluecan and
// 0x0005 and 0x0006 added. No frame is notified on 0x0001, and the CAN controller is
// not started: nothing goes onto any bus. The protocol is aollin's
// racechrono-ble-diy-device README, and the chunking of an equation into 17-byte
// payloads follows MagnusThome's Racechrono-LIVE-telemetry-data-export.
//
// While connected it prints the status every 30 s, so a run needs no commands. Serial
// commands, one character each, for tools/bench.py -c or --at:
//   ?   status: link, the configuration, per-monitor counts, intervals and spread
//   u   ask the app to update every monitor now (command 4, "Update all")
//   r   remove every monitor and configure them again
//   z   zero the per-monitor statistics
//   v/V print every value write on / off (on at start)
//
// Board: Adafruit Feather ESP32-S3 Reverse TFT (#5691)
// Build: ./build.sh bluecan_monitor --upload

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <time.h>

#if !defined(CONFIG_NIMBLE_ENABLED)
#error "written against the NimBLE host, as Bluecan is"
#endif

static constexpr uint16_t SERVICE_UUID    = 0x1ff8;
static constexpr uint16_t CHR_CAN_FRAMES  = 0x0001;
static constexpr uint16_t CHR_CAN_FILTER  = 0x0002;
static constexpr uint16_t CHR_MON_CONFIG  = 0x0005;
static constexpr uint16_t CHR_MON_VALUES  = 0x0006;

// Monitor configuration commands, device to app
static constexpr uint8_t CMD_REMOVE_ALL     = 0;
static constexpr uint8_t CMD_ADD_INCOMPLETE = 2;
static constexpr uint8_t CMD_ADD_COMPLETE   = 3;
static constexpr uint8_t CMD_UPDATE_ALL     = 4;

// Results, app to device
static constexpr uint8_t RES_OK              = 0;
static constexpr uint8_t RES_OUT_OF_SEQUENCE = 1;
static constexpr uint8_t RES_EXCEPTION       = 2;

// A configuration indication is 3 bytes of header and at most 17 of equation: the
// 20 bytes of the default ATT MTU, which the app is not assumed to have raised.
static constexpr size_t PAYLOAD_PART = 17;

// the API's value for "no value"
static constexpr int32_t INVALID = 0x7fffffff;

// An epoch in seconds between 2020 and 2036, for telling seconds from milliseconds.
static constexpr int32_t EPOCH_S_MIN = 1577836800;
static constexpr int32_t EPOCH_S_MAX = 2082758400;

static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ---------------------------------------------------------------------------------
// What the NimBLE host task hands to loop(). The callbacks only copy and queue; every
// print and every decision is made in loop(), so a slow serial port never holds up
// the host.

enum : uint8_t { EV_CONNECT, EV_DISCONNECT, EV_REASON, EV_MTU, EV_PARAMS, EV_SUBSCRIBE,
                 EV_FILTER, EV_CONFIG, EV_VALUES, EV_INDICATED };

struct bt_event
{
    int64_t  t_us;      // esp_timer at the callback
    uint8_t  kind;
    uint8_t  len;       // bytes kept in data
    uint16_t arg;       // characteristic, interval, MTU or status, by kind
    uint16_t arg2;      // subscription bits, latency, or the write's full length
    uint8_t  data[48];
};

static QueueHandle_t g_events;
static volatile uint32_t g_dropped = 0;   // events lost to a full queue

static void push(uint8_t kind, uint16_t arg, uint16_t arg2, const uint8_t* data, size_t len)
{
    bt_event e;
    e.t_us = esp_timer_get_time();
    e.kind = kind;
    e.arg  = arg;
    e.arg2 = arg2;
    e.len  = (uint8_t)(len < sizeof e.data ? len : sizeof e.data);
    if (data && e.len) memcpy(e.data, data, e.len);
    if (xQueueSend(g_events, &e, 0) != pdTRUE) g_dropped++;
}

// ---------------------------------------------------------------------------------
// The monitors

struct monitor
{
    const char* label;
    char        equation[96];
    bool        sent;       // the last chunk was indicated
    int8_t      result;     // -1 no answer yet, else the app's result code
    uint16_t    exception;  // the exception type, when result is RES_EXCEPTION
    uint32_t    count;
    uint32_t    invalid;
    int32_t     last;
    int64_t     last_us;
    uint32_t    gap_min_us, gap_max_us;
    uint64_t    gap_sum_us;
};

static constexpr int N_MONITORS = 4;
static monitor g_mon[N_MONITORS] = {
    { "ts",      "channel(device(gps), timestamp)" },
    { "ts/1000", "channel(device(gps), timestamp) / 1000" },
    { "speed",   "channel(device(gps), speed) * 100" },
    { "ts ms",   "" },  // built once the unit is known
};
static constexpr int MON_TS = 0, MON_TS_K = 1, MON_SPEED = 2, MON_MS = 3;

// Monitor 3 is (timestamp - base) in milliseconds, base a whole second shortly before
// its first value, so it stays within int32 for 24 days. g_base_s is that second.
static int64_t g_base_s = 0;

// The spread of (phone time - board time) over monitor 3's changes: how much the delay
// from the phone's clock to this callback varies. Only a value that differs from the
// one before counts; the app also repeats an unchanged value, late. The first change
// sets the zero.
static bool    g_skew_set = false;
static int64_t g_skew0_us = 0;
static int64_t g_skew_min_us = 0, g_skew_max_us = 0;

static void reset_stats()
{
    for (auto& m : g_mon) {
        m.count = m.invalid = 0;
        m.last_us = 0;
        m.gap_min_us = UINT32_MAX;
        m.gap_max_us = 0;
        m.gap_sum_us = 0;
    }
    g_skew_set = false;
}

// ---------------------------------------------------------------------------------
// Link state, owned by loop()

static bool     g_connected  = false;
static uint16_t g_sub_frames = 0;   // subscription bits on 0x0001
static uint16_t g_sub_config = 0;   // on 0x0005
static int64_t  g_config_at  = 0;   // when to start configuring, 0 for not
static uint32_t g_filter_writes = 0;
static uint32_t g_value_writes  = 0;
static bool     g_verbose = true;

static BLEServer*         g_server;
static BLECharacteristic* g_frames;
static BLECharacteristic* g_filter;
static BLECharacteristic* g_config;
static volatile uint16_t  g_conn = BLE_HS_CONN_HANDLE_NONE;

// The phone's confirmation of the last indication: waiting, confirmed, or the host's
// status when it failed. Set by handle_event().
static constexpr int CONFIRM_WAITING = -1;
static int g_confirm = CONFIRM_WAITING;

// The app's answer to the last configuration command, filled in by handle_event().
static bool    g_reply = false;
static uint8_t g_reply_data[8];
static uint8_t g_reply_len = 0;

// ---------------------------------------------------------------------------------
// BLE callbacks

class server_cb : public BLEServerCallbacks
{
    void onConnect(BLEServer*, ble_gap_conn_desc* d) override
    {
        g_conn = d->conn_handle;
        push(EV_CONNECT, d->conn_itvl, d->conn_latency, nullptr, 0);
    }
    void onDisconnect(BLEServer*, ble_gap_conn_desc*) override
    {
        g_conn = BLE_HS_CONN_HANDLE_NONE;
        push(EV_DISCONNECT, 0, 0, nullptr, 0);
        BLEDevice::startAdvertising();
    }
    void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override
    {
        push(EV_MTU, mtu, 0, nullptr, 0);
    }
    void onConnParamsUpdate(uint16_t, uint16_t interval, uint16_t latency,
                            uint16_t, uint8_t status) override
    {
        push(EV_PARAMS, interval, latency, &status, 1);
    }
};

class chr_cb : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic* c, ble_gap_conn_desc*) override
    {
        const uint8_t kind = c == g_filter ? EV_FILTER : c == g_config ? EV_CONFIG : EV_VALUES;
        push(kind, 0, (uint16_t)c->getLength(), c->getData(), c->getLength());
    }
    void onSubscribe(BLECharacteristic* c, ble_gap_conn_desc*, uint16_t sub) override
    {
        const uint16_t uuid = c == g_frames ? CHR_CAN_FRAMES : c == g_config ? CHR_MON_CONFIG : 0;
        push(EV_SUBSCRIBE, uuid, sub, nullptr, 0);
    }
    // Reached from the host's BLE_GAP_EVENT_NOTIFY_TX: SUCCESS_INDICATE when the phone
    // confirmed, ERROR_INDICATE_FAILURE with the host's status when it did not.
    void onStatus(BLECharacteristic* c, Status s, uint32_t code) override
    {
        if (c != g_config) return;
        if (s == SUCCESS_INDICATE) push(EV_INDICATED, 0, 0, nullptr, 0);
        else if (s == ERROR_INDICATE_FAILURE) push(EV_INDICATED, (uint16_t)code, 0, nullptr, 0);
    }
};

// The server callbacks are not told why a link ended; a GAP listener is.
static ble_gap_event_listener g_gap_listener;

static int on_gap_event(ble_gap_event* ev, void*)
{
    if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        push(EV_REASON, (uint16_t)ev->disconnect.reason, 0, nullptr, 0);
    }
    return 0;
}

static const char* reason_text(uint16_t r)
{
    switch (r) {
    case BLE_HS_ERR_HCI_BASE + 0x08: return "supervision timeout";
    case BLE_HS_ERR_HCI_BASE + 0x13: return "the phone ended it";
    case BLE_HS_ERR_HCI_BASE + 0x16: return "this board ended it";
    case BLE_HS_ERR_HCI_BASE + 0x22: return "link layer response timeout";
    case BLE_HS_ERR_HCI_BASE + 0x3e: return "failed to establish";
    default:                         return "other";
    }
}

static server_cb g_server_cb;
static chr_cb    g_chr_cb;

// ---------------------------------------------------------------------------------
// Printing

static void print_utc_ms(int64_t epoch_ms)
{
    time_t s = (time_t)(epoch_ms / 1000);
    struct tm t;
    gmtime_r(&s, &t);
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d.%03dZ",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec, (int)(epoch_ms % 1000));
}

static const char* sub_text(uint16_t sub)
{
    switch (sub & 3) {
    case 0:  return "off";
    case 1:  return "notify";
    case 2:  return "indicate";
    default: return "notify+indicate";
    }
}

static void print_filter(const uint8_t* d, uint8_t len)
{
    if (len == 1 && d[0] == 0) {
        Serial.print("DENY all");
    } else if (len == 3 && d[0] == 1) {
        Serial.printf("ALLOW all, %u ms", d[1] << 8 | d[2]);
    } else if (len == 7 && d[0] == 2) {
        Serial.printf("ALLOW 0x%03lx, %u ms",
                      (unsigned long)((uint32_t)d[3] << 24 | d[4] << 16 | d[5] << 8 | d[6]),
                      d[1] << 8 | d[2]);
    } else {
        Serial.print("unknown:");
        for (uint8_t i = 0; i < len; i++) Serial.printf(" %02x", d[i]);
    }
}

static void status()
{
    Serial.printf("link %s | 0x0001 %s | 0x0005 %s | filter writes %lu | value writes %lu"
                  " | dropped %lu\n",
                  g_connected ? "connected" : "advertising",
                  sub_text(g_sub_frames), sub_text(g_sub_config),
                  (unsigned long)g_filter_writes, (unsigned long)g_value_writes,
                  (unsigned long)g_dropped);
    for (int i = 0; i < N_MONITORS; i++) {
        const monitor& m = g_mon[i];
        if (!m.equation[0]) {
            Serial.printf("  m%d %-7s not added yet\n", i, m.label);
            continue;
        }
        Serial.printf("  m%d %-7s %-9s", i, m.label,
                      !m.sent ? "not sent" : m.result < 0 ? "no answer"
                      : m.result == RES_OK ? "ok" : m.result == RES_EXCEPTION ? "exception"
                      : "out of seq");
        if (m.result == RES_EXCEPTION) Serial.printf(" type %u", m.exception);
        Serial.printf(" | %lu values, %lu invalid", (unsigned long)m.count,
                      (unsigned long)m.invalid);
        if (m.count > 1) {
            Serial.printf(" | interval min %.1f mean %.1f max %.1f ms",
                          m.gap_min_us / 1000.0, m.gap_sum_us / 1000.0 / (m.count - 1),
                          m.gap_max_us / 1000.0);
        }
        Serial.printf(" | %s\n", m.equation);
    }
    if (g_skew_set) {
        Serial.printf("  m3 phone time - board time, spread: %.1f .. %.1f ms\n",
                      g_skew_min_us / 1000.0, g_skew_max_us / 1000.0);
    }
}

// ---------------------------------------------------------------------------------
// Values

// Monitor 3 is added once one of monitors 0 and 1 reads as an epoch in seconds. Which
// one does says the unit, and the equation is written for it.
static bool g_add_ms_pending = false;

static void consider_unit(int id, int32_t v)
{
    if (g_mon[MON_MS].equation[0] || g_add_ms_pending) return;
    if (v < EPOCH_S_MIN || v > EPOCH_S_MAX) return;

    g_base_s = (int64_t)v - 2;
    if (id == MON_TS) {
        snprintf(g_mon[MON_MS].equation, sizeof g_mon[MON_MS].equation,
                 "(channel(device(gps), timestamp) - %lld) * 1000", (long long)g_base_s);
        Serial.printf(">> timestamp reads as seconds; adding m3 with base %lld\n",
                      (long long)g_base_s);
    } else {
        snprintf(g_mon[MON_MS].equation, sizeof g_mon[MON_MS].equation,
                 "channel(device(gps), timestamp) - %lld000", (long long)g_base_s);
        Serial.printf(">> timestamp reads as milliseconds; adding m3 with base %lld s\n",
                      (long long)g_base_s);
    }
    g_add_ms_pending = true;
}

static void handle_value(int64_t t_us, uint8_t id, int32_t v)
{
    if (id >= N_MONITORS) {
        Serial.printf("m? id %u value %ld\n", id, (long)v);
        return;
    }
    monitor& m = g_mon[id];
    if (m.count) {
        const uint32_t gap = (uint32_t)(t_us - m.last_us);
        if (gap < m.gap_min_us) m.gap_min_us = gap;
        if (gap > m.gap_max_us) m.gap_max_us = gap;
        m.gap_sum_us += gap;
    }
    const bool changed = m.count == 0 || m.last != v;
    m.count++;
    m.last = v;
    m.last_us = t_us;
    if (v == INVALID) m.invalid++;

    if (g_verbose) {
        Serial.printf("m%u %-7s t %10.3f ms  %11ld", id, m.label, t_us / 1000.0, (long)v);
        if (v == INVALID) {
            Serial.print("  invalid");
        } else if (id == MON_TS && v >= EPOCH_S_MIN && v <= EPOCH_S_MAX) {
            Serial.print("  ");
            print_utc_ms((int64_t)v * 1000);
        } else if (id == MON_MS) {
            Serial.print("  ");
            print_utc_ms(g_base_s * 1000 + v);
        }
        Serial.println();
    }

    if (v == INVALID) return;
    if (id == MON_TS || id == MON_TS_K) consider_unit(id, v);
    if (id == MON_MS && changed) {
        const int64_t skew = (g_base_s * 1000 + v) * 1000 - t_us;
        if (!g_skew_set) {
            g_skew_set = true;
            g_skew0_us = skew;
            g_skew_min_us = g_skew_max_us = 0;
        } else {
            const int64_t d = skew - g_skew0_us;
            if (d < g_skew_min_us) g_skew_min_us = d;
            if (d > g_skew_max_us) g_skew_max_us = d;
        }
    }
}

// ---------------------------------------------------------------------------------
// Events from the host task

static void handle_event(const bt_event& e)
{
    switch (e.kind) {
    case EV_CONNECT:
        g_connected = true;
        Serial.printf(">> connected, interval %.2f ms, latency %u\n",
                      e.arg * 1.25, e.arg2);
        break;

    case EV_DISCONNECT:
        g_connected = false;
        g_sub_frames = g_sub_config = 0;
        g_config_at = 0;
        Serial.println(">> disconnected, advertising");
        break;

    case EV_REASON:
        Serial.printf(">> disconnect reason 0x%03x, %s\n", e.arg, reason_text(e.arg));
        break;

    case EV_INDICATED:
        g_confirm = e.arg;
        break;

    case EV_MTU:
        Serial.printf(">> MTU %u\n", e.arg);
        break;

    case EV_PARAMS:
        Serial.printf(">> interval %.2f ms, latency %u, status %u\n",
                      e.arg * 1.25, e.arg2, e.len ? e.data[0] : 0);
        break;

    case EV_SUBSCRIBE:
        Serial.printf(">> 0x%04x subscription %s, %.3f ms\n", e.arg, sub_text(e.arg2),
                      e.t_us / 1000.0);
        if (e.arg == CHR_CAN_FRAMES) {
            g_sub_frames = e.arg2;
        } else if (e.arg == CHR_MON_CONFIG) {
            g_sub_config = e.arg2;
            // The README: start indicating only once the app has registered for
            // indications. The pause is the example's margin, not the spec's.
            g_config_at = (e.arg2 & 2) ? e.t_us + 500000 : 0;
        }
        break;

    case EV_FILTER:
        g_filter_writes++;
        Serial.printf(">> 0x0002 ");
        print_filter(e.data, e.len);
        Serial.println();
        break;

    case EV_CONFIG:
        g_reply = true;
        g_reply_len = e.len < sizeof g_reply_data ? e.len : sizeof g_reply_data;
        memcpy(g_reply_data, e.data, g_reply_len);
        Serial.printf(">> 0x0005 answer:");
        for (uint8_t i = 0; i < e.len; i++) Serial.printf(" %02x", e.data[i]);
        Serial.println();
        break;

    case EV_VALUES:
        g_value_writes++;
        if (e.arg2 > e.len) {
            Serial.printf(">> 0x0006 write of %u bytes, %u kept\n", e.arg2, e.len);
        }
        for (uint8_t p = 0; p + 5 <= e.len; p += 5) {
            const int32_t v = (int32_t)((uint32_t)e.data[p + 1] << 24 |
                                        (uint32_t)e.data[p + 2] << 16 |
                                        (uint32_t)e.data[p + 3] << 8 |
                                        (uint32_t)e.data[p + 4]);
            handle_value(e.t_us, e.data[p], v);
        }
        if (e.len % 5) Serial.printf(">> 0x0006 write of %u bytes, not a multiple of 5\n", e.len);
        break;
    }
}

static void drain_events()
{
    bt_event e;
    while (xQueueReceive(g_events, &e, 0) == pdTRUE) handle_event(e);
}

// ---------------------------------------------------------------------------------
// Configuration, device to app

// Indicates one command and waits for the confirmation, then for the app's answer on
// 0x0005, draining events meanwhile. Returns false if the link is gone.
//
// Straight to the host, as Bluecan notifies frames. In this core's NimBLE build
// BLECharacteristic::indicate() never sees the confirmation: it waits out its second
// and reports a timeout whatever the phone did. The confirmation does arrive, as
// BLE_GAP_EVENT_NOTIFY_TX, which reaches onStatus().
static bool indicate(const uint8_t* bytes, size_t len, uint32_t answer_ms)
{
    const uint16_t conn = g_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return false;
    os_mbuf* om = ble_hs_mbuf_from_flat(bytes, len);
    if (!om) {
        Serial.println(">> no mbuf for the indication");
        return false;
    }
    g_reply = false;
    g_confirm = CONFIRM_WAITING;
    const uint32_t t0 = millis();
    // consumes the mbuf whatever the outcome
    const int rc = ble_gatts_indicate_custom(conn, g_config->getHandle(), om);
    if (rc != 0) {
        Serial.printf(">> indication refused by the host, rc %d\n", rc);
        return false;
    }
    // ATT gives the phone 30 s to confirm
    while (g_confirm == CONFIRM_WAITING && millis() - t0 < 31000) {
        drain_events();
        if (!g_connected) return false;
        delay(1);
    }
    if (g_confirm != 0) {
        Serial.printf(">> indication not confirmed after %lu ms, status %d\n",
                      (unsigned long)(millis() - t0), g_confirm);
        return false;
    }
    Serial.printf(">> 0x0005 indication of %u bytes confirmed in %lu ms\n",
                  (unsigned)len, (unsigned long)(millis() - t0));
    const uint32_t t1 = millis();
    while (!g_reply && millis() - t1 < answer_ms) {
        drain_events();
        if (!g_connected) return false;
        delay(1);
    }
    return true;
}

static bool add_monitor(int id)
{
    monitor& m = g_mon[id];
    const size_t n = strlen(m.equation);
    m.sent = false;
    m.result = -1;
    uint8_t seq = 0;
    for (size_t at = 0; at < n; at += PAYLOAD_PART, seq++) {
        const size_t part = n - at < PAYLOAD_PART ? n - at : PAYLOAD_PART;
        const bool last = at + part >= n;
        uint8_t b[3 + PAYLOAD_PART];
        b[0] = last ? CMD_ADD_COMPLETE : CMD_ADD_INCOMPLETE;
        b[1] = (uint8_t)id;
        b[2] = seq;
        memcpy(b + 3, m.equation + at, part);
        // An incomplete part may or may not be answered; a complete one must be.
        if (!indicate(b, 3 + part, last ? 3000 : 300)) return false;
        if (g_reply && g_reply_len >= 1 && g_reply_data[0] != RES_OK) break;
    }
    m.sent = true;
    if (g_reply && g_reply_len >= 1) {
        m.result = (int8_t)g_reply_data[0];
        if (m.result == RES_EXCEPTION && g_reply_len >= 4) {
            m.exception = g_reply_data[2] << 8 | g_reply_data[3];
        }
    }
    Serial.printf(">> m%d %s: %s\n", id,
                  m.result < 0 ? "no answer" : m.result == RES_OK ? "added"
                  : m.result == RES_EXCEPTION ? "equation exception" : "out of sequence",
                  m.equation);
    return true;
}

static void configure()
{
    Serial.println(">> configuring monitors");
    const uint8_t remove_all[3] = { CMD_REMOVE_ALL, 0, 0 };
    if (!indicate(remove_all, sizeof remove_all, 300)) return;
    for (int i = 0; i < N_MONITORS; i++) {
        if (g_mon[i].equation[0] && !add_monitor(i)) return;
    }
}

static void update_all()
{
    const uint8_t b[3] = { CMD_UPDATE_ALL, 0, 0 };
    Serial.println(">> update all");
    indicate(b, sizeof b, 300);
}

// ---------------------------------------------------------------------------------
// Screen: enough to watch with the phone in one hand and no console

static void draw()
{
    char line[4][24];
    snprintf(line[0], sizeof line[0], "%-11s %s",
             g_connected ? "connected" : "advertising",
             g_sub_frames & 1 ? "SUB" : "");
    int ok = 0;
    for (auto& m : g_mon) ok += m.result == RES_OK;
    snprintf(line[1], sizeof line[1], "mon ok %d  flt %lu", ok, (unsigned long)g_filter_writes);
    snprintf(line[2], sizeof line[2], "vals %lu", (unsigned long)g_value_writes);

    const monitor& ms = g_mon[MON_MS];
    const monitor& ts = g_mon[MON_TS];
    int64_t now_ms = -1;
    if (ms.count && ms.last != INVALID) now_ms = g_base_s * 1000 + ms.last;
    else if (ts.count && ts.last >= EPOCH_S_MIN && ts.last <= EPOCH_S_MAX) now_ms = (int64_t)ts.last * 1000;
    if (now_ms >= 0) {
        time_t s = (time_t)(now_ms / 1000);
        struct tm t;
        gmtime_r(&s, &t);
        snprintf(line[3], sizeof line[3], "%02d:%02d:%02d.%03dZ",
                 t.tm_hour, t.tm_min, t.tm_sec, (int)(now_ms % 1000));
    } else {
        snprintf(line[3], sizeof line[3], "--:--:--.---Z");
    }

    tft.setTextWrap(false);
    tft.setTextSize(2);
    for (int i = 0; i < 4; i++) {
        tft.setTextColor(i == 3 ? ST77XX_WHITE : ST77XX_CYAN, ST77XX_BLACK);
        tft.setCursor(0, 8 + i * 32);
        tft.printf("%-20s", line[i]);
    }
}

// ---------------------------------------------------------------------------------

static void start_ble()
{
    char name[32];
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        snprintf(name, sizeof name, "RC Monitor %02X:%02X:%02X", mac[3], mac[4], mac[5]);
    } else {
        snprintf(name, sizeof name, "RC Monitor");
    }
    BLEDevice::init(name);
    ble_gap_event_listener_register(&g_gap_listener, on_gap_event, nullptr);

    g_server = BLEDevice::createServer();
    g_server->setCallbacks(&g_server_cb);
    BLEService* service = g_server->createService(BLEUUID(SERVICE_UUID));

    g_frames = service->createCharacteristic(BLEUUID(CHR_CAN_FRAMES),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    g_frames->setCallbacks(&g_chr_cb);
    g_filter = service->createCharacteristic(BLEUUID(CHR_CAN_FILTER),
        BLECharacteristic::PROPERTY_WRITE);
    g_filter->setCallbacks(&g_chr_cb);
    g_config = service->createCharacteristic(BLEUUID(CHR_MON_CONFIG),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_INDICATE);
    g_config->setCallbacks(&g_chr_cb);
    BLECharacteristic* values = service->createCharacteristic(BLEUUID(CHR_MON_VALUES),
        BLECharacteristic::PROPERTY_WRITE_NR);
    values->setCallbacks(&g_chr_cb);
    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(service->getUUID());
    adv->setScanResponse(false);
    BLEDevice::startAdvertising();
    Serial.printf(">> advertising as \"%s\"\n", name);
}

static void handle_serial()
{
    while (Serial.available()) {
        switch (Serial.read()) {
        case '?': status(); break;
        case 'u': if (g_sub_config & 2) update_all(); else Serial.println(">> not subscribed"); break;
        case 'r':
            if (g_sub_config & 2) configure(); else Serial.println(">> not subscribed");
            break;
        case 'z': reset_stats(); Serial.println(">> stats zeroed"); break;
        case 'v': g_verbose = true;  Serial.println(">> values on");  break;
        case 'V': g_verbose = false; Serial.println(">> values off"); break;
        default: break;   // stray bytes are not errors
        }
    }
}

void setup()
{
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
    tft.init(135, 240);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);

    Serial.begin(115200);
    Serial.setTxTimeoutMs(5);

    g_events = xQueueCreate(64, sizeof(bt_event));
    reset_stats();
    for (auto& m : g_mon) m.result = -1;
    start_ble();
}

void loop()
{
    drain_events();
    handle_serial();

    if (g_config_at && esp_timer_get_time() >= g_config_at) {
        g_config_at = 0;
        configure();
    }
    if (g_add_ms_pending && (g_sub_config & 2)) {
        g_add_ms_pending = false;
        // configure() adds it too, when the equation was built while it ran
        if (!g_mon[MON_MS].sent) add_monitor(MON_MS);
    }

    static uint32_t last_status = 0;
    if (!g_connected) {
        last_status = millis();
    } else if (millis() - last_status >= 30000) {
        last_status = millis();
        status();
    }

    static uint32_t last_draw = 0;
    if (millis() - last_draw >= 250) {
        last_draw = millis();
        draw();
    }
    delay(1);
}
