/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>

/* ---- PLC input pins ---------------------------------------------------- */

constexpr int STICK_COUNT     = 4;
constexpr int STICK1_PIN      = 0;
constexpr int STICK2_PIN      = 1;
constexpr int STICK3_PIN      = 2;
constexpr int STICK4_PIN      = 3;
constexpr int CLAW_BUTTON_PIN = 4;
constexpr int START_PIN       = 5;

constexpr int PLC_INPUT_COUNT = 8;
constexpr int PLC_RELAY_COUNT = 4;

// enable this to test without the AC module
//#define DISABLE_AC

/* Stick index (0..3) -> PLC input pin. Single source of truth: both MachineIO
 * and StickMapper use this instead of keeping their own tables. */
inline int stickPin(int index)
{
    constexpr int pins[STICK_COUNT] = {STICK1_PIN, STICK2_PIN, STICK3_PIN, STICK4_PIN};
    return pins[index];
}

/* ---- Timings (ms) ------------------------------------------------------ */

constexpr unsigned long RELAY_ON_TIME             = 1000;
constexpr unsigned long RUNNING_TIMEOUT           = 50000;
constexpr unsigned long START_DEBOUNCE_MS         = 200;
constexpr unsigned long SCREEN_HOLD_MS            = 2000;
constexpr unsigned long STICK_RELEASE_DEBOUNCE_MS = 50;
constexpr unsigned long IO_POLL_INTERVAL_MS       = 50;
constexpr unsigned long CLOCK_UPDATE_INTERVAL_MS  = 1000;

/* ---- Serial ------------------------------------------------------------ */

constexpr unsigned long SERIAL_BAUD = 115200;

/* Set to 1 to render RTC time/date on the dashboard. Disabled to match the
 * previous behavior, where update_time_and_date() existed but was never called. */
#define ENABLE_CLOCK_DISPLAY 0

/* ---- Tablet wire protocol ---------------------------------------------- */

namespace Protocol {
/* Tablet -> PLC */
constexpr const char* CMD_COIN = "coin";

/* PLC -> tablet */
constexpr const char* START_BUTTON = "0";
constexpr const char* READY        = "ready";
constexpr const char* START        = "start";
constexpr const char* CLAW         = "claw";
}  // namespace Protocol
