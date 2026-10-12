// bluecan clock sketch
//
// A wall clock on the Feather's TFT, seeded over USB from the machine that runs
// candump, so a camera can film the clock and the video shares a time base with
// the CAN log. Nothing else: no CAN, no BLE, no other pixels on the glass. That
// is the whole point -- it is a reference, and a reference with a second job is a
// reference you cannot trust.
//
// It does NOT stay tethered. Seed it on the bench, unplug it, ride. esp_timer runs
// off the 40 MHz crystal (~10-20 ppm), so an hour of riding costs ~0.1 s, which the
// pipeline's lag search absorbs without noticing.
//
//   ../tools/clock-seed.py            seed from the host clock, local time
//   ../tools/clock-seed.py --utc      seed in UTC instead
//
// Two things the OCR side (analysis/css-skill .../vision_reference.py) imposes on
// the layout, and they are why this looks the way it does:
//
//   - It runs RapidOCR with use_det=False on the crop and takes res.txts[0], so the
//     ROI must contain the clock and nothing else. Hence a blank screen around it.
//   - Its regex is (\d{1,2})\D(\d{2})\D(\d{2}), so the separator has to be there.
//     "12:34:56" parses, "123456" does not.
//
// Unseeded reads "--:--:--", which that regex cannot match -- a board that lost its
// clock produces no samples instead of wrong ones.
//
// Board: Adafruit Feather ESP32-S3 Reverse TFT (#5691)
// Build: ./build.sh bluecan_clock --upload

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "esp_timer.h"

static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Rotation 3 gives 240x135. The classic GFX font is a 6x8 cell, so size 5 is 30x40
// per character and eight of them are 240 px -- the full width, exactly, which is
// the largest the built-in font goes without a custom face. Bigger is better here:
// this gets read by a camera on a moving bike.
static constexpr int TEXT_SIZE = 5;
static constexpr int TEXT_X    = 0;
static constexpr int TEXT_Y    = (135 - 8 * TEXT_SIZE) / 2;

// display_epoch_us = esp_timer_get_time() + g_offset_us.
//
// The board does no timezone arithmetic at all: the host sends the epoch it wants
// *shown*, already shifted. That keeps the DST question on the machine that has a
// tz database, and it is also the number analysis needs as --display-utc-offset.
static bool     g_seeded    = false;
static int64_t  g_offset_us = 0;
static uint32_t g_seed_gen  = 0;   // bumped on every seed; aborts a wait in flight

// The repaint's cost, measured rather than assumed -- it is what the paint is
// scheduled around. See the comment on the wait in loop().
static uint32_t g_lead_us   = 0;
static uint32_t g_draw_min  = UINT32_MAX;
static uint32_t g_draw_max  = 0;
static int32_t  g_late_last = 0;   // signed: how far past the boundary the paint ended
static int32_t  g_late_min  = INT32_MAX;
static int32_t  g_late_max  = INT32_MIN;
// The first paint after a seed is not evidence about the schedule. A seed that lands
// inside the lead window leaves the target already in the past, so that one paint is
// late by about a whole draw and self-corrects on the next second. Counting it would
// leave g_late_max permanently reading the transient instead of the steady state.
static bool     g_first_paint = true;
// The last boundary actually painted. A paint that lands a hair EARLY leaves the
// clock still inside the second it just drew, so the next pass computes the same
// boundary again, finds its target already gone, and repaints the same value ~12 ms
// late -- invisible on the glass, but it is a wasted paint and it was what put a
// 12 ms outlier into late max. The next boundary is the next one, by construction.
static int64_t  g_last_boundary = 0;
static uint32_t g_paints    = 0;
static bool     g_status    = false;

static inline int64_t now_us()
{
    return esp_timer_get_time() + g_offset_us;
}

// Every character, every time, over its own background. Adafruit_GFX has no change
// detection, so print() with a background colour redraws all eight cells whether or
// not they differ -- which here is the property we want, not a cost: a repaint whose
// duration depends on how many digits happened to change is a repaint you cannot
// schedule against. This one is a constant, and the numbers in status() say so.
static void draw_clock(int32_t sod, bool valid)
{
    char buf[9];
    if (valid) {
        snprintf(buf, sizeof buf, "%02d:%02d:%02d",
                 (int)(sod / 3600), (int)((sod / 60) % 60), (int)(sod % 60));
    } else {
        memcpy(buf, "--:--:--", 9);
    }
    tft.setTextWrap(false);
    tft.setTextSize(TEXT_SIZE);
    tft.setTextColor(valid ? ST77XX_WHITE : ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(TEXT_X, TEXT_Y);
    tft.print(buf);
}

static void status()
{
    Serial.printf("clk %s off=%lld us | draw last %lu min %lu max %lu us | "
                  "late last %ld min %ld max %ld us | paints %lu\n",
                  g_seeded ? "seeded" : "UNSEEDED", (long long)g_offset_us,
                  (unsigned long)g_lead_us,
                  (unsigned long)(g_draw_min == UINT32_MAX ? 0 : g_draw_min),
                  (unsigned long)g_draw_max,
                  (long)g_late_last,
                  (long)(g_late_min == INT32_MAX ? 0 : g_late_min),
                  (long)(g_late_max == INT32_MIN ? 0 : g_late_max),
                  (unsigned long)g_paints);
}

// Line protocol, because seeding needs arguments -- unlike bluecan_bench, whose
// single-character commands are the right shape for toggles and the wrong one here.
//
//   T                      -> "t <local_us>"   monotonic, for the round trip
//   S <local_us> <ep_us>   -> "s ok ..."       map that instant to that epoch
//   ?                      -> status line
//   v / V                  -> 1 Hz status on / off  (lower on, upper off, as bench)
//   z                      -> zero the draw/late statistics (the clock is untouched)
//
// The split between T and S is what keeps the seed accurate: S refers back to the
// instant T answered, so however long S itself takes to arrive is irrelevant. Only
// the T round trip enters the error, and the host sends several and keeps the
// fastest.
static void handle_line(char* line)
{
    switch (line[0]) {
    case 'T':
    case 't':
        Serial.printf("t %lld\n", (long long)esp_timer_get_time());
        break;

    case 'S':
    case 's': {
        char* end = nullptr;
        int64_t local_us = strtoll(line + 1, &end, 10);
        if (end == line + 1) { Serial.println("s err no local_us"); break; }
        char* end2 = nullptr;
        int64_t epoch_us = strtoll(end, &end2, 10);
        if (end2 == end) { Serial.println("s err no epoch_us"); break; }

        g_offset_us = epoch_us - local_us;
        g_seeded = true;
        g_seed_gen++;
        g_first_paint = true;
        g_last_boundary = 0;
        Serial.printf("s ok gen %lu off %lld us\n",
                      (unsigned long)g_seed_gen, (long long)g_offset_us);
        break;
    }

    case 'v':
    case 'V':
        g_status = (line[0] == 'v');
        Serial.printf(">> status %s\n", g_status ? "on" : "off");
        break;

    case 'z':
        // Zero the instrument, not the clock. Seeding leaves one transient paint in
        // the record when it lands inside the lead window; this is how a session
        // gets a clean steady-state number out of the board.
        g_draw_min = UINT32_MAX;
        g_draw_max = 0;
        g_late_min = INT32_MAX;
        g_late_max = INT32_MIN;
        g_paints = 0;
        Serial.println(">> stats zeroed");
        break;

    case '?':
        status();
        break;

    default:
        break;  // stray bytes are not errors
    }
}

static void handle_serial()
{
    static char buf[64];
    static size_t len = 0;

    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (len) { buf[len] = '\0'; handle_line(buf); len = 0; }
        } else if (len < sizeof buf - 1) {
            buf[len++] = (char)c;
        } else {
            len = 0;  // overlong line: drop it rather than truncate into a command
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
    // A CDC write must never be able to stall the paint. The board spends its
    // working life unplugged with nothing reading the port, and Bluecan already
    // paid for learning what a blocking Serial.write() costs a timed task.
    Serial.setTxTimeoutMs(5);

    // Warm the estimate before the first real second: GFX's first paint into a
    // fresh window is the slow one, and it must not be the one the schedule is
    // built on.
    uint32_t t0 = micros();
    draw_clock(0, false);
    g_lead_us = micros() - t0;
}

void loop()
{
    handle_serial();

    if (!g_seeded) {
        static uint32_t last = 0;
        if (millis() - last >= 500) {
            last = millis();
            draw_clock(0, false);
        }
        return;
    }

    // Phase the paint onto the second, do not merely correct its value.
    //
    // The naive version repaints on a free-running period and predates the value by
    // the draw time. That fixes the draw time and leaves the period: the digit still
    // flips somewhere in the 0..period window after the true second, uniformly, and
    // on this panel the draw is single-digit ms while a period is hundreds. So the
    // period is the term worth removing, and the way to remove it is to sleep until
    // (boundary - measured draw) and paint into the boundary.
    //
    // The lead is the *whole* repaint, not half of it: the digits that actually
    // change are the rightmost ones, drawn last, so the new reading is complete only
    // when the paint is. g_late_* records how well that landed.
    const uint32_t gen = g_seed_gen;
    int64_t boundary = (now_us() / 1000000 + 1) * 1000000;
    if (g_last_boundary && boundary <= g_last_boundary) {
        boundary = g_last_boundary + 1000000;
    }
    const int64_t target = boundary - (int64_t)g_lead_us;

    while (true) {
        handle_serial();
        // A seed lands mid-wait and the boundary we computed is meaningless. Bail
        // out and let the next pass recompute against the clock we now have.
        if (g_seed_gen != gen || !g_seeded) return;
        int64_t left = target - now_us();
        if (left <= 0) break;
        if (left > 3000) delay(1);  // yield the bulk, spin only the last few ms
    }

    const uint32_t t0 = micros();
    draw_clock((int32_t)((boundary / 1000000) % 86400), true);
    const uint32_t d = micros() - t0;

    g_lead_us = d;
    if (d < g_draw_min) g_draw_min = d;
    if (d > g_draw_max) g_draw_max = d;

    g_last_boundary = boundary;
    g_late_last = (int32_t)(now_us() - boundary);
    if (g_first_paint) {
        g_first_paint = false;
    } else {
        if (g_late_last < g_late_min) g_late_min = g_late_last;
        if (g_late_last > g_late_max) g_late_max = g_late_last;
    }
    g_paints++;

    if (g_status) status();
}
