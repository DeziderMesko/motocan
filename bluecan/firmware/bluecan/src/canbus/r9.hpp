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

#include <cstdint>

namespace canbus
{

/**
 * What the Yamaha R9 (2026) sends on its diagnostic connector, as far as it has been
 * decoded. Measured, not from Yamaha: the evidence is motocan/analysis/r9-signals.md and
 * the machine-readable form is motocan/analysis/r9.dbc, which is the one to believe if
 * this file and it ever disagree. motocan's tools/r9-decode-check.py holds every function
 * here to that DBC over the captures.
 *
 * This is for the panel only. RaceChrono still gets raw frames and decodes them itself,
 * from equations entered in the app, so nothing here is on the path to the phone.
 *
 * Every function takes the frame's data bytes and reads only the ones its frame has:
 * 0x27C is seven bytes long and 0x512 four. Results are integers -- rpm, tenths of a
 * degree, tenths of a percent -- because that is what the panel prints.
 *
 * Deliberately not here: the fields that change and have no name. A field gets a
 * function once something independent has agreed with it.
 */
namespace r9
{

constexpr uint32_t id_engine = 0x27C;     //!< engine speed, gear, TCS, the mode field; 100 Hz
constexpr uint32_t id_temps = 0x512;      //!< water and air temperature; 1 Hz
constexpr uint32_t id_throttle = 0x758;   //!< the grip, and the throttle valve as measured; 20 Hz
constexpr uint32_t id_valve_cmd = 0x759;  //!< the throttle valve as commanded; 20 Hz

/// what gear() returns between gears
constexpr uint8_t gear_between = 7U;

// --- 0x27C

/// engine speed in rpm. Bytes 0-1, big-endian, in units of 100 rpm with eight bits of
/// fraction: byte 0 is the whole hundreds.
constexpr uint32_t rpm(const uint8_t* d) noexcept
{
    return (((static_cast<uint32_t>(d[0]) << 8) | d[1]) * 100U) / 256U;
}

/// 0 in neutral, 1-6, and gear_between while the box is between two of them
constexpr uint8_t gear(const uint8_t* d) noexcept
{
    return d[4] & 0x07U;
}

/// the TCS setting, as the dash shows it. Bits 1-4 of byte 3: bit 5 above them is set
/// in gear with the engine running, and without the mask TCS 1 reads 17 on the road.
constexpr uint8_t tcs(const uint8_t* d) noexcept
{
    return (d[3] >> 1) & 0x0FU;
}

/// The PWR setting less one: PWR 1 reads 0, PWR 4 reads 3. Passed on as the number the
/// bike sends, so it reads one less than the dash.
constexpr uint8_t mode(const uint8_t* d) noexcept
{
    return d[4] >> 6;
}

// --- 0x512

/// both temperatures share one equation, raw * 0.625 - 30 degrees C; in tenths here
constexpr int16_t temp_tenths(uint8_t raw) noexcept
{
    return static_cast<int16_t>((raw * 25) / 4 - 300);
}

/// coolant, tenths of a degree C. Within 2 degrees of the dash from 17 to 76.
constexpr int16_t water_tenths(const uint8_t* d) noexcept
{
    return temp_tenths(d[0]);
}

/// intake air, tenths of a degree C. The dash shows a degree or two less.
constexpr int16_t air_tenths(const uint8_t* d) noexcept
{
    return temp_tenths(d[1]);
}

// --- 0x758

/// The throttle grip, tenths of a percent of its travel. 15 bits spread over the low
/// nibbles of bytes 7 and 6 and all of byte 5; 31672 at the stop. It reads 0 for the
/// first twentieth of the travel, and a little over 1000 is possible on paper.
constexpr uint16_t grip_tenths(const uint8_t* d) noexcept
{
    const uint32_t raw = (static_cast<uint32_t>(d[7] & 0x0FU) << 12)
                       | (static_cast<uint32_t>(d[6] & 0x0FU) << 8)
                       | d[5];
    return static_cast<uint16_t>((raw * 1000U) / 31672U);
}

/// The throttle valve as measured, tenths of a percent. 12 bits: the high nibble of
/// byte 0 over byte 2, in sixteenths of the unit 0x759 uses, 144.4 of those at the top.
/// The zero is provisional -- a warm idle reads about 8 percent.
constexpr uint16_t valve_tenths(const uint8_t* d) noexcept
{
    const uint32_t raw = (static_cast<uint32_t>(d[0] >> 4) << 8) | d[2];
    return static_cast<uint16_t>((raw * 10000U) / (16U * 1444U));
}

// --- 0x759

/// The throttle valve as commanded, tenths of a percent. 8 bits: the high nibbles of
/// bytes 0 and 2. Leads valve_tenths() by one sample.
constexpr uint16_t valve_cmd_tenths(const uint8_t* d) noexcept
{
    const uint32_t raw = (static_cast<uint32_t>(d[0] >> 4) << 4) | (d[2] >> 4);
    return static_cast<uint16_t>((raw * 10000U) / 1444U);
}

} // namespace r9

} // namespace canbus
