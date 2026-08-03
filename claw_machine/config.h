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

/* Prize sensor. Read directly, never through stickPin(). */
constexpr int PRIZE_SENSOR_PIN = 7;

/* Highest PLC input the web panel is allowed to drive. The prize sensor sits
 * above it on purpose: a virtual win would be a cheat vector. */
constexpr int WEB_MAX_INPUT_PIN = START_PIN;

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

/* How long WaitPrize samples the prize sensor before declaring a loss. */
constexpr unsigned long PRIZE_WAIT_MS = 5000;

/* A virtual (web) input expires this long after the last press message, so a
 * client that vanishes mid-press cannot latch a control on. */
constexpr unsigned long VIRTUAL_INPUT_TTL_MS = 400;

/* ---- CAN link to tablet bridge ------------------------------------------
 *
 * The StamPLC no longer talks directly to the tablet over Serial. It talks
 * CAN to an Arduino Nano (see nano_can_bridge/), which relays bytes to/from
 * the tablet over its own Serial connection. These IDs are shared with that
 * sketch -- keep them identical on both sides, they are not negotiated. */

namespace CanBus {
constexpr uint32_t ID_PLC_TO_TABLET = 0x100;
constexpr uint32_t ID_TABLET_TO_PLC = 0x101;
}  // namespace CanBus

/* Renders RTC time/date on the dashboard. Enabled now that the clock can be set
 * from the web panel -- without a way to correct it, a drifted RTC on screen was
 * worse than no clock. */
#define ENABLE_CLOCK_DISPLAY 1

/* ---- Web control panel ------------------------------------------------- */

constexpr const char* AP_SSID         = "DBAdmin";
constexpr const char* AP_PASSWORD     = "31773177db";
constexpr uint16_t    WEB_SERVER_PORT = 80;

/* Mirrored by hand in web_page.h -- the JavaScript cannot read these. The hold
 * repeat must stay comfortably below VIRTUAL_INPUT_TTL_MS or a held button will
 * expire under the user's finger. */
constexpr unsigned long WEB_INPUT_REFRESH_MS = 200;
constexpr unsigned long WEB_STATUS_POLL_MS   = 500;

/* ---- Game history ------------------------------------------------------ */

constexpr const char* HISTORY_CSV_PATH   = "/historico.csv";
constexpr const char* HISTORY_CSV_HEADER = "GAME_ID,DATA,HORA,TEMPO,RESULTADO";
constexpr const char* NVS_NAMESPACE      = "arcade";
constexpr const char* NVS_KEY_GAME_ID    = "gameId";

/* ---- Tablet wire protocol ---------------------------------------------- */

namespace Protocol {
/* Tablet -> PLC */
constexpr const char* CMD_COIN     = "coin";
constexpr const char* CMD_FREEPLAY = "freeplay";

/* PLC -> tablet.
 *
 * START_BUTTON was "0" through the modularization. The tablet app was updated in
 * parallel with monolito_v02 and now expects "ready" on the start button, so the
 * two share a string. They remain distinct events: START_BUTTON fires on the
 * debounced rising edge of the physical/virtual start input, READY on entry to
 * the Screen and Freeplay states. */
constexpr const char* START_BUTTON = "ready";
constexpr const char* READY        = "ready";
constexpr const char* START        = "start";
constexpr const char* CLAW         = "claw";
}  // namespace Protocol
