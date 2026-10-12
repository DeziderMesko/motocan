// bluecan bring-up sketch
//
// Step 1 of the plan in ../../firmware.md: prove the toolchain, the board, the TFT
// and the ESP32-S3 TWAI controller *before* any BLE or RaceChrono code exists.
//
// The CAN self-test runs the controller in NO_ACK mode and transmits self-reception
// frames. Those still leave via the TX pin and come back in via the RX pin, so the
// signal has to get from one to the other somehow. Two ways to run it:
//
//   A) No transceiver, just a jumper wire from A1 (GPIO17) to A0 (GPIO18).
//      Tests the MCU side only. Do this first — it isolates toolchain and silicon
//      from any wiring mistake.
//   B) SN65HVD230 wired up per ../../hardware.md, with its 120R jumper fitted.
//      The module drives the bus and reads it back through its own receiver, so no
//      second node is needed. Also tests the transceiver's own driver and receiver.
//
// Board: Adafruit Feather ESP32-S3 Reverse TFT (#5691)
// FQBN:  esp32:esp32:adafruit_feather_esp32s3_reversetft

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/twai.h"

// Reverse TFT: display pins come from the board variant, buttons are D0/D1/D2.
// D0 is the BOOT strapping pin so it needs a pull-up: it idles HIGH and reads LOW
// when pressed. D1 and D2 need pull-downs, idle LOW and read HIGH. See
// ../../hardware.md -- and setup(), which is what the pin modes actually are.
static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_17;  // A1
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_18;  // A0

static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

static int line_y = 0;
static bool g_can_ok = false;
static uint32_t g_test_runs = 0;
static const char* g_fail_step = "not run";

static void screen(const char* title)
{
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(2);
    tft.println(title);
    tft.setTextSize(1);
    tft.println();
    line_y = tft.getCursorY();
}

static void report(const char* msg, bool ok)
{
    Serial.printf("[%s] %s\n", ok ? " OK " : "FAIL", msg);
    tft.setCursor(0, line_y);
    tft.setTextColor(ok ? ST77XX_GREEN : ST77XX_RED);
    tft.printf("%-5s %s\n", ok ? "OK" : "FAIL", msg);
    line_y = tft.getCursorY();
}

// The TWAI controller keeps error counters and a state machine. When a self test
// fails, those say far more about why than the pass/fail does: a climbing
// bus_error_count means what goes out on TX is not coming back in on RX, while
// tx_failed_count means the frame never left the queue at all.
static void dump_twai_status(const char* when)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) {
        Serial.printf("  status(%s): unavailable\n", when);
        return;
    }
    const char* state = st.state == TWAI_STATE_STOPPED  ? "STOPPED"
                      : st.state == TWAI_STATE_RUNNING  ? "RUNNING"
                      : st.state == TWAI_STATE_BUS_OFF  ? "BUS_OFF"
                      : st.state == TWAI_STATE_RECOVERING ? "RECOVERING" : "?";
    Serial.printf("  status(%s): %s tx_err=%lu rx_err=%lu tx_failed=%lu "
                  "rx_missed=%lu arb_lost=%lu bus_err=%lu msgs_to_rx=%lu\n",
                  when, state,
                  (unsigned long)st.tx_error_counter, (unsigned long)st.rx_error_counter,
                  (unsigned long)st.tx_failed_count, (unsigned long)st.rx_missed_count,
                  (unsigned long)st.arb_lost_count, (unsigned long)st.bus_error_count,
                  (unsigned long)st.msgs_to_rx);
}

// Electrical continuity check for the A0-A1 link, done as plain GPIO before the
// TWAI driver claims the pins. This separates "is the signal getting across" from
// "does the controller work" -- two failures that look identical from the CAN layer,
// and which the error counters turned out not to distinguish.
//
// It passes both with a bare jumper and with the KAmod in circuit, because the
// SN65HVD230 is non-inverting end to end: D low -> dominant -> R low, D high ->
// recessive -> R high. So the reading is the same either way, and the 200 us settle
// is three orders of magnitude more than the transceiver's ~100 ns loop delay.
//
// What a pass through the transceiver proves: the module is powered, out of standby,
// and TX/RX are on the right pins in both directions -- an unpowered or absent module
// leaves RX floating, which the pull-up reads HIGH, failing the drive-LOW half.
// What it does not prove: anything about CAN-H/CAN-L, the 120R jumper, or that there
// is a transceiver here at all rather than a plain short.
static bool jumper_test()
{
    pinMode(CAN_TX_PIN, OUTPUT);
    pinMode(CAN_RX_PIN, INPUT_PULLUP);   // pull up, so an open circuit reads HIGH

    bool ok = true;
    digitalWrite(CAN_TX_PIN, LOW);
    delayMicroseconds(200);
    bool saw_low = (digitalRead(CAN_RX_PIN) == LOW);

    digitalWrite(CAN_TX_PIN, HIGH);
    delayMicroseconds(200);
    bool saw_high = (digitalRead(CAN_RX_PIN) == HIGH);

    ok = saw_low && saw_high;
    Serial.printf("  jumper: drive LOW -> read %s, drive HIGH -> read %s\n",
                  saw_low ? "LOW" : "HIGH (open?)", saw_high ? "HIGH" : "LOW (shorted?)");

    // Hand the pins back before TWAI takes them over.
    pinMode(CAN_TX_PIN, INPUT);
    pinMode(CAN_RX_PIN, INPUT);
    return ok;
}

static bool can_self_test()
{
    g_fail_step = "none";
    // NO_ACK so a single node on the bus doesn't error out waiting for an ACK
    // from a peer that isn't there.
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  // R9 bus rate, per analysis/r9-signals.md
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        report("twai_driver_install", false);
        g_fail_step = "driver_install";
        return false;
    }
    report("twai_driver_install", true);

    if (twai_start() != ESP_OK) {
        report("twai_start", false);
        g_fail_step = "start";
        twai_driver_uninstall();
        return false;
    }
    report("twai_start", true);

    twai_message_t tx = {};
    tx.identifier = 0x236;  // the R9's highest-confidence candidate ID, as a mascot
    tx.data_length_code = 8;
    tx.self = 1;            // ask the controller to receive its own frame
    for (int i = 0; i < 8; i++) {
        tx.data[i] = 0xA0 + i;
    }

    dump_twai_status("before tx");

    // NB: ESP_OK here only means the frame was queued, not that it reached the bus.
    if (twai_transmit(&tx, pdMS_TO_TICKS(1000)) != ESP_OK) {
        report("twai_transmit", false);
        g_fail_step = "transmit";
        dump_twai_status("tx failed");
        twai_stop();
        twai_driver_uninstall();
        return false;
    }
    report("twai_transmit", true);

    twai_message_t rx = {};
    bool looped = (twai_receive(&rx, pdMS_TO_TICKS(1000)) == ESP_OK);
    if (looped) {
        looped = (rx.identifier == tx.identifier && rx.data_length_code == 8);
        for (int i = 0; i < 8 && looped; i++) {
            looped = (rx.data[i] == tx.data[i]);
        }
    }
    report("loopback rx", looped);
    dump_twai_status("after rx");
    if (!looped) {
        g_fail_step = "loopback";
        Serial.println("  -> nothing came back. Either TX never reached the bus, or it");
        Serial.println("     did and RX is not seeing it. The counters above tell which.");
    }

    twai_stop();
    twai_driver_uninstall();
    return looped;
}

// Runs the whole check and repaints the screen. Callable again at runtime so the
// transceiver or the A0-A1 jumper can be added without reflashing: press D1.
static void run_checks()
{
    g_test_runs++;

    screen("bluecan");
    Serial.printf("\n--- checks, run %lu ---\n", (unsigned long)g_test_runs);

    report("tft init", true);
    report(xPortGetCoreID() == 1 ? "arduino on core 1" : "arduino NOT on core 1",
           xPortGetCoreID() == 1);

    // Passes on a bare jumper and through the KAmod alike, and cannot tell those
    // two apart (see jumper_test), so it is reported rather than gating anything.
    report("A0-A1 continuity", jumper_test());

    g_can_ok = can_self_test();

    tft.setCursor(0, line_y + 6);
    tft.setTextColor(g_can_ok ? ST77XX_GREEN : ST77XX_YELLOW);
    tft.setTextSize(2);
    tft.println(g_can_ok ? "CAN READY" : "CAN FAIL");
    tft.setTextSize(1);
}

void setup()
{
    // Reverse TFT needs both the display power rail and the backlight enabled.
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);

    tft.init(135, 240);
    tft.setRotation(3);

    Serial.begin(115200);
    delay(2000);  // give USB CDC time to enumerate before the first printf

    pinMode(0, INPUT_PULLUP);    // D0: idles HIGH, pressed = LOW
    pinMode(1, INPUT_PULLDOWN);  // D1: idles LOW,  pressed = HIGH
    pinMode(2, INPUT_PULLDOWN);  // D2: idles LOW,  pressed = HIGH

    run_checks();
}

void loop()
{
    bool d0 = digitalRead(0) == LOW;    // inverted vs D1/D2, see setup()
    bool d1 = digitalRead(1) == HIGH;
    bool d2 = digitalRead(2) == HIGH;

    // D1 re-runs the checks. Lets the KAmod or a bare A0-A1 jumper be added to a
    // running board and retested immediately, instead of a reflash per attempt.
    static bool d1_prev = false;
    if (d1 && !d1_prev) {
        run_checks();
    }
    d1_prev = d1;

    // Button check: confirms the three buttons work before the sniffer UI depends
    // on them, and makes D0's inverted sense visible rather than assumed.
    static uint32_t last_tft = 0;
    if (millis() - last_tft > 200) {
        last_tft = millis();
        tft.setCursor(0, 118);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.printf("D0:%d D1:%d D2:%d  up %lus ", d0, d1, d2, millis() / 1000);
    }

    // Serial heartbeat. Without it the board is silent after boot, so anything
    // attaching to the port later sees nothing and can't tell a running sketch
    // from a dead one. Also the only way to observe the board over a link that
    // can't reset it -- which is the case for USB CDC through usbipd.
    static uint32_t last_beat = 0;
    if (millis() - last_beat > 1000) {
        last_beat = millis();
        Serial.printf("up %4lus | core %d | CAN %-5s | fail@%-14s | D0:%d D1:%d D2:%d\n",
                      millis() / 1000, xPortGetCoreID(),
                      g_can_ok ? "READY" : "FAIL", g_fail_step, d0, d1, d2);
    }

    // While the test is failing, retry it on a timer. During bring-up that means
    // wiring something up and watching it start working, with no button press and
    // no reflash -- which matters here because the host cannot reset this board.
    static uint32_t last_retry = 0;
    if (!g_can_ok && millis() - last_retry > 5000) {
        last_retry = millis();
        run_checks();
    }
}
