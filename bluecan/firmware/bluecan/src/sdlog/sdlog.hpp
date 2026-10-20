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
#include "../canbus/frame.hpp"

#include <atomic>
#include <cstdint>

/// CONFIG_SDLOG, set by a board file that gives the card's pins, builds in a log of
/// every received frame to a microSD card on SPI, as on M5Stack's Atomic TF-Card Base.
/// The CAN interrupt copies each standard data frame into a ring in RAM before the app's
/// filter sees it, and a task of its own writes the ring out in 4096-byte sectors, so a
/// card that stalls costs ring space, never a frame on its way to the phone.
///
/// Whether a card is there is found out once, at start-up. Without one there is no
/// ring and no task: the board carries the code and nothing else. A card put in later
/// is found at the next start, which on the bike is the next wake. The look tells four
/// things apart, see sdlog::card: nothing on the pins, a socket with no card, a card
/// that does not mount, and one that does.
///
/// CONFIG_SDLOG_EMULATE stands in for a card that is not there: if none mounts, the
/// task still drives the SPI bus as the SD library would for every sector -- the same
/// bytes at the same clock, the same busy-polling while the card programs, a long
/// stall now and then -- and holds the heap a mounted card would. It is for measuring
/// what the card costs the rest of the board, on a board without one.
#if defined(CONFIG_SDLOG)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../utils/timer.hpp"

/// ring size in bytes; a multiple of 4096 and a power of two. 32 KB is 1365 frames,
/// about 2.7 s of the R9's 506 frames a second
#ifndef CONFIG_SDLOG_BUFFER
#define CONFIG_SDLOG_BUFFER 32768
#endif

/// SPI clock: M5Stack's example for the Atomic TF-Card Base runs it at 25 MHz. The SD
/// library's own default is 4 MHz
#ifndef CONFIG_SDLOG_SPI_HZ
#define CONFIG_SDLOG_SPI_HZ 25000000
#endif

/// Core 1 by default: core 0 has Bluetooth and the frame path to it, core 1 only the
/// CAN interrupt and loop(). The SD library busy-polls the card, so the core it runs
/// on is the core that pays for a slow card.
#ifndef CONFIG_SDLOG_CORE
#define CONFIG_SDLOG_CORE 1
#endif

/// how often the file's directory entry is brought up to date, in seconds
#ifndef CONFIG_SDLOG_SYNC_S
#define CONFIG_SDLOG_SYNC_S 5
#endif

/// how often a card closed after a failed write is mounted again, in seconds
#ifndef CONFIG_SDLOG_RETRY_S
#define CONFIG_SDLOG_RETRY_S 5
#endif

/// emulated card: time it busy-polls after each 512-byte block
#ifndef CONFIG_SDLOG_EMU_BLOCK_BUSY_US
#define CONFIG_SDLOG_EMU_BLOCK_BUSY_US 300
#endif

/// emulated card: a stall of this many ms, the SD specification's limit for a write...
#ifndef CONFIG_SDLOG_EMU_STALL_MS
#define CONFIG_SDLOG_EMU_STALL_MS 250
#endif

/// ...once every this many sectors
#ifndef CONFIG_SDLOG_EMU_STALL_EVERY
#define CONFIG_SDLOG_EMU_STALL_EVERY 64
#endif

#endif // CONFIG_SDLOG

namespace storage
{

/**
 * One frame as written to the card, 24 bytes, little-endian.
 */
struct record
{
    uint64_t t_us;   //!< esp_timer time the CAN interrupt read the frame, in microseconds
    uint32_t id;     //!< identifier; bit 31 set for a 29-bit one, as SocketCAN has it
    uint8_t dlc;     //!< data length
    uint8_t flags;   //!< bit 0: remote frame
    uint8_t pad[2];
    uint8_t data[8];
} __attribute__ ((__packed__));

static_assert(sizeof(record) == 24, "not 24 bytes");

class sdlog final
{
    CPP_NOCOPY(sdlog);
    CPP_NOMOVE(sdlog);

public:
    static sdlog& get() noexcept;

    ~sdlog() noexcept = default;

#if defined(CONFIG_SDLOG)
    /// what the last look for the card found
    enum class card : uint8_t
    {
        not_looked, //!< none yet
        no_base,    //!< nothing on the card pins: MISO is not pulled up
        empty,      //!< MISO pulled up, as the TF-Card Base does, but nothing answers
        unreadable, //!< a card answers, but no FAT file system mounts on it
        mounted,
    };

    /// a phrase for the console
    static const char* name(card c) noexcept;

    /// what the last look found: start()'s task, or mount_for_reading(). Read it from
    /// the task that looked
    card found() const noexcept { return _found; }

    /**
     * Start the writer task, which looks for the card first. With a card, or the
     * emulation, it takes the ring from the heap and logs; without one it ends, and
     * nothing is logged until the next start. Returns at once: the SD library takes
     * about 1.5 s to give up on a card that is not there.
     * @return false if the task could not be started
     */
    bool start() noexcept;

    /**
     * Copy one frame into the ring. Never blocks: a full ring counts a drop.
     * Single producer: the CAN interrupt on core 1.
     */
    void note(canbus::frame const& f) noexcept;

    /**
     * Write out what the ring holds, partial sector included, close the file and stop
     * the task. For a board about to lose its power or go to sleep. Waits up to three
     * seconds: closing a file is several single-block writes, each of which the
     * specification lets a card take 250 ms over (500 ms for SDXC), and the SD library
     * retries. Not longer, because it is called from loop(), which the task watchdog
     * restarts after five.
     */
    void stop() noexcept;

    /**
     * Mount the card to read its files, in place of start(), for the Wi-Fi mode: no
     * ring, no writer task, nothing written. The files are then the SD library's, under
     * "/". Tried once; later calls say what that found.
     * @return true if the card is mounted
     */
    bool mount_for_reading() noexcept;
#else
    bool start() noexcept { return true; }
    void note(canbus::frame const&) noexcept {}
    void stop() noexcept {}
#endif

private:
    explicit sdlog() noexcept;

#if defined(CONFIG_SDLOG)
    static void task(void* self);
    void run() noexcept;
    bool find_card() noexcept;
    card mount(uint8_t files) noexcept;
    bool open_card() noexcept;
    void close_card() noexcept;
    bool write_sector(uint8_t const* p) noexcept;
    void write_rest() noexcept;
    void sync() noexcept;
#if defined(DEBUG)
    void stats() noexcept;
#endif

    enum mode_t : uint8_t
    {
        mode_off,
        mode_card,
        mode_emulated,
    };

    /// the ring, taken from the heap once a card is found; null until then and without
    /// one. Published by the task after everything else is set, read by note()
    std::atomic<uint8_t*> _ring;
    std::atomic<uint32_t> _head;    //!< bytes ever put in, by note()
    std::atomic<uint32_t> _tail;    //!< bytes ever written out, by the task
    std::atomic<uint32_t> _dropped; //!< frames that found the ring full
    uint32_t _peak;                 //!< most bytes ever waiting
    uint32_t _sectors;              //!< sectors written
    uint32_t _worst_us;             //!< slowest sector write
    uint32_t _errors;               //!< failed writes
    uint32_t _reopens;              //!< files opened after the first attempt
    mode_t _mode;
    bool _open;                     //!< card mode: a file is open; the task's alone
    card _found;
    std::atomic<bool> _stop;        //!< asked by stop()
    std::atomic<bool> _stopped;     //!< answered by the task
    TaskHandle_t _handle;
    void* _charge;                  //!< emulation: the heap a mounted card would hold
    utils::timer _sync_timer;
    utils::timer _retry_timer;
#if defined(DEBUG)
    utils::timer _stats_timer;
#endif
#endif
};

} // namespace storage

extern storage::sdlog& SDLOG;
