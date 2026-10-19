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

// Combinations that cannot work, refused here at compile time rather than found out on
// the bike. Whatever a board file or the build script sets ends up checked here.

#if !defined(CAN_RX_PIN) || !defined(CAN_TX_PIN) || !defined(BLE_PWR_LVL)
#error "a board file gives CAN_RX_PIN, CAN_TX_PIN and BLE_PWR_LVL"
#endif

#if defined(CONFIG_SDLOG) && !(defined(SDLOG_SCK) && defined(SDLOG_MOSI) && defined(SDLOG_MISO) && defined(SDLOG_CS))
#error "CONFIG_SDLOG needs the board file to give the card's pins, SDLOG_SCK, SDLOG_MOSI, SDLOG_MISO and SDLOG_CS"
#endif

#if defined(CONFIG_SDLOG_EMULATE) && !defined(CONFIG_SDLOG)
#error "CONFIG_SDLOG_EMULATE stands in for the card of CONFIG_SDLOG; set both"
#endif

#if defined(CONFIG_STANDBY) && defined(CONFIG_DISPLAY_TFT)
#error "CONFIG_STANDBY does not put the display to sleep; build the atom or feather-as-atom profile"
#endif

#if defined(CONFIG_WIFI) && !defined(BUTTON_PIN)
#error "CONFIG_WIFI is entered by holding the board's button: the board file gives BUTTON_PIN, or build with CONFIG_NO_WIFI"
#endif

#if defined(BUTTON_PIN) && !defined(BUTTON_PRESSED)
#error "BUTTON_PIN needs BUTTON_PRESSED, the level the pin reads while the button is pressed"
#endif

#if defined(CONFIG_STATUS_LED) && !defined(STATUS_LED_PIN)
#error "CONFIG_STATUS_LED needs the board file to give the pixel's pin, STATUS_LED_PIN"
#endif

#if defined(STATUS_LED_POWER) && !defined(STATUS_LED_POWER_ON)
#error "STATUS_LED_POWER needs STATUS_LED_POWER_ON, the level that powers the pixel"
#endif
