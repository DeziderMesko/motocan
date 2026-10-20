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
#include "../logging/logging.hpp"

#include "sdlog.hpp"

#if defined(CONFIG_SDLOG)
#include <cstdlib>
#include <cstring>

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_timer.h>
#include <ff.h>
#endif

storage::sdlog& SDLOG = storage::sdlog::get();

namespace storage
{

sdlog& sdlog::get() noexcept
{
    static sdlog instance;
    return instance;
}

#if !defined(CONFIG_SDLOG)

sdlog::sdlog() noexcept {}

#else // CONFIG_SDLOG

namespace
{

static_assert(CONFIG_SDLOG_BUFFER % 4096 == 0, "ring must hold whole sectors");
static_assert((CONFIG_SDLOG_BUFFER & (CONFIG_SDLOG_BUFFER - 1)) == 0, "ring must be a power of two");

constexpr uint32_t sector = 4096U;
constexpr uint32_t mask = CONFIG_SDLOG_BUFFER - 1U;

// FatFs keeps its long-file-name buffer on the stack in this core's configuration.
constexpr uint32_t stack_size = 6 * 1024;

SPIClass sd_spi(FSPI);
File file;

// Busy-poll the bus for `us`, as sdWait() in the SD library does while a card
// programs: clocking 0xFF until the card lets go of MISO.
void poll(uint32_t us) noexcept
{
    const int64_t end = esp_timer_get_time() + us;
    while (esp_timer_get_time() < end)
    {
        sd_spi.transfer(0xFF);
    }
}

// What AcquireSPI and sdSelectCard() do around each disk operation in the SD library.
struct emulated_op
{
    emulated_op() noexcept
    {
        sd_spi.beginTransaction(SPISettings(CONFIG_SDLOG_SPI_HZ, MSBFIRST, SPI_MODE0));
        if (SDLOG_CS >= 0)
        {
            digitalWrite(SDLOG_CS, LOW);
        }
    }
    ~emulated_op() noexcept
    {
        if (SDLOG_CS >= 0)
        {
            digitalWrite(SDLOG_CS, HIGH);
        }
        sd_spi.endTransaction();
    }
};

// One 512-byte block as sdWriteBytes() puts it on the bus: start token, data, CRC,
// and the card's data-response byte.
void emulate_block(uint8_t const* p) noexcept
{
    poll(CONFIG_SDLOG_EMU_BLOCK_BUSY_US);
    sd_spi.write(0xFC);
    sd_spi.writeBytes(p, 512);
    sd_spi.write16(0xFFFF);
    sd_spi.transfer(0xFF);
}

} // namespace

sdlog::sdlog() noexcept
    : _ring(nullptr)
    , _head(0U)
    , _tail(0U)
    , _dropped(0U)
    , _peak(0U)
    , _sectors(0U)
    , _worst_us(0U)
    , _errors(0U)
    , _reopens(0U)
    , _mode(mode_off)
    , _open(false)
    , _read_tried(false)
    , _reading(false)
    , _stop(false)
    , _stopped(false)
    , _handle(nullptr)
    , _charge(nullptr)
    , _sync_timer{}
    , _retry_timer{}
#if defined(DEBUG)
    , _stats_timer{}
#endif
{
}

bool sdlog::open_card() noexcept
{
    // One file open at a time is all this needs, and each one FatFs is allowed costs
    // a FIL, sector buffer included, whether it is used or not.
    // -1, a board with no chip select, reaches the library as 255, and its writes to
    // that pin do nothing -- which is what M5Stack's example relies on as well.
    if (!SD.begin(SDLOG_CS, sd_spi, CONFIG_SDLOG_SPI_HZ, "/sd", 1))
    {
        return false;
    }

    // The number after the highest there, found in one pass over the root directory.
    // Trying each name in turn opened every file on the card on every start, and
    // every wake adds one. Never a file that exists: FILE_WRITE truncates.
    unsigned next = 0U;
    File root = SD.open("/");
    if (root)
    {
        for (String entry = root.getNextFileName(); entry.length() > 0; entry = root.getNextFileName())
        {
            if (strncmp(entry.c_str(), "/bluecan-", 9) != 0)
            {
                continue;
            }
            const char* digits = entry.c_str() + 9;
            char* end = nullptr;
            const unsigned long n = strtoul(digits, &end, 10);
            if (end != digits && strcmp(end, ".bin") == 0 && n >= next)
            {
                next = n + 1U;
            }
        }
        root.close();
    }

    char name[24];
    snprintf(name, sizeof(name), "/bluecan-%04u.bin", next);
    if (next <= 9999U && !SD.exists(name))
    {
        file = SD.open(name, FILE_WRITE);
    }
    if (!file)
    {
        errorln("ERROR: SD log could not open %s", name);
        SD.end();
        return false;
    }

    bootln("SD log: writing %s", name);
    return true;
}

void sdlog::close_card() noexcept
{
    file.close();
    SD.end();
    _open = false;
}

bool sdlog::start() noexcept
{
    // Level with loop(), which spins on core 1: the two share the core by time slice,
    // and a card that busy-polls for a quarter of a second does not hold back the bus
    // supervisor in loop() for all of it. From the heap, so that a board without a card
    // gets the stack back when the task ends.
    if (xTaskCreatePinnedToCore(task, "sdlog", stack_size, this, tskIDLE_PRIORITY + 1,
                                &_handle, CONFIG_SDLOG_CORE) != pdPASS)
    {
        errorln("ERROR: SD log task creation failed!");
        _handle = nullptr;
        return false;
    }
    return true;
}

bool sdlog::find_card() noexcept
{
    bootln("SD log: looking for a card...");

    sd_spi.begin(SDLOG_SCK, SDLOG_MISO, SDLOG_MOSI, SDLOG_CS);

    _mode = mode_card;
    _open = open_card();

#if defined(CONFIG_SDLOG_EMULATE)
    if (!_open)
    {
        // A mounted volume and one open file: FatFs's FATFS and FIL, each with a
        // sector buffer of its own in this configuration, plus the card's handle.
        _charge = malloc(sizeof(FATFS) + sizeof(FIL) + 64U);
        if (_charge != nullptr)
        {
            memset(_charge, 0, sizeof(FATFS) + sizeof(FIL) + 64U);
        }
        // A failed SD.begin() released the bus; take it back for the emulation.
        sd_spi.begin(SDLOG_SCK, SDLOG_MISO, SDLOG_MOSI, SDLOG_CS);
        if (SDLOG_CS >= 0)
        {
            pinMode(SDLOG_CS, OUTPUT);
            digitalWrite(SDLOG_CS, HIGH);
        }
        _mode = mode_emulated;
        bootln("SD log: no card, emulating one (%u bytes of heap held)",
               (unsigned)(sizeof(FATFS) + sizeof(FIL) + 64U));
    }
#endif

    if (_mode == mode_card && !_open)
    {
        _mode = mode_off;
        bootln("SD log: no card, nothing is logged until the next start");
        return false;
    }

    uint8_t* ring = static_cast<uint8_t*>(malloc(CONFIG_SDLOG_BUFFER));
    if (ring == nullptr)
    {
        errorln("ERROR: SD log could not take %u bytes for its ring", (unsigned)CONFIG_SDLOG_BUFFER);
        if (_open)
        {
            close_card();
        }
        _mode = mode_off;
        return false;
    }

    // Last: from here on the CAN interrupt writes into it.
    _ring.store(ring, std::memory_order_release);
    bootln("SD log started!");
    return true;
}

void sdlog::note(canbus::frame const& f) noexcept
{
    uint8_t* const ring = _ring.load(std::memory_order_acquire);
    if (RCUNLIKELY(ring == nullptr))
    {
        return;
    }

    const uint32_t head = _head.load(std::memory_order_relaxed);
    const uint32_t tail = _tail.load(std::memory_order_acquire);

    if (RCUNLIKELY(CONFIG_SDLOG_BUFFER - (head - tail) < sizeof(record)))
    {
        _dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }

    record r;
    r.t_us = static_cast<uint64_t>(esp_timer_get_time());
    r.id = f.id | (f.info.frame_format == canbus::extended ? 0x80000000U : 0U);
    r.dlc = f.info.dlc;
    r.flags = f.info.rtr ? 1U : 0U;
    r.pad[0] = r.pad[1] = 0U;
    memcpy(r.data, f.data.u8, sizeof(r.data));

    // A record can straddle the end of the ring; the sectors the task takes cannot,
    // because the ring is a whole number of them.
    const uint32_t at = head & mask;
    const uint32_t first = (CONFIG_SDLOG_BUFFER - at) < sizeof(record) ? (CONFIG_SDLOG_BUFFER - at) : sizeof(record);
    memcpy(ring + at, &r, first);
    if (first < sizeof(record))
    {
        memcpy(ring, reinterpret_cast<uint8_t const*>(&r) + first, sizeof(record) - first);
    }

    _head.store(head + sizeof(record), std::memory_order_release);
}

void sdlog::task(void* self)
{
    static_cast<sdlog*>(self)->run();
}

void sdlog::run() noexcept
{
    if (!find_card())
    {
        _stopped.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    uint8_t* const ring = _ring.load(std::memory_order_relaxed);
    bool dirty = false;

    while (true)
    {
        if (_stop.load(std::memory_order_acquire))
        {
            write_rest();
            _stopped.store(true, std::memory_order_release);
            vTaskSuspend(nullptr);
        }

        // A card closed after a failed write is mounted again now and then, with a
        // new file each time. The ring goes on filling meanwhile, and counts what it
        // cannot hold.
        if (_mode == mode_card && !_open)
        {
            if (_retry_timer.elapsed(CONFIG_SDLOG_RETRY_S * 1000000UL) > 0UL && open_card())
            {
                _open = true;
                _reopens++;
            }
            if (!_open)
            {
#if defined(DEBUG)
                stats();
#endif
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }

        const uint32_t tail = _tail.load(std::memory_order_relaxed);
        const uint32_t waiting = _head.load(std::memory_order_acquire) - tail;

        if (waiting > _peak)
        {
            _peak = waiting;
        }

        if (waiting >= sector)
        {
            const int64_t t0 = esp_timer_get_time();
            const bool written = write_sector(ring + (tail & mask));
            const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
            if (us > _worst_us)
            {
                _worst_us = us;
            }
            if (!written)
            {
                // After a failed write the SD library's state is not to be trusted,
                // so the card is closed and mounted again rather than written to
                // blind. The sector stays in the ring and goes into the new file.
                _errors++;
                close_card();
                dirty = false;
                continue;
            }
            _sectors++;
            dirty = true;
            _tail.store(tail + sector, std::memory_order_release);
            continue;
        }

        if (dirty && _sync_timer.elapsed(CONFIG_SDLOG_SYNC_S * 1000000UL) > 0UL)
        {
            sync();
            dirty = false;
        }

#if defined(DEBUG)
        stats();
#endif

        // A sector fills in about 0.3 s at the R9's rate; 20 ms between looks is
        // well inside that and costs the core nothing.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool sdlog::write_sector(uint8_t const* p) noexcept
{
    if (_mode == mode_card)
    {
        return file.write(p, sector) == sector;
    }

    // Emulated: a multiple-block write of eight blocks, and every so often the long
    // busy a card takes when it has to erase.
    emulated_op op;
    for (uint32_t i = 0U; i < sector / 512U; i++)
    {
        emulate_block(p + i * 512U);
    }
    if ((_sectors + 1U) % CONFIG_SDLOG_EMU_STALL_EVERY == 0U)
    {
        poll(CONFIG_SDLOG_EMU_STALL_MS * 1000U);
    }
    return true;
}

bool sdlog::mount_for_reading() noexcept
{
    if (_read_tried)
    {
        return _reading;
    }
    _read_tried = true;

    // A directory being listed and a file being sent are two open at once.
    sd_spi.begin(SDLOG_SCK, SDLOG_MISO, SDLOG_MOSI, SDLOG_CS);
    _reading = SD.begin(SDLOG_CS, sd_spi, CONFIG_SDLOG_SPI_HZ, "/sd", 2);
    bootln("SD log: %s", _reading ? "card mounted for reading" : "no card");
    return _reading;
}

void sdlog::stop() noexcept
{
    // Also when the task is still looking for the card, or found none and ended:
    // either way it says so.
    if (_handle == nullptr)
    {
        return;
    }

    _stop.store(true, std::memory_order_release);
    for (int i = 0; i < 300 && !_stopped.load(std::memory_order_acquire); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void sdlog::write_rest() noexcept
{
    if (_mode == mode_card && !_open)
    {
        return;
    }

    uint8_t* const ring = _ring.load(std::memory_order_relaxed);

    // Whole sectors first, as the loop would have, then whatever is left of the last.
    const uint32_t head = _head.load(std::memory_order_acquire);
    uint32_t tail = _tail.load(std::memory_order_relaxed);

    while (head - tail >= sector)
    {
        write_sector(ring + (tail & mask));
        _sectors++;
        tail += sector;
    }

    // The rest is less than a sector and starts on a sector boundary, so it cannot
    // wrap.
    const uint32_t rest = head - tail;
    if (rest > 0U)
    {
        if (_mode == mode_card)
        {
            file.write(ring + (tail & mask), rest);
        }
        else
        {
            emulated_op op;
            for (uint32_t done = 0U; done < rest; done += 512U)
            {
                emulate_block(ring + ((tail + done) & mask));
            }
        }
        tail += rest;
    }
    _tail.store(tail, std::memory_order_release);

    if (_mode == mode_card)
    {
        close_card();
    }
    else
    {
        sync();
    }
}

void sdlog::sync() noexcept
{
    if (_mode == mode_card)
    {
        file.flush();
        return;
    }

    // Emulated: f_sync() rewrites the directory sector and a sector of the FAT.
    static uint8_t blank[512];
    emulated_op op;
    emulate_block(blank);
    emulate_block(blank);
}

#if defined(DEBUG)
void sdlog::stats() noexcept
{
    if (logging::logger::get().level() >= logging::log_level::info)
    {
        if (_stats_timer.elapsed(CONFIG_RC_STATS_TIMEOUT) > 0UL)
        {
            infoln("            SD log: %s, %lu sectors, %lu errors, %lu reopened, worst %lu us",
                   _mode == mode_emulated ? "emulated" : _open ? "card" : "no card",
                   (unsigned long)_sectors, (unsigned long)_errors,
                   (unsigned long)_reopens, (unsigned long)_worst_us);
            infoln("       SD log ring: %lu waiting, peak %lu of %u, %lu frames dropped",
                   (unsigned long)(_head.load(std::memory_order_relaxed) - _tail.load(std::memory_order_relaxed)),
                   (unsigned long)_peak, (unsigned)CONFIG_SDLOG_BUFFER,
                   (unsigned long)_dropped.load(std::memory_order_relaxed));
            infoln("      SD log stack: %lu bytes free",
                   (unsigned long)(_handle ? uxTaskGetStackHighWaterMark(_handle) : 0));
        }
    }
}
#endif

#endif // CONFIG_SDLOG

} // namespace storage
