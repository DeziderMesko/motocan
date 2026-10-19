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

alignas(4) uint8_t ring[CONFIG_SDLOG_BUFFER];

// FatFs keeps its long-file-name buffer on the stack in this core's configuration.
constexpr uint32_t stack_size = 6 * 1024;
StaticTask_t task_buffer;
StackType_t task_stack[stack_size];

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
    : _head(0U)
    , _tail(0U)
    , _dropped(0U)
    , _peak(0U)
    , _sectors(0U)
    , _worst_us(0U)
    , _errors(0U)
    , _mode(mode_off)
    , _stop(false)
    , _stopped(false)
    , _handle(nullptr)
    , _charge(nullptr)
    , _sync_timer{}
#if defined(DEBUG)
    , _stats_timer{}
#endif
{
}

bool sdlog::start() noexcept
{
    bootln("SD log starting...");

    sd_spi.begin(SDLOG_SCK, SDLOG_MISO, SDLOG_MOSI, SDLOG_CS);

    // One file open at a time is all this needs, and each one FatFs is allowed costs
    // a FIL, sector buffer included, whether it is used or not.
    // -1, a board with no chip select, reaches the library as 255, and its writes to
    // that pin do nothing -- which is what M5Stack's example relies on as well.
    if (SD.begin(SDLOG_CS, sd_spi, CONFIG_SDLOG_SPI_HZ, "/sd", 1))
    {
        char name[24];
        for (unsigned n = 0U; n < 10000U; n++)
        {
            snprintf(name, sizeof(name), "/bluecan-%04u.bin", n);
            if (!SD.exists(name))
            {
                break;
            }
        }
        file = SD.open(name, FILE_WRITE);
        if (file)
        {
            _mode = mode_card;
            bootln("SD log: writing %s", name);
        }
        else
        {
            errorln("ERROR: SD log could not open %s", name);
        }
    }

#if defined(CONFIG_SDLOG_EMULATE)
    if (_mode == mode_off)
    {
        SD.end();
        // A mounted volume and one open file: FatFs's FATFS and FIL, each with a
        // sector buffer of its own in this configuration, plus the card's handle.
        _charge = malloc(sizeof(FATFS) + sizeof(FIL) + 64U);
        if (_charge != nullptr)
        {
            memset(_charge, 0, sizeof(FATFS) + sizeof(FIL) + 64U);
        }
        // SD.end() released the bus with the volume; take it back for the emulation.
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

    if (_mode == mode_off)
    {
        warnln("SD log: no card, nothing will be logged");
        return false;
    }

    // Level with loop(), which spins on core 1: the two share the core by time slice,
    // and a card that busy-polls for a quarter of a second does not hold back the bus
    // supervisor in loop() for all of it.
    _handle = xTaskCreateStaticPinnedToCore(
        task,
        "sdlog",
        stack_size,
        this,
        tskIDLE_PRIORITY + 1,
        task_stack,
        &task_buffer,
        CONFIG_SDLOG_CORE
    );

    if (_handle == nullptr)
    {
        errorln("ERROR: SD log task creation failed!");
        _mode = mode_off;
        return false;
    }

    bootln("SD log started!");
    return true;
}

void sdlog::note(canbus::frame const& f) noexcept
{
    if (RCUNLIKELY(_mode == mode_off))
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
    bool dirty = false;

    while (true)
    {
        if (_stop.load(std::memory_order_acquire))
        {
            write_rest();
            _stopped.store(true, std::memory_order_release);
            vTaskSuspend(nullptr);
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
            if (write_sector(ring + (tail & mask)))
            {
                _sectors++;
                dirty = true;
            }
            else
            {
                _errors++;
            }
            const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
            if (us > _worst_us)
            {
                _worst_us = us;
            }
            // Written or not, the sector is given up: a card that fails a write is
            // not going to take the next one any better, and the ring must not jam.
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

void sdlog::stop() noexcept
{
    if (_mode == mode_off || _handle == nullptr)
    {
        return;
    }

    _stop.store(true, std::memory_order_release);
    for (int i = 0; i < 100 && !_stopped.load(std::memory_order_acquire); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void sdlog::write_rest() noexcept
{
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
        file.close();
        SD.end();
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
            infoln("            SD log: %s, %lu sectors, %lu errors, worst %lu us",
                   _mode == mode_card ? "card" : "emulated",
                   (unsigned long)_sectors, (unsigned long)_errors,
                   (unsigned long)_worst_us);
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
