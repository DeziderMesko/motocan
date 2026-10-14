// bluecan bench sketch
//
// Step 2 of the plan in ../../history/firmware.md: the first firmware that puts the
// Feather on a bus with a *second node* on it. Everything before this was one
// node talking to itself -- ../bluecan_bringup proved the MCU, the toolchain and
// the KAmod, but it installs the TWAI driver, runs one self-reception frame and
// uninstalls again, so between self-tests the Feather is electrically absent from
// the bus. That is fine for bring-up and useless for an end-to-end test.
//
// This sketch instead keeps the controller running continuously, in NORMAL mode,
// so it does what a real node does: receives, counts, and -- crucially -- ACKs.
// The bench partner is the CANable (see ../../hardware.md), joined to the KAmod
// over a twisted pair, CAN-H to CAN-H and CAN-L to CAN-L, 120R at each end.
//
// What each direction proves:
//
//   Feather -> CANable   the KAmod's bus driver reaches a foreign receiver, and
//                        the pair carries 500 kbps differential signalling
//   CANable -> Feather   the KAmod's receiver recovers a foreign transmitter's
//                        frames, which self-reception could never show
//   either direction     an ACK from the other end. Both nodes stay ERROR-ACTIVE
//                        with flat error counters, which is the single clearest
//                        proof the link is real: an unACKed transmitter climbs to
//                        TEC 128 and goes ERROR-PASSIVE within a few milliseconds
//
// Buttons:
//   D0  toggle NORMAL <-> LISTEN-ONLY   (reinstalls the driver; see below)
//   D1  send a burst of 10 frames, id 0x236, payload A0..A7
//   D2  toggle per-frame serial printing
//
// Every one of those is also a single-character serial command, so a bench run can be
// scripted from a shell rather than performed by hand. Unlike the buttons the commands
// set rather than toggle -- lower case on, upper case off -- so a script never depends
// on the state it inherited. Send '?' for the list.
//
// D0's listen-only toggle is not a convenience. It is patch 2 from ../../history/firmware.md
// -- the mode the device MUST be in before it ever touches the R9 -- and this is the
// cheapest place to prove it works: in listen-only the Feather stops ACKing, so the
// CANable should fall to ERROR-PASSIVE exactly as it does with no partner at all.
// Being able to show that on the bench means the bike is never the experiment.
//
// Board: Adafruit Feather ESP32-S3 Reverse TFT (#5691)
// FQBN:  esp32:esp32:adafruit_feather_esp32s3_reversetft

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/twai.h"

static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_17;  // A1
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_18;  // A0

// The R9's rate per ../../../analysis/r9-signals.md, and what the CANable is brought up at.
static constexpr int CAN_BITRATE_KBPS = 500;

static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

static twai_mode_t g_mode      = TWAI_MODE_NORMAL;
static bool        g_installed = false;
static bool        g_tx_beat   = true;   // periodic heartbeat frame
static bool        g_verbose   = true;   // per-frame serial printing
static bool        g_show      = true;   // TFT repaints

// How long the slowest pass through loop() took in the last second, and how much
// of that was the TFT. The RX queue is 64 frames deep, which at the ~1000 frames/s
// ../../history/firmware.md quotes for an R1-family bus is only ~64 ms of slack -- so a
// single blocking pass longer than that drops frames with no bus error to show for
// it. Worth measuring rather than reasoning about: the first bench run lost 1200 of
// 2000 frames with tec/rec flat at zero, which says the loss was ours, not the bus's.
static uint32_t g_max_loop_us = 0;
static uint32_t g_max_loop_rep = 0;
static uint32_t g_draw_us = 0;

// Set by 'z' on the loop task, honoured by the display task. The TFT has exactly one
// owner once display_task starts, and it is not this one.
static volatile bool g_clear_request = false;

// Soak instrumentation. A run of a few seconds cannot distinguish "works" from "works
// until it doesn't", and the three ways this firmware could fail slowly are all
// invisible in the frame counters:
//
//   a leak      -- free heap trending down. Nothing here allocates on purpose, but the
//                  Arduino USBCDC and the TWAI driver both do, so "on purpose" is not
//                  the same as "not at all"
//   a reset     -- the uptime counter starting over. Easy to miss in a long log, and
//                  a watchdog reset looks exactly like a clean boot afterwards, so the
//                  reason is printed at startup where it cannot be confused for one
//   a stack     -- the display task overflowing. It formats into 48-byte buffers and
//                  calls into Adafruit_GFX, and 4 KB was a guess
static TaskHandle_t g_display_task = NULL;
static uint32_t g_min_heap_seen = 0xFFFFFFFF;

static uint32_t g_rx_total = 0;
static uint32_t g_tx_total = 0;
static uint32_t g_tx_fail  = 0;
static uint32_t g_rx_rate  = 0;   // frames in the last completed second
static uint32_t g_rx_window = 0;
static uint32_t g_last_rx_id = 0xFFFFFFFF;
static uint8_t  g_last_rx_data[8];
static uint8_t  g_last_rx_dlc = 0;
static uint32_t g_bus_off_events = 0;

// Which arbitration IDs have been seen, and how often. This is the whole point of
// the device per ../../history/readme-2026-10-13.md -- "which CAN family does the R9 belong to" is
// answered by the ID list, not by decoding anything -- so it goes in from the start
// even though the bench partner only ever sends what we tell it to.
static constexpr int ID_SLOTS = 24;
static uint32_t g_ids[ID_SLOTS];
static uint32_t g_id_hits[ID_SLOTS];
static int      g_id_used = 0;
static uint32_t g_id_overflow = 0;

static void note_id(uint32_t id)
{
    for (int i = 0; i < g_id_used; i++) {
        if (g_ids[i] == id) { g_id_hits[i]++; return; }
    }
    if (g_id_used < ID_SLOTS) {
        g_ids[g_id_used] = id;
        g_id_hits[g_id_used] = 1;
        g_id_used++;
    } else {
        g_id_overflow++;
    }
}

static void zero_counters()
{
    g_rx_total = g_tx_total = g_tx_fail = 0;
    g_rx_rate = g_rx_window = 0;
    g_id_used = 0;
    g_id_overflow = 0;
    g_last_rx_id = 0xFFFFFFFF;
    g_last_rx_dlc = 0;
    g_max_loop_us = g_max_loop_rep = 0;
    g_draw_us = 0;
    g_clear_request = true;
}

static const char* mode_name()
{
    return g_mode == TWAI_MODE_LISTEN_ONLY ? "LISTEN-ONLY"
         : g_mode == TWAI_MODE_NO_ACK      ? "NO-ACK"
                                           : "NORMAL";
}

static const char* state_name(twai_state_t s)
{
    return s == TWAI_STATE_STOPPED    ? "STOPPED"
         : s == TWAI_STATE_RUNNING    ? "RUNNING"
         : s == TWAI_STATE_BUS_OFF    ? "BUS-OFF"
         : s == TWAI_STATE_RECOVERING ? "RECOVER"
                                      : "?";
}

// ---------------------------------------------------------------- controller

static void can_stop()
{
    if (!g_installed) return;
    twai_stop();
    twai_driver_uninstall();
    g_installed = false;
}

static bool can_start(twai_mode_t mode)
{
    can_stop();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, mode);
    // The default queues are 5 deep. At 500 kbps a full bus is ~1000 frames/s and
    // ../../history/firmware.md quotes ~1050 msg/s for the R1-family bus, so 5 slots is
    // about 5 ms of headroom -- less than one TFT repaint. 64 is still trivial RAM
    // and turns a dropped-frame bug into a non-event.
    g.rx_queue_len = 64;
    g.tx_queue_len = 16;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        Serial.println("!! twai_driver_install failed");
        return false;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("!! twai_start failed");
        twai_driver_uninstall();
        return false;
    }

    // Bus-off is the one condition the sketch cannot talk its way out of, so ask to
    // be told about it rather than inferring it from a stalled counter.
    twai_reconfigure_alerts(TWAI_ALERT_BUS_OFF | TWAI_ALERT_ERR_PASS |
                            TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_RECOVERED, NULL);

    g_mode = mode;
    g_installed = true;
    Serial.printf(">> TWAI up: %d kbps, %s\n", CAN_BITRATE_KBPS, mode_name());
    return true;
}

static void can_transmit(uint32_t id, const uint8_t* data, uint8_t dlc)
{
    if (!g_installed) return;
    // Listen-only physically cannot transmit; the driver rejects it. Say so once
    // rather than letting the failure counter tick up and look like a bus fault.
    if (g_mode == TWAI_MODE_LISTEN_ONLY) {
        Serial.println("   (tx suppressed: listen-only)");
        return;
    }

    twai_message_t tx = {};
    tx.identifier = id;
    tx.data_length_code = dlc;
    for (int i = 0; i < dlc; i++) tx.data[i] = data[i];

    // A short timeout on purpose. With no ACK partner the frame is retransmitted
    // forever and the queue backs up; blocking here would freeze the UI and hide
    // exactly the failure worth seeing.
    if (twai_transmit(&tx, pdMS_TO_TICKS(50)) == ESP_OK) {
        g_tx_total++;
    } else {
        g_tx_fail++;
    }
}

// Drain the whole RX queue each pass. Bounded so a flood cannot starve the rest of
// loop(), but the bound is well above one pass's worth at 500 kbps.
static void can_drain()
{
    if (!g_installed) return;

    static uint32_t printed_second = 0;
    static uint32_t printed_count  = 0;
    uint32_t now_s = millis() / 1000;
    if (now_s != printed_second) { printed_second = now_s; printed_count = 0; }

    twai_message_t rx;
    for (int i = 0; i < 128; i++) {
        if (twai_receive(&rx, 0) != ESP_OK) break;

        g_rx_total++;
        g_rx_window++;
        note_id(rx.identifier);
        g_last_rx_id = rx.identifier;
        g_last_rx_dlc = rx.data_length_code;
        for (int b = 0; b < 8; b++) g_last_rx_data[b] = b < rx.data_length_code ? rx.data[b] : 0;

        // Printing every frame over USB CDC is self-defeating above a few hundred
        // per second: the write blocks, the RX queue overflows, and the sketch
        // reports a bus problem it caused itself. So cap the printing, never the
        // counting -- the rate line below stays honest either way.
        if (g_verbose && printed_count < 20) {
            printed_count++;
            Serial.printf("RX %s%03lX [%u] ",
                          rx.extd ? "x" : "", (unsigned long)rx.identifier,
                          rx.data_length_code);
            for (int b = 0; b < rx.data_length_code; b++) Serial.printf("%02X ", rx.data[b]);
            Serial.println();
        } else if (g_verbose && printed_count == 20) {
            printed_count++;
            Serial.println("RX ... (rate too high to print; counting continues)");
        }
    }
}

static void can_check_alerts()
{
    if (!g_installed) return;
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) != ESP_OK) return;

    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) Serial.println("!! rx queue full -- frames dropped");
    if (alerts & TWAI_ALERT_ERR_PASS)      Serial.println("!! error-passive (nobody ACKing?)");
    if (alerts & TWAI_ALERT_BUS_OFF) {
        g_bus_off_events++;
        Serial.println("!! BUS-OFF -- initiating recovery");
        twai_initiate_recovery();
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED) {
        Serial.println(">> bus recovered, restarting");
        twai_start();
    }
}

// ---------------------------------------------------------------------- ui

static void draw_static()
{
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(0, 0);
    tft.println("bluecan bench");
    tft.setTextSize(1);
}

// One line of the display, redrawn only when its text actually changed.
//
// The whole panel is ~250 characters and Adafruit_GFX renders text a character at a
// time over SPI, so repainting all of it measured 198 ms -- three times the RX
// queue's entire depth at bus rate. Most of those characters are identical from one
// frame to the next (the ID census barely moves, the mode never does), so comparing
// against what is already on the glass turns a full repaint into a couple of short
// ones. The cache is per-call-site, hence the static buffer each caller owns.
static void field(int y, uint16_t color, char* cache, size_t cache_len, const char* text)
{
    if (strncmp(cache, text, cache_len) == 0) return;
    snprintf(cache, cache_len, "%s", text);
    tft.setCursor(0, y);
    tft.setTextColor(color, ST77XX_BLACK);
    tft.print(text);
}

static void draw_live()
{
    twai_status_info_t st = {};
    bool have = g_installed && twai_get_status_info(&st) == ESP_OK;

    tft.setTextSize(1);

    char buf[48];
    static char c_mode[48], c_counts[48], c_err[48], c_last[48], c_hdr[48], c_ids[3][48], c_foot[48];

    if (g_clear_request) {
        g_clear_request = false;
        tft.fillRect(0, 22, 240, 113, ST77XX_BLACK);
        // Blank the caches too, or the next pass sees "unchanged" and redraws nothing
        // onto a screen that was just wiped.
        c_mode[0] = c_counts[0] = c_err[0] = c_last[0] = c_hdr[0] = c_foot[0] = '\x01';
        c_ids[0][0] = c_ids[1][0] = c_ids[2][0] = '\x01';
    }

    snprintf(buf, sizeof buf, "mode %-12s %-8s", mode_name(), have ? state_name(st.state) : "DOWN");
    field(22, ST77XX_WHITE, c_mode, sizeof c_mode, buf);

    snprintf(buf, sizeof buf, "rx %-8lu %4lu/s  tx %-8lu",
             (unsigned long)g_rx_total, (unsigned long)g_rx_rate, (unsigned long)g_tx_total);
    field(34, ST77XX_WHITE, c_counts, sizeof c_counts, buf);

    if (have) {
        snprintf(buf, sizeof buf, "tec %-4lu rec %-4lu drop %-6lu",
                 (unsigned long)st.tx_error_counter, (unsigned long)st.rx_error_counter,
                 (unsigned long)(st.rx_missed_count + st.rx_overrun_count));
        field(46, (st.tx_error_counter || st.rx_error_counter) ? ST77XX_YELLOW : ST77XX_GREEN,
              c_err, sizeof c_err, buf);
    } else {
        field(46, ST77XX_RED, c_err, sizeof c_err, "controller down            ");
    }

    if (g_last_rx_id != 0xFFFFFFFF) {
        int n = snprintf(buf, sizeof buf, "last %03lX [%u] ",
                         (unsigned long)g_last_rx_id, g_last_rx_dlc);
        for (int i = 0; i < g_last_rx_dlc && i < 6 && n < (int)sizeof buf - 3; i++) {
            n += snprintf(buf + n, sizeof buf - n, "%02X", g_last_rx_data[i]);
        }
        snprintf(buf + n, sizeof buf - n, "     ");
    } else {
        snprintf(buf, sizeof buf, "last  --                    ");
    }
    field(58, ST77XX_WHITE, c_last, sizeof c_last, buf);

    // The ID census: the first real question this device exists to answer, per
    // ../../history/readme-2026-10-13.md -- which CAN family the R9 belongs to is read off the ID list,
    // not off any decoded value.
    snprintf(buf, sizeof buf, "ids seen: %-3d%-12s", g_id_used, g_id_overflow ? " (+more)" : "");
    field(74, ST77XX_CYAN, c_hdr, sizeof c_hdr, buf);

    for (int row = 0; row < 3; row++) {
        int n = 0;
        buf[0] = 0;
        for (int col = 0; col < 4; col++) {
            int i = row * 4 + col;
            if (i < g_id_used) n += snprintf(buf + n, sizeof buf - n, "%03lX:%-6lu",
                                             (unsigned long)g_ids[i], (unsigned long)g_id_hits[i]);
            else               n += snprintf(buf + n, sizeof buf - n, "          ");
        }
        field(86 + row * 10, ST77XX_WHITE, c_ids[row], sizeof c_ids[row], buf);
    }

    snprintf(buf, sizeof buf, "D0 mode D1 burst D2 log:%s up %lus  ",
             g_verbose ? "on " : "off", millis() / 1000);
    field(118, ST77XX_WHITE, c_foot, sizeof c_foot, buf);
}

// The display gets its own task on core 0, at a priority below the Arduino loop.
//
// This is the hard rule from ../../history/firmware.md, and the bench measured exactly the
// failure it exists to prevent: with draw_live() called from loop() on core 1, a
// 2000-frame run at ~940 frames/s delivered 854 and dropped 1200, with the
// controller's error counters flat at zero the whole time. Nothing was wrong on the
// bus -- the frames arrived and rotted in the RX queue while the CPU pushed pixels.
// Off the CAN core, the same run delivers 2000 of 2000.
//
// Core 0 is also where the BLE stack will live, which is why the repaint cost was
// worth cutting as well as relocating: a task that blocks core 0 for 198 ms at a
// time would just move the problem onto the link to RaceChrono.
static void display_task(void*)
{
    for (;;) {
        if (g_show) {
            uint32_t t0 = micros();
            draw_live();
            g_draw_us = micros() - t0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// --------------------------------------------------------------- host commands

// The three buttons are the right interface at the handlebar, and the wrong one on
// the bench: every test here is driven from a WSL shell over the same USB cable that
// carries the log, and nobody can reach across and press D1 from inside a script. So
// the same actions are reachable as single-character serial commands, which makes a
// bench run reproducible end to end instead of "press this, then watch that".
//
// The buttons toggle. These do not: lower case turns a thing on, upper case turns it
// off, and 'm'/'M' name the mode outright. A toggle is the right control for a finger
// on a board you can see and the wrong one for a script, which has no idea what state
// it inherited -- a soak run that silently enabled per-frame logging instead of
// disabling it measured a 25 ms worst-case loop() instead of 700 us, and the log gave
// no hint that the two runs differed.
static void handle_serial()
{
    while (Serial.available()) {
        int c = Serial.read();
        switch (c) {
        case 'v':
        case 'V':
            g_verbose = (c == 'v');
            Serial.printf(">> frame logging %s\n", g_verbose ? "on" : "off");
            break;
        case 'd':
        case 'D':
            g_show = (c == 'd');
            Serial.printf(">> display %s\n", g_show ? "on" : "off");
            break;
        case 'h':
        case 'H':
            g_tx_beat = (c == 'h');
            Serial.printf(">> heartbeat %s\n", g_tx_beat ? "on" : "off");
            break;
        case 'b': {
            uint8_t d[8];
            for (int i = 0; i < 8; i++) d[i] = 0xA0 + i;
            Serial.println(">> burst: 10 x 236#A0A1A2A3A4A5A6A7");
            for (int i = 0; i < 10; i++) can_transmit(0x236, d, 8);
            break;
        }
        case 'm':
        case 'M':
            can_start(c == 'm' ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
            break;
        case 'z':
            zero_counters();
            Serial.println(">> counters zeroed");
            break;
        case '?':
            Serial.println(">> lower=on upper=off: v log, d display, h heartbeat, "
                           "m NORMAL/M listen-only. b burst, z zero");
            break;
        default:
            break;  // newlines and stray bytes are not errors
        }
    }
}

// ------------------------------------------------------------------- main

void setup()
{
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);

    tft.init(135, 240);
    // Adafruit_ST7789 defaults conservatively. The ST7789 is good for 40 MHz and the
    // repaint cost is almost entirely SPI clocking, so this is close to a free 
    // halving of it. It is not the real fix -- see display_task -- just cheaper work.
    tft.setSPISpeed(40000000);
    tft.setRotation(3);
    draw_static();

    Serial.begin(115200);
    delay(2000);  // USB CDC needs to enumerate before the first printf lands

    // D0 is the BOOT strapping pin: pulled up, reads LOW when pressed. D1/D2 are
    // pulled down and read HIGH. See ../../hardware.md -- they do not share polarity.
    pinMode(0, INPUT_PULLUP);
    pinMode(1, INPUT_PULLDOWN);
    pinMode(2, INPUT_PULLDOWN);

    Serial.println();
    Serial.println("=== bluecan bench ===");
    Serial.printf("core %d\n", xPortGetCoreID());

    // Printed unconditionally, because the interesting case is the one nobody is
    // watching for: a soak run that quietly restarted hours in and kept counting from
    // zero as if nothing happened.
    esp_reset_reason_t rr = esp_reset_reason();
    const char* rname = rr == ESP_RST_POWERON  ? "power-on"
                      : rr == ESP_RST_SW       ? "software"
                      : rr == ESP_RST_PANIC    ? "PANIC / exception"
                      : rr == ESP_RST_INT_WDT  ? "INTERRUPT WATCHDOG"
                      : rr == ESP_RST_TASK_WDT ? "TASK WATCHDOG"
                      : rr == ESP_RST_WDT      ? "OTHER WATCHDOG"
                      : rr == ESP_RST_BROWNOUT ? "BROWNOUT"
                      : rr == ESP_RST_USB      ? "usb peripheral"
                      : rr == ESP_RST_EXT      ? "external pin"
                                               : "other";
    Serial.printf("reset reason: %s (%d)\n", rname, (int)rr);
    Serial.printf("heap at boot: %lu free, %lu largest block\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    can_start(TWAI_MODE_NORMAL);

    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 1, &g_display_task, 0);
}

void loop()
{
    uint32_t loop_t0 = micros();

    can_drain();
    can_check_alerts();
    handle_serial();

    bool d0 = digitalRead(0) == LOW;
    bool d1 = digitalRead(1) == HIGH;
    bool d2 = digitalRead(2) == HIGH;

    static bool p0 = false, p1 = false, p2 = false;

    if (d0 && !p0) {
        can_start(g_mode == TWAI_MODE_NORMAL ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    }
    if (d1 && !p1) {
        // 0x236 with A0..A7 is the same frame ../bluecan_bringup sends, kept
        // identical so the two sketches are directly comparable on the wire.
        uint8_t d[8];
        for (int i = 0; i < 8; i++) d[i] = 0xA0 + i;
        Serial.println(">> burst: 10 x 236#A0A1A2A3A4A5A6A7");
        for (int i = 0; i < 10; i++) can_transmit(0x236, d, 8);
    }
    if (d2 && !p2) {
        g_verbose = !g_verbose;
        Serial.printf(">> frame logging %s\n", g_verbose ? "on" : "off");
    }
    p0 = d0; p1 = d1; p2 = d2;

    // Heartbeat frame, once a second. Gives the CANable side something to see
    // without anyone touching the board, which is what makes the Feather -> CANable
    // direction testable from a shell alone.
    static uint32_t last_beat = 0;
    if (g_tx_beat && g_mode != TWAI_MODE_LISTEN_ONLY && millis() - last_beat >= 1000) {
        last_beat = millis();
        static uint32_t seq = 0;
        seq++;
        uint8_t d[8] = {
            (uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq,
            0xBE, 0xEF, 0x00, 0x01
        };
        can_transmit(0x100, d, 8);
    }

    static uint32_t last_second = 0;
    if (millis() - last_second >= 1000) {
        last_second = millis();
        g_rx_rate = g_rx_window;
        g_rx_window = 0;

        twai_status_info_t st = {};
        bool have = g_installed && twai_get_status_info(&st) == ESP_OK;
        g_max_loop_rep = g_max_loop_us;
        g_max_loop_us = 0;

        uint32_t heap = ESP.getFreeHeap();
        if (heap < g_min_heap_seen) g_min_heap_seen = heap;
        UBaseType_t hwm = g_display_task ? uxTaskGetStackHighWaterMark(g_display_task) : 0;

        Serial.printf("up %4lus | %-11s %-7s | rx %-7lu %4lu/s | tx %-6lu fail %-4lu | "
                      "tec %-3lu rec %-3lu drop %-4lu | ids %d | loop %lu us draw %lu us | "
                      "heap %lu min %lu | dstack %lu\n",
                      millis() / 1000, mode_name(), have ? state_name(st.state) : "DOWN",
                      (unsigned long)g_rx_total, (unsigned long)g_rx_rate,
                      (unsigned long)g_tx_total, (unsigned long)g_tx_fail,
                      (unsigned long)(have ? st.tx_error_counter : 0),
                      (unsigned long)(have ? st.rx_error_counter : 0),
                      (unsigned long)(have ? st.rx_missed_count + st.rx_overrun_count : 0),
                      g_id_used,
                      (unsigned long)g_max_loop_rep, (unsigned long)g_draw_us,
                      (unsigned long)heap, (unsigned long)g_min_heap_seen,
                      (unsigned long)hwm);
    }

    uint32_t took = micros() - loop_t0;
    if (took > g_max_loop_us) g_max_loop_us = took;
}
