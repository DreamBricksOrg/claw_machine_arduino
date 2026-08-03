# Monolito v02 Feature Port — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the six feature areas added in `monolito_v02.ino` — web control panel, virtual inputs, freeplay mode, prize detection, SD history, persistent game id — into the modular `claw_machine/` firmware, fixing the eleven defects catalogued in the design.

**Architecture:** Two new modules (`GameLog` owns SD + NVS, `WebPortal` owns WiFi + HTTP), a virtual-input layer inside the existing `MachineIO`, and two new states in `GameStateMachine`. `WebPortal` submits commands through the same `std::function<void(const char*)>` handler `TabletLink` uses, so it cannot write machine state — every state change still goes through `transitionTo()`.

**Tech Stack:** Arduino C++ (ESP32-S3), `M5StamPLC`, `M5GFX`, ESP32 `WiFi` / `WebServer` / `SD` / `Preferences`.

**Reference spec:** `docs/superpowers/specs/2026-08-03-monolito-v02-port-design.md`

## Global Constraints

- **No unit-test framework and no Arduino toolchain on this machine.** `arduino-cli` is not installed. Do not claim any task builds. Each task's gate is: (1) a self-review against the checks listed in that task, and (2) a **compile gate** — ask the developer to open `claw_machine/claw_machine.ino` in the Arduino IDE and click Verify, then report the result before moving on.
- **`MachineIO` is the sole owner of `M5StamPLC` / `M5StamPLC_AC`.** No other translation unit reads an input or writes a relay.
- **`TabletLink` is the sole owner of `Serial`.** No other translation unit calls `Serial.print*` or `Serial.read`.
- **`GameLog` is the sole owner of `SD` and `Preferences`.** No other translation unit includes `<SD.h>` or `<Preferences.h>`.
- **`WebPortal` is the sole owner of `WiFi` and `WebServer`.**
- **Nothing blocks `loop()`.** No `delay()` in any new or modified code. The only permitted exception is the existing retry loop in `MachineIO::begin()`.
- **Every state change goes through `transitionTo()`.** No direct writes to `_status` outside it.
- **String literals in source stay unaccented ASCII.** The existing console strings follow this (`"Mapeamento concluido!"`, `"Modo Config Iniciado"`); keep it for the new status strings too, so source encoding can never corrupt them in transit.
- **Copyright header** — every new file starts with the same block the existing files use:
  ```cpp
  /*
   * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
   *
   * SPDX-License-Identifier: MIT
   */
  ```
- **Tasks 1–5 are additive.** Task 1 flips two behavior switches (documented in that task); Tasks 2–5 add code that nothing calls yet. Task 6 and Task 7 are the switchovers.
- `monolito_v01.ino` and `monolito_v02.ino` are reference material at the repo root. They are not part of the sketch and must not be modified.

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `claw_machine/config.h` | Modify | All constants and the wire protocol. Gains prize sensor, web, and history constants. |
| `claw_machine/machine_io.h/.cpp` | Modify | Adds the virtual-input layer, the prize sensor, BtnA, and RTC writes. |
| `claw_machine/game_log.h/.cpp` | Create | SD card CSV history + NVS game counter. |
| `claw_machine/web_page.h` | Create | The HTML/CSS/JS control panel, as one PROGMEM literal. |
| `claw_machine/web_portal.h/.cpp` | Create | WiFi AP, HTTP routes, command forwarding. |
| `claw_machine/game_state.h/.cpp` | Modify | Adds `Freeplay` and `WaitPrize` states, freeplay toggle, game logging. |
| `claw_machine/claw_machine.ino` | Modify | Instantiates and wires the two new modules. |

---

### Task 1: Constants and protocol

**Files:**
- Modify: `claw_machine/config.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `PRIZE_SENSOR_PIN`, `PRIZE_WAIT_MS`, `VIRTUAL_INPUT_TTL_MS`, `WEB_INPUT_REFRESH_MS`, `WEB_STATUS_POLL_MS`, `WEB_MAX_INPUT_PIN`, `AP_SSID`, `AP_PASSWORD`, `WEB_SERVER_PORT`, `HISTORY_CSV_PATH`, `HISTORY_CSV_HEADER`, `NVS_NAMESPACE`, `NVS_KEY_GAME_ID`, `Protocol::CMD_FREEPLAY`. Changes `Protocol::START_BUTTON` to `"ready"` and `ENABLE_CLOCK_DISPLAY` to `1`.

**This task changes runtime behavior in two ways**, both intended and both listed in the design's decisions table:
1. The start button now sends `"ready"` instead of `"0"`. The tablet build must match.
2. The RTC clock appears on the dashboard.

- [ ] **Step 1: Add the new constant blocks to `config.h`**

Insert after the existing `PLC_RELAY_COUNT` line, before the `DISABLE_AC` comment:

```cpp
/* Prize sensor. Read directly, never through stickPin(). */
constexpr int PRIZE_SENSOR_PIN = 7;

/* Highest PLC input the web panel is allowed to drive. The prize sensor sits
 * above it on purpose: a virtual win would be a cheat vector. */
constexpr int WEB_MAX_INPUT_PIN = START_PIN;
```

Insert into the timings block, after `CLOCK_UPDATE_INTERVAL_MS`:

```cpp
/* How long WaitPrize samples the prize sensor before declaring a loss. */
constexpr unsigned long PRIZE_WAIT_MS = 5000;

/* A virtual (web) input expires this long after the last press message, so a
 * client that vanishes mid-press cannot latch a control on. */
constexpr unsigned long VIRTUAL_INPUT_TTL_MS = 400;
```

Add a new section after the `Serial` block:

```cpp
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
```

- [ ] **Step 2: Flip `ENABLE_CLOCK_DISPLAY` and update its comment**

Replace:

```cpp
/* Set to 1 to render RTC time/date on the dashboard. Disabled to match the
 * previous behavior, where update_time_and_date() existed but was never called. */
#define ENABLE_CLOCK_DISPLAY 0
```

with:

```cpp
/* Renders RTC time/date on the dashboard. Enabled now that the clock can be set
 * from the web panel -- without a way to correct it, a drifted RTC on screen was
 * worse than no clock. */
#define ENABLE_CLOCK_DISPLAY 1
```

- [ ] **Step 3: Change the protocol block**

Replace the `namespace Protocol` body with:

```cpp
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
```

- [ ] **Step 4: Self-review**

- `WEB_MAX_INPUT_PIN` is declared after `START_PIN`, which it references.
- `PRIZE_SENSOR_PIN` (7) is `< PLC_INPUT_COUNT` (8) and `> WEB_MAX_INPUT_PIN` (5).
- No constant is declared twice.

- [ ] **Step 5: Compile gate**

Ask the developer to Verify `claw_machine.ino` in the Arduino IDE. `config.h` is already included by every module, so this compiles the change immediately. Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add claw_machine/config.h
git commit -m "feat: constants for prize sensor, web panel and game history

Also switches Protocol::START_BUTTON to \"ready\" to match the updated tablet
build, and enables the dashboard clock now that it can be set from the web."
```

---

### Task 2: Virtual inputs, prize sensor, RTC writes in `MachineIO`

**Files:**
- Modify: `claw_machine/machine_io.h`
- Modify: `claw_machine/machine_io.cpp`

**Interfaces:**
- Consumes: `PRIZE_SENSOR_PIN`, `WEB_MAX_INPUT_PIN`, `VIRTUAL_INPUT_TTL_MS`, `PLC_INPUT_COUNT` from Task 1.
- Produces:
  - `void MachineIO::setVirtualInput(int pin, bool state)`
  - `void MachineIO::clearVirtualInputs()`
  - `bool MachineIO::virtualActive(int pin) const`
  - `bool MachineIO::prizeSensor() const`
  - `bool MachineIO::freeplayButtonClicked()`
  - `bool MachineIO::setRtcTime(const struct tm* t)`

Nothing calls the new methods yet, and no virtual input is ever set, so the merged readers return exactly what they returned before. This task is behavior-neutral.

- [ ] **Step 1: Add the declarations to `machine_io.h`**

Add `#include <time.h>` below `#include <array>`.

Add to the public section, after the `clawButton()` / `startJustPressed()` / `configButtonClicked()` group:

```cpp
    /* Front-panel BtnA -- toggles freeplay. */
    bool freeplayButtonClicked();

    /* Prize sensor. Physical read only: the virtual-input layer deliberately
     * cannot reach this pin, so a web client cannot fake a win. */
    bool prizeSensor() const;

    /* ---- Virtual (web) inputs ----
     *
     * The web panel presses a control by calling setVirtualInput(pin, true)
     * repeatedly while it is held. Each call stamps millis(); the stamp expires
     * VIRTUAL_INPUT_TTL_MS later. A client that drops off WiFi, backgrounds its
     * browser, or loses its release message therefore releases the control
     * instead of latching it on.
     *
     * Every input reader below ORs the physical pin with its virtual stamp, so
     * StickMapper and the configuration walkthrough get web support with no
     * changes of their own. Pins outside 0..WEB_MAX_INPUT_PIN are ignored. */
    void setVirtualInput(int pin, bool state);
    void clearVirtualInputs();
    bool virtualActive(int pin) const;

    /* Writes the RTC. Normalizes the caller's struct first -- see the .cpp. */
    bool setRtcTime(const struct tm* t);
```

Add to the private section, after `_relays`:

```cpp
    /* millis() of the last press per pin; 0 means "not pressed". */
    std::array<unsigned long, PLC_INPUT_COUNT> _virtualStamp{};
```

- [ ] **Step 2: Enable the SD reader in `MachineIO::begin()`**

Replace the opening of `begin()` in `machine_io.cpp`:

```cpp
void MachineIO::begin()
{
    /* The internal microSD reader must be enabled on the config before the
     * driver starts. GameLog mounts nothing itself; it relies on this. */
    auto config         = M5StamPLC.config();
    config.enableSdCard = true;
    M5StamPLC.config(config);

    M5StamPLC.begin();
```

The `#ifndef DISABLE_AC` retry block below it is unchanged.

- [ ] **Step 3: Implement the virtual-input layer**

Add to `machine_io.cpp`, after `updateButtonTones()`:

```cpp
void MachineIO::setVirtualInput(int pin, bool state)
{
    if (pin < 0 || pin > WEB_MAX_INPUT_PIN) {
        return;
    }

    if (!state) {
        _virtualStamp[pin] = 0;
        return;
    }

    unsigned long now = millis();
    /* 0 is the "not pressed" sentinel, so never store it as a timestamp. */
    _virtualStamp[pin] = (now == 0) ? 1 : now;
}

void MachineIO::clearVirtualInputs()
{
    _virtualStamp.fill(0);
}

bool MachineIO::virtualActive(int pin) const
{
    if (pin < 0 || pin >= PLC_INPUT_COUNT || _virtualStamp[pin] == 0) {
        return false;
    }
    /* Expiry is evaluated here rather than swept in poll(), so a stamp can never
     * be read after its deadline no matter when poll() last ran. */
    return millis() - _virtualStamp[pin] < VIRTUAL_INPUT_TTL_MS;
}
```

- [ ] **Step 4: Merge the virtual layer into the readers**

Replace the four existing readers in `machine_io.cpp`:

```cpp
bool MachineIO::stick(int index) const
{
    int pin = stickPin(index);
    return M5StamPLC.readPlcInput(pin) || virtualActive(pin);
}

bool MachineIO::rawInput(int pin) const
{
    return M5StamPLC.readPlcInput(pin) || virtualActive(pin);
}

bool MachineIO::clawButton() const
{
    return M5StamPLC.readPlcInput(CLAW_BUTTON_PIN) || virtualActive(CLAW_BUTTON_PIN);
}

bool MachineIO::startJustPressed()
{
    bool input = M5StamPLC.readPlcInput(START_PIN) || virtualActive(START_PIN);
    bool edge  = input && !_lastStartInput && (millis() - _lastStartEdge > START_DEBOUNCE_MS);

    if (edge) {
        _lastStartEdge = millis();
    }
    _lastStartInput = input;

    return edge;
}
```

And merge the dashboard snapshot inside `poll()` — replace the input loop only:

```cpp
    for (int i = 0; i < PLC_INPUT_COUNT; i++) {
        _inputs[i] = M5StamPLC.readPlcInput(i) || virtualActive(i);
    }
```

The relay loop below it is unchanged.

- [ ] **Step 5: Add the prize sensor, BtnA, and RTC write**

Add to `machine_io.cpp`, next to `configButtonClicked()`:

```cpp
bool MachineIO::freeplayButtonClicked()
{
    return M5StamPLC.BtnA.wasClicked();
}

bool MachineIO::prizeSensor() const
{
    return M5StamPLC.readPlcInput(PRIZE_SENSOR_PIN);
}
```

Add next to `getRtcTime()`:

```cpp
bool MachineIO::setRtcTime(const struct tm* t)
{
    if (t == nullptr) {
        return false;
    }

    /* The web sync handler fills only the six calendar fields. mktime() derives
     * tm_wday and tm_yday from those and normalizes any out-of-range value, so
     * the RTC never receives an indeterminate day-of-week.
     *
     * tm_isdst is pinned to 0 rather than -1: the phone sends local wall-clock
     * time and the RTC stores local wall-clock time, so letting mktime() guess a
     * DST offset could shift the hour by one. */
    struct tm normalized = *t;
    normalized.tm_isdst  = 0;

    if (mktime(&normalized) == (time_t)-1) {
        return false;
    }

    M5StamPLC.setRtcTime(&normalized);
    return true;
}
```

- [ ] **Step 6: Self-review**

- `virtualActive()` is `const` and is called from three `const` readers — it must not mutate `_virtualStamp`.
- `setVirtualInput` bounds-checks against `WEB_MAX_INPUT_PIN` (5), `virtualActive` against `PLC_INPUT_COUNT` (8). The asymmetry is deliberate: writes are restricted to the panel's controls, reads are safe for any pin.
- `prizeSensor()` calls `readPlcInput` directly with **no** `virtualActive` term. This is the cheat guard — verify it by eye.
- `startJustPressed()` still updates `_lastStartInput` unconditionally on every call.
- `M5StamPLC.config()` is called *before* `M5StamPLC.begin()`.

- [ ] **Step 7: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS. A failure in `M5StamPLC.config()` or `setRtcTime()` would mean the installed library version differs from what `monolito_v02.ino` was written against — report the exact error rather than working around it.

- [ ] **Step 8: Commit**

```bash
git add claw_machine/machine_io.h claw_machine/machine_io.cpp
git commit -m "feat: virtual web inputs, prize sensor and RTC writes in MachineIO

Virtual inputs expire VIRTUAL_INPUT_TTL_MS after the last press message so a
web client that vanishes mid-press cannot latch a control on. The prize sensor
is physical-only: a virtual win would be a cheat vector."
```

---

### Task 3: `GameLog` — SD history and persistent game id

**Files:**
- Create: `claw_machine/game_log.h`
- Create: `claw_machine/game_log.cpp`

**Interfaces:**
- Consumes: `HISTORY_CSV_PATH`, `HISTORY_CSV_HEADER`, `NVS_NAMESPACE`, `NVS_KEY_GAME_ID` from Task 1; `MachineIO::getRtcTime()` (already exists).
- Produces:
  - `GameLog::GameLog(MachineIO& io)`
  - `void GameLog::begin()`
  - `bool GameLog::available() const`
  - `uint32_t GameLog::nextGameId() const`
  - `bool GameLog::record(unsigned long durationMs, bool won)`
  - `File GameLog::openForRead()`

Nothing constructs a `GameLog` yet. The Arduino IDE compiles every `.cpp` in the sketch folder, so the compile gate still exercises this file.

- [ ] **Step 1: Write `game_log.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include "config.h"
#include "machine_io.h"

/* Sole owner of the SD card and of NVS. No other translation unit includes
 * <SD.h> or <Preferences.h>.
 *
 * Every failure here is non-fatal by design: a machine with no card, or with a
 * card that has gone read-only, must still take coins and play. */
class GameLog {
public:
    explicit GameLog(MachineIO& io) : _io(io) {}

    /* Loads the persisted game id and makes sure the CSV exists with its header
     * row. Call AFTER MachineIO::begin(), which is what enables the SD reader. */
    void begin();

    /* False when the card is missing or unwritable. record() is a no-op and the
     * download route reports 404. */
    bool available() const { return _available; }

    /* The id the next successful record() will use. */
    uint32_t nextGameId() const { return _gameId; }

    /* Appends one row. Increments and persists the game id ONLY on a successful
     * write, so a failed write leaves no gap in the numbering. Returns whether
     * the row reached the card. */
    bool record(unsigned long durationMs, bool won);

    /* Opens the CSV for reading. Returns an invalid File when unavailable; the
     * caller must test it and is responsible for closing it. */
    File openForRead();

private:
    MachineIO&  _io;
    Preferences _prefs;
    bool        _available = false;
    uint32_t    _gameId    = 1;
};
```

- [ ] **Step 2: Write `game_log.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "game_log.h"

void GameLog::begin()
{
    /* NVS lives in internal flash, independent of the card: the id keeps
     * advancing even across a period with no card fitted. */
    if (_prefs.begin(NVS_NAMESPACE, false)) {
        _gameId = _prefs.getUInt(NVS_KEY_GAME_ID, 1);
    }

    File file = SD.open(HISTORY_CSV_PATH, FILE_READ);
    if (file) {
        file.close();
        _available = true;
        return;
    }

    /* No file yet: create it with the header row. This doubles as the mount
     * test -- if no card is fitted, the open fails and logging stays off. */
    file = SD.open(HISTORY_CSV_PATH, FILE_WRITE);
    if (!file) {
        _available = false;
        return;
    }

    file.println(HISTORY_CSV_HEADER);
    file.close();
    _available = true;
}

bool GameLog::record(unsigned long durationMs, bool won)
{
    if (!_available) {
        return false;
    }

    struct tm t;
    if (!_io.getRtcTime(&t)) {
        return false;
    }

    /* GAME_ID,DD/MM/YYYY,HH:MM,SECONDS,RESULT -- matches HISTORY_CSV_HEADER. */
    char line[128];
    snprintf(line, sizeof(line), "%06lu,%02d/%02d/%04d,%02d:%02d,%02lu,%s",
             (unsigned long)_gameId,
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
             t.tm_hour, t.tm_min,
             durationMs / 1000UL,
             won ? "GANHOU" : "PERDEU");

    File file = SD.open(HISTORY_CSV_PATH, FILE_APPEND);
    if (!file) {
        return false;
    }

    file.println(line);
    file.close();

    /* Only now is the id spent. */
    _gameId++;
    _prefs.putUInt(NVS_KEY_GAME_ID, _gameId);

    return true;
}

File GameLog::openForRead()
{
    if (!_available) {
        return File();
    }
    return SD.open(HISTORY_CSV_PATH, FILE_READ);
}
```

- [ ] **Step 3: Self-review**

- The format string uses `%06lu` for the id (cast to `unsigned long`) and `%02lu` for the duration (`durationMs / 1000UL` is already `unsigned long`). The monolith's `%06d` on a `uint32_t` is the bug being fixed here.
- The column count in the `snprintf` format matches `HISTORY_CSV_HEADER`'s five fields.
- `_gameId++` and `putUInt` are both **after** the successful `file.println`, and unreachable when the open failed.
- Every `SD.open` has a matching `close()` on the success path.
- `record()` returns `false` rather than logging — the caller owns the console.

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. `game_log.cpp` now compiles as part of the sketch even though nothing calls it. Expected: PASS. A failure here is a real error in the new file.

- [ ] **Step 5: Commit**

```bash
git add claw_machine/game_log.h claw_machine/game_log.cpp
git commit -m "feat: add GameLog for SD card history and persistent game id

The game id advances only on a successful write, so a card failure leaves no
gap in the numbering. Every failure path is non-fatal: the machine plays on
without a card."
```

---

### Task 4: The web control panel page

**Files:**
- Create: `claw_machine/web_page.h`

**Interfaces:**
- Consumes: nothing at compile time. Mirrors `WEB_INPUT_REFRESH_MS` (200) and `WEB_STATUS_POLL_MS` (500) from Task 1 by hand.
- Produces: `static const char kWebPage[] PROGMEM`.

The page is v02's, with three changes: a held control re-sends its press every 200 ms (the other half of the stuck-input fix), the status box reports SD health, and the CSV download handles the 409 and 404 responses instead of navigating blindly.

- [ ] **Step 1: Write `web_page.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>

/* The control panel served at "/". Kept in its own header so web_portal.cpp
 * stays readable.
 *
 * HOLD_REPEAT_MS below mirrors WEB_INPUT_REFRESH_MS and POLL_MS mirrors
 * WEB_STATUS_POLL_MS from config.h -- the JavaScript cannot read them. The
 * firmware expires a virtual input VIRTUAL_INPUT_TTL_MS (400 ms) after the last
 * press message, so HOLD_REPEAT_MS must stay comfortably below that. */
static const char kWebPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Arcade StamPLC</title>
    <style>
        :root { --bg: #1a1a2e; --panel: #16213e; --accent: #e94560; --text: #fff; --btn: #0f3460; --btn-hover: #1f5f99; --gold: #f9a826; }
        body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', Tahoma, sans-serif; text-align: center; margin: 0; padding: 15px; user-select: none; -webkit-user-select: none; touch-action: manipulation; }
        h2 { margin: 5px 0 15px 0; color: var(--accent); letter-spacing: 2px; text-transform: uppercase; }
        .status-box { background: var(--panel); padding: 15px; border-radius: 12px; font-size: 1.2rem; font-weight: bold; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); border: 1px solid #2a3a5e; }
        #statusText { color: var(--gold); }
        #sdText { font-size: 0.8rem; font-weight: normal; display: block; margin-top: 6px; opacity: 0.7; }
        .controls-top { display: flex; gap: 10px; justify-content: center; margin-bottom: 20px; }
        .btn { padding: 15px; border: none; border-radius: 8px; font-size: 1rem; font-weight: bold; cursor: pointer; text-transform: uppercase; color: var(--text); flex: 1; transition: transform 0.1s; }
        .btn:active { transform: scale(0.95); }
        .btn-coin { background: var(--gold); color: #000; }
        .btn-freeplay { background: var(--btn); }
        .btn-start { background: #2ecc71; margin-bottom: 20px; width: 100%; padding: 18px; font-size: 1.2rem; }
        .dpad-container { margin: 0 auto 20px auto; width: 220px; height: 220px; display: grid; grid-template-columns: 1fr 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; gap: 8px; }
        .dpad-btn { background: var(--btn); color: white; border: none; border-radius: 15px; font-size: 2rem; display: flex; align-items: center; justify-content: center; box-shadow: 0 6px 0 #0a2342; transition: all 0.05s; }
        .dpad-btn:active { transform: translateY(6px); box-shadow: 0 0 0 #0a2342; background: var(--btn-hover); }
        .empty { background: transparent; }
        .btn-action { background: var(--accent); width: 100%; max-width: 350px; padding: 25px; font-size: 1.5rem; box-shadow: 0 6px 0 #8b2939; border-radius: 15px; margin-bottom: 20px; }
        .btn-action:active { transform: translateY(6px); box-shadow: 0 0 0 #8b2939; }
        .time-box { background: var(--panel); padding: 10px; border-radius: 8px; font-size: 0.9rem; margin-bottom: 10px; border: 1px solid #2a3a5e; }
        .btn-sync { background: #4a69bd; padding: 10px; font-size: 0.9rem; width: 100%; margin-top: 10px; }
        .btn-download { background: #e67e22; padding: 10px; font-size: 0.9rem; width: 100%; margin-top: 10px; }
    </style>
</head>
<body>
    <h2>Arcade Control</h2>

    <div class="time-box">
        Hora do Celular: <span id="mobileTime">--:--:--</span>
        <button class="btn btn-sync" onclick="syncArcadeTime()">Sincronizar Relogio do Arcade</button>
    </div>

    <div class="time-box">
        Historico de Jogadas:
        <button class="btn btn-download" onclick="downloadCsv()">Baixar CSV (SD Card)</button>
    </div>

    <div class="status-box">
        Status: <span id="statusText">CARREGANDO...</span>
        <span id="sdText">--</span>
    </div>

    <div class="controls-top">
        <button class="btn btn-coin" onclick="sendCmd('coin')">Ficha</button>
        <button class="btn btn-freeplay" onclick="sendCmd('freeplay')">Freeplay</button>
    </div>

    <button class="btn btn-start" id="btn-start">LIBERAR JOGADA (START)</button>

    <div class="dpad-container">
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-up">&#9650;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-left">&#9664;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-right">&#9654;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-down">&#9660;</button>
        <div class="empty"></div>
    </div>

    <button class="btn btn-action" id="btn-claw">DESCER GARRA</button>

    <script>
        const HOLD_REPEAT_MS = 200;
        const POLL_MS = 500;

        const timers = {};
        const releasers = [];

        function sendCmd(cmd) { fetch('/cmd?action=' + cmd); }
        function setInput(pin, state) { fetch('/input?pin=' + pin + '&state=' + state); }

        function bindMomentary(id, pin) {
            const el = document.getElementById(id);

            const press = (e) => {
                if (e) e.preventDefault();
                if (timers[pin]) return;
                setInput(pin, 1);
                // Re-send while held. The firmware expires a virtual input 400ms
                // after the last message, so a dropped packet or a backgrounded
                // tab releases the control instead of latching it on.
                timers[pin] = setInterval(() => setInput(pin, 1), HOLD_REPEAT_MS);
            };

            const release = (e) => {
                if (e) e.preventDefault();
                if (!timers[pin]) return;
                clearInterval(timers[pin]);
                timers[pin] = null;
                setInput(pin, 0);
            };

            el.addEventListener('touchstart', press, { passive: false });
            el.addEventListener('touchend', release);
            el.addEventListener('touchcancel', release);
            el.addEventListener('mousedown', press);
            el.addEventListener('mouseup', release);
            el.addEventListener('mouseleave', release);
            el.addEventListener('pointercancel', release);

            releasers.push(release);
        }

        function releaseEverything() { releasers.forEach((r) => r()); }
        window.addEventListener('blur', releaseEverything);
        window.addEventListener('pagehide', releaseEverything);
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) releaseEverything();
        });

        bindMomentary('dpad-up', 0);
        bindMomentary('dpad-left', 1);
        bindMomentary('dpad-down', 2);
        bindMomentary('dpad-right', 3);
        bindMomentary('btn-claw', 4);
        bindMomentary('btn-start', 5);

        setInterval(() => {
            fetch('/status').then((r) => r.json()).then((data) => {
                document.getElementById('statusText').innerText = data.status;
                document.getElementById('sdText').innerText =
                    data.sd ? 'SD Card OK' : 'SD Card indisponivel';
            }).catch((e) => console.log(e));
        }, POLL_MS);

        function updateClockUI() {
            document.getElementById('mobileTime').innerText =
                new Date().toLocaleString('pt-BR');
        }
        setInterval(updateClockUI, 1000);
        updateClockUI();

        function syncArcadeTime() {
            const now = new Date();
            const q = 'y=' + now.getFullYear() +
                      '&m=' + (now.getMonth() + 1) +
                      '&d=' + now.getDate() +
                      '&h=' + now.getHours() +
                      '&min=' + now.getMinutes() +
                      '&s=' + now.getSeconds();

            fetch('/sync_time?' + q)
                .then((r) => {
                    if (!r.ok) throw new Error(r.status);
                    alert('Relogio do Arcade sincronizado!');
                })
                .catch(() => alert('Erro ao sincronizar.'));
        }

        function downloadCsv() {
            fetch('/download_csv').then((r) => {
                // The transfer blocks the firmware loop, so it is refused while
                // a game is in progress.
                if (r.status === 409) { alert('Aguarde a jogada terminar.'); return null; }
                if (r.status === 404) { alert('Nenhum historico no cartao SD.'); return null; }
                if (!r.ok) { alert('Erro ao baixar.'); return null; }
                return r.blob();
            }).then((blob) => {
                if (!blob) return;
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = 'historico.csv';
                a.click();
                URL.revokeObjectURL(url);
            }).catch(() => alert('Erro ao baixar.'));
        }
    </script>
</body>
</html>
)rawliteral";
```

- [ ] **Step 2: Self-review**

- `releasers` and `timers` are declared **before** the `bindMomentary` calls that push onto them.
- Every `bindMomentary` pin matches `config.h`: up=0, left=1, down=2, right=3, claw=4 (`CLAW_BUTTON_PIN`), start=5 (`START_PIN`). All are `<= WEB_MAX_INPUT_PIN`.
- The raw literal delimiter is `rawliteral` on both ends, and the HTML contains no `)rawliteral"` sequence.
- Arrows and the claw label use HTML entities / plain ASCII rather than raw multi-byte characters, so the file's encoding cannot corrupt the page.
- `press` guards on `timers[pin]` so touch-then-mouse double firing on hybrid devices cannot start two intervals.

- [ ] **Step 3: Compile gate**

Ask the developer to Verify in the Arduino IDE. Nothing includes `web_page.h` yet, so this only confirms nothing was broken. Expected: same result as before this task.

- [ ] **Step 4: Commit**

```bash
git add claw_machine/web_page.h
git commit -m "feat: add the web control panel page

Ported from monolito_v02 with three changes: a held control re-sends its press
every 200ms so the firmware's 400ms input TTL cannot expire under the user's
finger, the status box reports SD health, and the CSV download handles the 409
and 404 responses."
```

---

### Task 5: `WebPortal` — WiFi AP and HTTP routes

**Files:**
- Create: `claw_machine/web_portal.h`
- Create: `claw_machine/web_portal.cpp`

**Interfaces:**
- Consumes: `MachineIO::setVirtualInput()` and `MachineIO::setRtcTime()` from Task 2; `GameLog::available()` and `GameLog::openForRead()` from Task 3; `kWebPage` from Task 4; `AP_SSID`, `AP_PASSWORD`, `WEB_SERVER_PORT`, `WEB_MAX_INPUT_PIN` from Task 1.
- Produces:
  - `WebPortal::WebPortal(MachineIO& io, GameLog& log, DashboardUI& ui)`
  - `using CommandHandler = std::function<void(const char*)>`
  - `using StatusProvider = std::function<String()>`
  - `using DownloadGate = std::function<bool()>`
  - `void WebPortal::begin()`, `void WebPortal::pump()`
  - `void WebPortal::onCommand(CommandHandler)`, `setStatusProvider(StatusProvider)`, `setDownloadGate(DownloadGate)`

Nothing constructs a `WebPortal` yet.

- [ ] **Step 1: Write `web_portal.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <functional>
#include "config.h"
#include "dashboard_ui.h"
#include "game_log.h"
#include "machine_io.h"

/* Sole owner of WiFi and of the HTTP server.
 *
 * Deliberately knows nothing about GameStateMachine. Commands arrive through the
 * same CommandHandler signature TabletLink uses, and status is read through a
 * provider the sketch supplies -- so the web panel is structurally incapable of
 * writing machine state. Every state change still goes through transitionTo().
 * This is what makes the monolith's "handler sets machineStatus directly" bug
 * class impossible here. */
class WebPortal {
public:
    using CommandHandler = std::function<void(const char*)>;
    using StatusProvider = std::function<String()>;
    using DownloadGate   = std::function<bool()>;

    WebPortal(MachineIO& io, GameLog& log, DashboardUI& ui)
        : _io(io), _log(log), _ui(ui), _server(WEB_SERVER_PORT)
    {
    }

    /* Brings up the access point and registers the routes. Never blocks: a radio
     * failure is logged to the console and the machine plays on without it. */
    void begin();

    /* Services at most the pending request. Call every loop(), next to
     * TabletLink::pump(). */
    void pump() { _server.handleClient(); }

    void onCommand(CommandHandler h) { _handler = std::move(h); }
    void setStatusProvider(StatusProvider p) { _status = std::move(p); }

    /* Returns false while the CSV download must be refused. streamFile() blocks
     * loop() for the whole transfer, so it cannot run during a game. */
    void setDownloadGate(DownloadGate g) { _gate = std::move(g); }

private:
    MachineIO&   _io;
    GameLog&     _log;
    DashboardUI& _ui;
    WebServer    _server;

    CommandHandler _handler;
    StatusProvider _status;
    DownloadGate   _gate;

    void handleRoot();
    void handleStatus();
    void handleCmd();
    void handleInput();
    void handleSyncTime();
    void handleDownloadCsv();
};
```

- [ ] **Step 2: Write `web_portal.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "web_portal.h"

#include "web_page.h"

void WebPortal::begin()
{
    _ui.console_log("Iniciando WiFi (AP)...");

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        /* Non-fatal: no routes are registered and the machine plays normally. */
        _ui.console_log("Falha ao criar AP");
        return;
    }

    String ipLog = "IP: " + WiFi.softAPIP().toString();
    _ui.console_log("Rede Criada!");
    _ui.console_log(ipLog.c_str());

    _server.on("/", [this]() { handleRoot(); });
    _server.on("/status", [this]() { handleStatus(); });
    _server.on("/cmd", [this]() { handleCmd(); });
    _server.on("/input", [this]() { handleInput(); });
    _server.on("/sync_time", [this]() { handleSyncTime(); });
    _server.on("/download_csv", [this]() { handleDownloadCsv(); });
    _server.begin();
}

void WebPortal::handleRoot()
{
    _server.send_P(200, "text/html", kWebPage);
}

void WebPortal::handleStatus()
{
    String json = "{\"status\":\"";
    json += _status ? _status() : String("DESCONHECIDO");
    json += "\",\"sd\":";
    json += _log.available() ? "true" : "false";
    json += "}";

    _server.send(200, "application/json", json);
}

void WebPortal::handleCmd()
{
    if (!_server.hasArg("action")) {
        _server.send(400, "text/plain", "missing action");
        return;
    }

    /* Forwarded to the same handler the tablet's serial commands reach. This
     * class never touches machine state itself. */
    if (_handler) {
        _handler(_server.arg("action").c_str());
    }

    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleInput()
{
    if (!_server.hasArg("pin") || !_server.hasArg("state")) {
        _server.send(400, "text/plain", "missing pin or state");
        return;
    }

    int pin = _server.arg("pin").toInt();
    if (pin < 0 || pin > WEB_MAX_INPUT_PIN) {
        _server.send(400, "text/plain", "pin out of range");
        return;
    }

    _io.setVirtualInput(pin, _server.arg("state").toInt() == 1);
    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleSyncTime()
{
    if (!_server.hasArg("y") || !_server.hasArg("m") || !_server.hasArg("d") ||
        !_server.hasArg("h") || !_server.hasArg("min") || !_server.hasArg("s")) {
        _server.send(400, "text/plain", "Bad Request");
        return;
    }

    /* Zero-initialized, not merely assigned: MachineIO::setRtcTime() normalizes
     * with mktime(), which needs a struct with no indeterminate fields. The
     * monolith passed an uninitialized one, handing the RTC a garbage
     * day-of-week. */
    struct tm t = {};
    t.tm_year   = _server.arg("y").toInt() - 1900;
    t.tm_mon    = _server.arg("m").toInt() - 1;
    t.tm_mday   = _server.arg("d").toInt();
    t.tm_hour   = _server.arg("h").toInt();
    t.tm_min    = _server.arg("min").toInt();
    t.tm_sec    = _server.arg("s").toInt();

    if (!_io.setRtcTime(&t)) {
        _server.send(400, "text/plain", "invalid time");
        return;
    }

    _ui.console_log("Relogio web sincronizado!");
    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleDownloadCsv()
{
    if (_gate && !_gate()) {
        _server.send(409, "text/plain", "Maquina ocupada");
        return;
    }

    File file = _log.openForRead();
    if (!file) {
        _server.send(404, "text/plain", "Historico nao encontrado");
        return;
    }

    _server.sendHeader("Content-Disposition", "attachment; filename=\"historico.csv\"");
    _server.streamFile(file, "text/csv");
    file.close();

    _ui.console_log("Web: Download CSV concluido");
}
```

- [ ] **Step 3: Self-review**

- No handler assigns to any machine state — the only writes are `_io.setVirtualInput` and `_io.setRtcTime`. Verify by eye; this is the whole point of the class.
- `_server` is constructed with `WEB_SERVER_PORT` in the member-init list, in declaration order (`_io`, `_log`, `_ui`, `_server`) so no `-Wreorder` warning.
- Every handler sends exactly one response on every path, including the early returns.
- `_status`, `_handler` and `_gate` are all null-checked before being called — `begin()` can run before the sketch wires them.
- `#include "web_page.h"` is in the `.cpp` only, so `kWebPage` gets exactly one definition.

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. `web_portal.cpp` now compiles as part of the sketch even though nothing calls it — this is where a missing `WiFi`/`WebServer` header or an incompatible `WebServer` API would surface. Expected: PASS. Also ask for the reported flash/RAM figures: this task is where the WiFi stack enters the binary, and footprint is the design's main risk.

- [ ] **Step 5: Commit**

```bash
git add claw_machine/web_portal.h claw_machine/web_portal.cpp
git commit -m "feat: add WebPortal for the WiFi AP and HTTP control panel

Commands are forwarded through the same handler signature TabletLink uses, so
the portal cannot write machine state -- every transition still goes through
transitionTo(). The CSV download is gated because streamFile blocks loop()."
```

---

### Task 6: Freeplay and prize detection in `GameStateMachine`

**Files:**
- Modify: `claw_machine/game_state.h`
- Modify: `claw_machine/game_state.cpp`
- Modify: `claw_machine/claw_machine.ino` (constructor arguments and `GameLog` instantiation only — the `WebPortal` wiring is Task 7)

**Interfaces:**
- Consumes: `GameLog` from Task 3; `MachineIO::prizeSensor()`, `freeplayButtonClicked()` from Task 2; `PRIZE_WAIT_MS`, `Protocol::CMD_FREEPLAY` from Task 1.
- Produces:
  - `GameStateMachine::GameStateMachine(MachineIO&, TabletLink&, StickMapper&, DashboardUI&, GameLog&)` — **note the new fifth argument**
  - `MachineStatus::Freeplay`, `MachineStatus::WaitPrize`
  - `void GameStateMachine::setFreeplay(bool on)`
  - `String GameStateMachine::statusText() const`
  - `bool GameStateMachine::downloadAllowed() const`

This is the first task that changes gameplay. It is fully testable on hardware without the web panel: BtnA toggles freeplay, and the prize sensor is a physical input.

- [ ] **Step 1: Extend the enum and the class in `game_state.h`**

Add `#include "game_log.h"` below `#include "dashboard_ui.h"`.

Replace the enum:

```cpp
enum class MachineStatus {
    Idle,
    RelayOn,
    Screen,
    WaitTimer,
    Running,
    WaitPrize,
    Freeplay,
    ConfigMapping
};
```

Replace the constructor:

```cpp
    GameStateMachine(MachineIO& io, TabletLink& tablet, StickMapper& mapper, DashboardUI& ui,
                     GameLog& log)
        : _io(io), _tablet(tablet), _mapper(mapper), _ui(ui), _log(log)
    {
    }
```

Add to the public section, after `enterConfigMode()`:

```cpp
    /* Freeplay toggle. Reachable from any state and from either entry point
     * (front-panel BtnA or the web panel), so -- exactly like enterConfigMode()
     * -- it releases the credit relay before transitioning, and cannot leave it
     * latched when toggled mid-credit. */
    void setFreeplay(bool on);

    /* Read by WebPortal through the providers the sketch installs. */
    String statusText() const;

    /* The CSV transfer blocks loop(), so it is only allowed between games. */
    bool downloadAllowed() const;
```

Add to the private section, after `_ui`:

```cpp
    GameLog& _log;
```

and after `_entryPending`:

```cpp
    bool          _freeplayMode  = false;
    /* Start of play. _lastChange cannot serve this role: entering WaitPrize
     * overwrites it before the duration is computed. */
    unsigned long _gameStartedAt = 0;
```

and next to `transitionTo`:

```cpp
    /* Shared exit from WaitPrize: logs the outcome, records the row, and routes
     * back to Freeplay or Idle. */
    void finishGame(bool won);
```

- [ ] **Step 2: Add `setFreeplay`, `statusText`, `downloadAllowed` and `finishGame` to `game_state.cpp`**

Add after `enterConfigMode()`:

```cpp
void GameStateMachine::setFreeplay(bool on)
{
    _freeplayMode = on;

    /* Same hazard as config mode: reachable from RelayOn and Running, skipping
     * their cleanup. Release the credit relay so it cannot stay latched.
     * (transitionTo handles the motor relays.) */
    _io.creditRelay(false);

    if (_freeplayMode) {
        _ui.console_log("Modo Freeplay ATIVADO");
        transitionTo(MachineStatus::Freeplay);
    } else {
        _ui.console_log("Modo Freeplay DESATIVADO");
        transitionTo(MachineStatus::Idle);
    }
}

String GameStateMachine::statusText() const
{
    switch (_status) {
    case MachineStatus::Idle:          return "AGUARDANDO FICHA";
    case MachineStatus::RelayOn:       return "LIBERANDO CREDITO";
    case MachineStatus::Screen:        return "TELA DE ESPERA";
    case MachineStatus::WaitTimer:     return "AGUARDANDO INICIO";
    case MachineStatus::Running:       return "EM JOGO";
    case MachineStatus::WaitPrize:     return "AGUARDANDO PREMIO";
    case MachineStatus::Freeplay:      return "MODO FREEPLAY";
    case MachineStatus::ConfigMapping: return "MODO CONFIGURACAO";
    }
    return "DESCONHECIDO";
}

bool GameStateMachine::downloadAllowed() const
{
    return _status == MachineStatus::Idle || _status == MachineStatus::Freeplay;
}

void GameStateMachine::finishGame(bool won)
{
    _ui.console_log(won ? "VITORIA! Premio detectado" : "DERROTA! Nenhum premio");

    if (_log.record(millis() - _gameStartedAt, won)) {
        _ui.console_log("SD: Registro Salvo");
    } else {
        _ui.console_log("SD: Erro ao Salvar");
    }

    transitionTo(_freeplayMode ? MachineStatus::Freeplay : MachineStatus::Idle);
}
```

- [ ] **Step 3: Clear freeplay in `begin()` and `enterConfigMode()`**

In `begin()`, add before the `transitionTo`:

```cpp
    _freeplayMode = false;
```

In `enterConfigMode()`, add immediately after the `console_log("Modo Config Iniciado")` line:

```cpp
    /* Config and freeplay must not overlap: leaving freeplay armed would send
     * the walkthrough's exit straight back into a free game. */
    _freeplayMode = false;
```

- [ ] **Step 4: Handle the freeplay command in `onCommand()`**

Add immediately after the `CMD_COIN` block's closing `}` (after its `return;`), before the "Unknown command" fallthrough:

```cpp
    if (strcmp(cmd, Protocol::CMD_FREEPLAY) == 0) {
        setFreeplay(!_freeplayMode);
        return;
    }
```

- [ ] **Step 5: Add the BtnA check to `update()`**

Add immediately after the existing `configButtonClicked()` block:

```cpp
    /* BtnA and the web panel's Freeplay button reach the same method, so the two
     * entry points cannot drift apart. */
    if (_io.freeplayButtonClicked()) {
        setFreeplay(!_freeplayMode);
    }
```

- [ ] **Step 6: Rewrite the affected cases in `update()`**

Replace `RelayOn`:

```cpp
    case MachineStatus::RelayOn:
        if (millis() - _lastChange >= RELAY_ON_TIME) {
            _io.creditRelay(false);
            _ui.console_log("Relay Off");
            /* Freeplay already sent "ready" on entry to its own state, so it
             * skips the tablet's screen handshake and goes straight to play. */
            transitionTo(_freeplayMode ? MachineStatus::Running : MachineStatus::Screen);
        }
        break;
```

Replace `WaitTimer` — the `Protocol::START` send moves out of here:

```cpp
    case MachineStatus::WaitTimer:
        if (_io.stick(0) || _io.stick(1) || _io.stick(2) || _io.stick(3)) {
            _ui.console_log("Contagem iniciada");
            transitionTo(MachineStatus::Running);
        }
        break;
```

Add the new `Freeplay` case after `WaitTimer`:

```cpp
    case MachineStatus::Freeplay:
        /* On entry, put the tablet on its attract screen and wait. */
        if (_entryPending) {
            _ui.console_log("Freeplay: aguardando jogada");
            _tablet.send(Protocol::READY);
            _entryPending = false;
        }

        if (_io.stick(0) || _io.stick(1) || _io.stick(2) || _io.stick(3)) {
            /* A free game still issues a real credit, so the machine's own
             * counters stay consistent with the tablet's. RelayOn releases it
             * after RELAY_ON_TIME and routes back here to Running. */
            _io.creditRelay(true);
            _ui.console_log("Relay On (Freeplay)");
            transitionTo(MachineStatus::RelayOn);
        }
        break;
```

Replace `Running`:

```cpp
    case MachineStatus::Running:
        if (_entryPending) {
            /* "start" is sent HERE, not at the stick press, so both entry paths
             * -- WaitTimer and the freeplay credit pulse -- start the tablet's
             * timer at the instant the joystick actually drives the motors. In
             * the monolith the freeplay path sent it a full second early. */
            _tablet.send(Protocol::START);
            _ui.console_log("Time running");
            _gameStartedAt = millis();
            _entryPending  = false;
        }

        _mapper.apply();

        if (_io.clawButton() || millis() - _lastChange > RUNNING_TIMEOUT) {
            _tablet.send(Protocol::CLAW);
            _ui.console_log("Aguardando sensor...");
            transitionTo(MachineStatus::WaitPrize);
        }
        break;
```

Add the new `WaitPrize` case after `Running`:

```cpp
    case MachineStatus::WaitPrize:
        /* Replaces the monolith's blocking 5 s while-loop. As a state, it lets
         * loop() keep calling TabletLink::pump() throughout: the USB CDC receive
         * queue holds 256 bytes and the ISR silently discards the overflow, so
         * five blocked seconds cost real tablet messages.
         *
         * transitionTo() has already de-energized the motor relays on entry. */
        if (_io.prizeSensor()) {
            finishGame(true);
        } else if (millis() - _lastChange >= PRIZE_WAIT_MS) {
            finishGame(false);
        }
        break;
```

- [ ] **Step 7: Wire `GameLog` into the sketch**

In `claw_machine.ino`, add `#include "game_log.h"` after `#include "dashboard_ui.h"`, and change the globals:

```cpp
MachineIO   machineIo;
TabletLink  tablet;
DashboardUI dashboardUi;
GameLog     gameLog(machineIo);

StickMapper      mapper(machineIo);
GameStateMachine game(machineIo, tablet, mapper, dashboardUi, gameLog);
```

In `setup()`, add after `machineIo.begin();`:

```cpp
    /* After machineIo.begin(): that call is what enables the SD reader. */
    gameLog.begin();
```

- [ ] **Step 8: Self-review**

Check each of these by reading the modified `update()`:

- `_mapper.apply()` appears **only** inside `case MachineStatus::Running`. The monolith moved it into the main loop, energizing motors in every state — that is defect D1 and is explicitly not being ported.
- `transitionTo()` is unchanged and still de-energizes the motor relays on entry to every state except `Running`, so `Freeplay` and `WaitPrize` both leave the motors off.
- `Protocol::START` is sent in exactly one place — `Running`'s entry block — and no longer in `WaitTimer`.
- `_gameStartedAt` is written only in `Running`'s entry block and read only in `finishGame()`.
- No `delay()` was introduced. `WaitPrize` returns to `loop()` on every iteration.
- `setFreeplay()` calls `_io.creditRelay(false)` before `transitionTo()`, on both branches.
- Every `MachineStatus` enumerator has a `case` in both `update()` and `statusText()`. Eight states, eight cases in each.
- The `.ino` constructor call passes five arguments in the order the header declares them.

- [ ] **Step 9: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS. A "no matching constructor" error means Step 7's argument list and Step 1's declaration disagree.

- [ ] **Step 10: Hardware acceptance test**

Ask the developer to flash and check, reporting each result:

1. **Normal game** — insert a coin from the tablet; credit relay pulses ~1 s; tablet gets `ready`; joystick starts the game and the tablet gets `start` at that moment; claw button sends `claw`; the console shows `Aguardando sensor...` and, ~5 s later, `DERROTA! Nenhum premio` and `SD: Registro Salvo`.
2. **Win path** — repeat, triggering PLC input 7 during the 5 s window. Console shows `VITORIA! Premio detectado` and the machine returns to idle immediately rather than waiting the full 5 s.
3. **Loop is not blocked** — during the 5 s prize wait, the dashboard console still animates and the front-panel button tones still respond. In the monolith this window was frozen.
4. **Freeplay** — BtnA shows `Modo Freeplay ATIVADO`; a joystick press pulses the credit relay and starts a game with no coin; after the game the console shows `Freeplay: aguardando jogada` rather than returning to idle. BtnA again shows `Modo Freeplay DESATIVADO`.
5. **Freeplay toggled mid-credit** — press BtnA while the credit relay is energized (during the ~1 s `RelayOn` window). The credit relay must go off immediately and must not stay latched.
6. **Config still clears freeplay** — enable freeplay, then press BtnB. The walkthrough runs and the machine ends in idle, not freeplay.
7. **CSV** — pull the card and confirm the rows: sequential ids, today's date, the played duration in seconds, `GANHOU`/`PERDEU` matching what happened.
8. **No card** — boot with the card removed. Console reports the failure once; games still play; the console shows `SD: Erro ao Salvar` at the end of each.
9. **Clock** — the dashboard now shows RTC time and date (it may be wrong until Task 7's sync exists; only presence is being checked here).

- [ ] **Step 11: Commit**

```bash
git add claw_machine/game_state.h claw_machine/game_state.cpp claw_machine/claw_machine.ino
git commit -m "feat: add freeplay mode and non-blocking prize detection

WaitPrize replaces the monolith's blocking 5s sensor loop, so TabletLink::pump()
keeps draining the 256-byte USB CDC queue throughout. Protocol::START moves to
Running's entry action, which removes the freeplay timing skew. setFreeplay()
releases the credit relay before transitioning, like enterConfigMode()."
```

---

### Task 7: Wire the web portal into the sketch

**Files:**
- Modify: `claw_machine/claw_machine.ino`

**Interfaces:**
- Consumes: `WebPortal` from Task 5; `GameStateMachine::statusText()`, `downloadAllowed()`, `onCommand()` from Task 6.
- Produces: nothing — this is the final switchover.

- [ ] **Step 1: Instantiate `WebPortal`**

Add `#include "web_portal.h"` after `#include "tablet_link.h"`, and add the global after the `game` declaration (it must come after `gameLog`, which it references):

```cpp
WebPortal web(machineIo, gameLog, dashboardUi);
```

- [ ] **Step 2: Wire the callbacks in `setup()`**

Add after the existing `game.begin();`:

```cpp
    /* The portal reaches the state machine only through these three callables:
     * it can submit a command, read the status string, and ask whether a
     * download is allowed. It cannot write state. Installed after game.begin()
     * so the AP only advertises once the machine is in a known state. */
    web.onCommand([](const char* cmd) { game.onCommand(cmd); });
    web.setStatusProvider([]() { return game.statusText(); });
    web.setDownloadGate([]() { return game.downloadAllowed(); });
    web.begin();
```

- [ ] **Step 3: Service HTTP in `loop()`**

Add immediately after `tablet.pump();`:

```cpp
    /* Same cadence as the serial drain. */
    web.pump();
```

- [ ] **Step 4: Self-review**

- `web` is declared after `gameLog` — a reference member bound to a not-yet-constructed global would be undefined behavior.
- The three lambdas capture nothing and refer to the file-scope `game`, matching the existing `tablet.onCommand` pattern.
- `web.pump()` is unconditional, in every state, like `tablet.pump()`.
- `web.begin()` is the last statement in `setup()`.

- [ ] **Step 5: Compile gate**

Ask the developer to Verify in the Arduino IDE, and to report the final flash and RAM figures alongside the numbers from Task 5. WiFi, `WebServer`, SD and the existing `M5GFX` sprite canvases are all in the binary now; footprint is the design's headline risk and this is the measurement that settles it.

- [ ] **Step 6: Hardware acceptance test**

Ask the developer to flash, join the `DBAdmin` network, open `http://192.168.4.1`, and report each result:

1. **Page loads** and the status box tracks the machine — `AGUARDANDO FICHA` when idle, `EM JOGO` during play, `AGUARDANDO PREMIO` during the sensor window.
2. **Ficha** starts a normal game; **Freeplay** toggles the mode and the dashboard console mirrors it.
3. **D-pad drives the motors during a game** and does nothing outside one — with the machine idle, holding a direction must leave every motor relay off.
4. **Stuck-input test (the D3 fix)** — hold a D-pad button and turn the phone's WiFi off mid-press, or switch apps. The motor must release within roughly half a second. This is the single most important check in the plan; the monolith latched the relay on indefinitely.
5. **Claw and start buttons** work from the page.
6. **Clock sync** sets the dashboard clock to the phone's time, and the next CSV row carries the corrected date.
7. **CSV download** works from idle. Requesting it mid-game shows `Aguarde a jogada terminar.` instead of stalling the machine.
8. **No card** — with the card removed, the status box reads `SD Card indisponivel` and the download reports no history.
9. **Config from the phone** — press BtnB, then complete the whole mapping walkthrough using only the D-pad.
10. **Joystick responsiveness** — play a normal game with the page open on a phone (so `/status` is polling twice a second). Motor response to the physical joystick must feel unchanged. If it does not, say so: the design's fallback is to service HTTP only outside `Running`, and that decision is the developer's.

- [ ] **Step 7: Commit**

```bash
git add claw_machine/claw_machine.ino
git commit -m "feat: wire the web control panel into the sketch

Completes the monolito_v02 port. The portal reaches the state machine through
three callables -- submit a command, read the status, ask whether a download is
allowed -- and web.pump() runs every loop alongside tablet.pump()."
```

---

## Self-Review

**Spec coverage** — every section of `2026-08-03-monolito-v02-port-design.md` maps to a task:

| Spec section | Task |
|--------------|------|
| `config.h` additions | 1 |
| `MachineIO` virtual input layer, `prizeSensor`, `freeplayButtonClicked`, `setRtcTime`, SD enable | 2 |
| `GameLog` | 3 |
| `WebPortal` page (`web_page.h`) | 4 |
| `WebPortal` class and routes | 5 |
| `GameStateMachine` — `Freeplay`, `WaitPrize`, `setFreeplay`, `statusText`, `downloadAllowed` | 6 |
| `claw_machine.ino` wiring | 6 (GameLog), 7 (WebPortal) |
| Error-handling table | 3 (SD/NVS), 5 (AP failure, malformed request, 409), 2 (input expiry), 6 (prize timeout) |
| Verification section | Per-task compile gates; Task 6 step 10 and Task 7 step 6 |
| Defects D1–D11 | D1 → 6 step 8 · D2 → 6 · D3 → 2 + 4 · D4 → 5 + 6 · D5 → 6 · D6 → global constraint · D7 → 2 + 5 · D8 → 3 · D9 → 3 · D10 → 5 + 6 · D11 → 1 |

**Type consistency** — checked across tasks: `setVirtualInput(int, bool)`, `virtualActive(int) const`, `prizeSensor() const`, `freeplayButtonClicked()`, `setRtcTime(const struct tm*)` (Task 2) are called with those exact signatures in Task 5. `GameLog::available()`, `openForRead()`, `record(unsigned long, bool)` (Task 3) match their call sites in Tasks 5 and 6. `statusText()` returns `String`, matching `StatusProvider = std::function<String()>`; `downloadAllowed()` returns `bool`, matching `DownloadGate = std::function<bool()>`. The five-argument `GameStateMachine` constructor is declared in Task 6 step 1 and called in Task 6 step 7.

**Two deliberate divergences from the spec text**, both recorded here rather than left as silent drift:

1. The spec says `poll()` expires virtual input stamps. The implementation evaluates expiry lazily inside `virtualActive()` instead. Same observable behavior, and it removes any dependence on when `poll()` last ran.
2. The spec's `statusText()` strings carry Portuguese accents. The implementation uses unaccented ASCII, consistent with every other string literal in the sketch, so source encoding cannot corrupt them on the way to the page.
