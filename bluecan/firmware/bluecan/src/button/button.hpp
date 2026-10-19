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

#include <cstdint>

/// seconds the button must be held to switch between the normal and the Wi-Fi mode
#ifndef CONFIG_BUTTON_HOLD_S
#define CONFIG_BUTTON_HOLD_S 5
#endif

/// The board's one button for the mode switch, BUTTON_PIN in the board file: the Lite's
/// G41, the Feather's D2. Held for CONFIG_BUTTON_HOLD_S and let go, it asks for the
/// other mode. Letting go is the trigger, not the fifth second, so that the board does
/// not restart under a finger still on the button; the status light says when it is
/// time to let go. A short press does nothing here. On the Feather with its display, D2
/// also freezes the screen, as it always has.
///
/// Read from loop() on core 1, every 10 ms. Standby reads the same pin to wake the
/// chip; that needs nothing from here.
namespace button
{

#if defined(BUTTON_PIN)
/**
 * From setup(): the pin as an input, pulled the way it is not pressed.
 */
void start() noexcept;

/**
 * From loop(), every pass.
 * @return true once, as the button is let go after a long enough hold
 */
bool poll() noexcept;

/**
 * True while the button is down and has been for long enough: let go now to switch.
 * For the status light. Any task.
 */
bool ready() noexcept;
#else
inline void start() noexcept {}
inline bool poll() noexcept { return false; }
inline bool ready() noexcept { return false; }
#endif

} // namespace button
