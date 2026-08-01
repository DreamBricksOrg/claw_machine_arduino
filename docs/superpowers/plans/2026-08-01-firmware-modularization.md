# Firmware Modularization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the monolithic `claw_machine.ino` into six focused translation units, with all `M5StamPLC` access behind `MachineIO` and all `Serial` access behind `TabletLink`.

**Architecture:** Constructor injection. `GameStateMachine` holds references to `MachineIO`, `TabletLink`, `StickMapper`, and `DashboardUI`; those depend only on `config.h`. Dependencies point one direction — nothing points back up. The `.ino` reduces to instance declarations, `setup()`, and a six-call `loop()`.

**Tech Stack:** Arduino / ESP32-S3, M5StamPLC library, M5GFX, C++17.

## Global Constraints

- Target sketch folder: `claw_machine/` in `C:\Users\Win 11\Documents\github\claw_machine_arduino`. **Never** edit the Google Drive copy under `G:\.shortcut-targets-by-id\...` — it silently reverts edits.
- Branch: `refactor/modularize-firmware`.
- Design spec: `docs/superpowers/specs/2026-08-01-firmware-modularization-design.md`.
- `DashboardUI` (`dashboard_ui.h/.cpp`) must not be modified.
- The font and image headers (`font_montserrat_*.h`, `img_tag_*.h`) must not be modified.
- Only `machine_io.cpp` may include `M5StamPLC.h` for hardware access; the `.ino` includes it solely for `M5StamPLC.update()` and `M5StamPLC.Display`.
- Only `tablet_link.cpp` may reference `Serial`.
- Portuguese console strings are user-facing and must be copied **byte for byte**, including missing accents (`"Mapeamento concluido!"`, `"Botao de inicio apertado"`, `"Coin ignorado (ocupado)"`).
- Wire protocol strings are fixed by the tablet and must not change: inbound `"coin"`; outbound `"0"`, `"ready"`, `"start"`, `"claw"`.
- `pump()` must never block. No `delay()` may be introduced anywhere in the `loop()` path.

## Verification model

There is no Arduino toolchain on the development machine and no unit-test framework in this sketch. Do not claim any task builds. Each task's gate is:

1. **Static check** — the checklist given in the task, performed by reading the code.
2. **Compile gate** — the developer opens `claw_machine/claw_machine.ino` in the Arduino IDE and clicks Verify. Report the result before moving on.

Tasks 1–5 only add files. The old `.ino` keeps its original code and keeps running unchanged; the new classes compile but are unused. Task 6 is the switchover and is the only task that can change runtime behavior.

Note on Tasks 2–5: `MachineIO` declares its own `M5StamPLC_AC _ac` member while the old `.ino` still has its `stamplc_ac` global. Two objects exist, but only the old one is ever `begin()`-ed, so behavior is unaffected. Task 6 deletes the old global.

---

### Task 1: Configuration header

**Files:**
- Create: `claw_machine/config.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `STICK_COUNT`, `STICK1_PIN`…`STICK4_PIN`, `CLAW_BUTTON_PIN`, `START_PIN`, `PLC_INPUT_COUNT`, `PLC_RELAY_COUNT`, `RELAY_ON_TIME`, `RUNNING_TIMEOUT`, `START_DEBOUNCE_MS`, `SCREEN_HOLD_MS`, `STICK_RELEASE_DEBOUNCE_MS`, `IO_POLL_INTERVAL_MS`, `CLOCK_UPDATE_INTERVAL_MS`, `SERIAL_BAUD`, `ENABLE_CLOCK_DISPLAY`, `stickPin(int)`, `namespace Protocol`.

- [ ] **Step 1: Create `claw_machine/config.h`**

```cpp
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
```

- [ ] **Step 2: Static check against the original**

Open `claw_machine/claw_machine.ino` and confirm every value carried over exactly:

| `config.h` | Original source |
|---|---|
| `RELAY_ON_TIME = 1000` | `#define RELAY_ON_TIME 1000` |
| `RUNNING_TIMEOUT = 50000` | `#define RUNNING_TIMEOUT 50000` |
| `START_DEBOUNCE_MS = 200` | `#define START_DEBOUNCE_MS 200` |
| `STICK1_PIN..STICK4_PIN = 0,1,2,3` | `#define STICK1_PIN 0` … `STICK4_PIN 3` |
| `CLAW_BUTTON_PIN = 4` | `#define BUTTON_PIN 4` |
| `START_PIN = 5` | `#define START_PIN 5` |
| `SCREEN_HOLD_MS = 2000` | literal `2000` in `case STATUS_SCREEN` |
| `STICK_RELEASE_DEBOUNCE_MS = 50` | literal `delay(50)` in `case STATUS_CONFIG_MAPPING` |
| `IO_POLL_INTERVAL_MS = 50` | literal `50` in `update_plc_io_state()` |
| `CLOCK_UPDATE_INTERVAL_MS = 1000` | literal `1000` in `update_time_and_date()` |
| `SERIAL_BAUD = 115200` | `Serial.begin(115200)` |
| `PLC_INPUT_COUNT = 8` | `for (int i = 0; i < 8; i++)` in `update_plc_io_state()` |
| `PLC_RELAY_COUNT = 4` | `for (int i = 0; i < 4; i++)` in `update_plc_io_state()` |

Note `BUTTON_PIN` is renamed to `CLAW_BUTTON_PIN` — the old name gave no hint that it is the claw-drop button. The old `#define`s stay in the `.ino` until Task 6, so there is no conflict yet.

- [ ] **Step 3: Compile gate**

Ask the developer to Verify `claw_machine.ino` in the Arduino IDE. The sketch does not yet include `config.h`, so this only confirms nothing was broken. Expected: same result as before this task.

- [ ] **Step 4: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/config.h
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: extract pins, timings and protocol strings into config.h"
```

---

### Task 2: MachineIO — hardware access

**Files:**
- Create: `claw_machine/machine_io.h`, `claw_machine/machine_io.cpp`

**Interfaces:**
- Consumes: `config.h` (Task 1) — `stickPin()`, `START_PIN`, `CLAW_BUTTON_PIN`, `PLC_INPUT_COUNT`, `PLC_RELAY_COUNT`, `IO_POLL_INTERVAL_MS`, `START_DEBOUNCE_MS`.
- Produces: `class MachineIO` with `void begin()`, `void poll()`, `bool stick(int)`, `bool rawInput(int)`, `bool clawButton()`, `bool startJustPressed()`, `bool configButtonClicked()`, `void motorRelay(int,bool)`, `void allMotorRelaysOff()`, `void creditRelay(bool)`, `bool getRtcTime(struct tm*)`, `const std::array<int,PLC_INPUT_COUNT>& inputs() const`, `const std::array<int,PLC_RELAY_COUNT>& relays() const`.

Deviation from the spec: `begin()` returns `void`, not `bool`. The AC init retry loop never exits on failure, so a status return would always be `true` and would invite a caller to check something meaningless.

- [ ] **Step 1: Create `claw_machine/machine_io.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <M5StamPLC.h>
#include <array>
#include "config.h"

/* Sole owner of all M5StamPLC and M5StamPLC-AC hardware access.
 * No other translation unit reads an input or writes a relay. */
class MachineIO {
public:
    /* Brings up the PLC and the AC module. Blocks, retrying once per second,
     * until the AC module answers -- same as the original setup(). */
    void begin();

    /* Refreshes the cached input/relay snapshot (throttled) and plays the
     * front-panel button tones. Call once per loop(). */
    void poll();

    /* ---- Inputs ---- */
    bool stick(int index) const;   /* index 0..3 */
    bool rawInput(int pin) const;  /* raw PLC input pin */
    bool clawButton() const;

    /* Rising edge of the physical start button, debounced. Returns the edge
     * only; sending the protocol byte is the state machine's job. */
    bool startJustPressed();

    /* Front-panel BtnB click -- enters config mode. */
    bool configButtonClicked();

    /* ---- Outputs ---- */
    void motorRelay(int index, bool on);
    void allMotorRelaysOff();

    /* Credit relay and its status light move together: green energized,
     * red released. */
    void creditRelay(bool on);

    /* ---- Misc ---- */
    bool getRtcTime(struct tm* out);

    /* Snapshot refreshed by poll(), rendered by the dashboard. */
    const std::array<int, PLC_INPUT_COUNT>& inputs() const { return _inputs; }
    const std::array<int, PLC_RELAY_COUNT>& relays() const { return _relays; }

private:
    M5StamPLC_AC _ac;

    std::array<int, PLC_INPUT_COUNT> _inputs{};
    std::array<int, PLC_RELAY_COUNT> _relays{};

    unsigned long _lastPoll      = 0;
    bool          _lastStartInput = false;
    unsigned long _lastStartEdge  = 0;

    void updateButtonTones();
};
```

- [ ] **Step 2: Create `claw_machine/machine_io.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "machine_io.h"

void MachineIO::begin()
{
    M5StamPLC.begin();

    while (!_ac.begin()) {
        printf("M5StamPLC-AC init failed, retry in 1s...\n");
        delay(1000);
    }
}

void MachineIO::poll()
{
    updateButtonTones();

    if (millis() - _lastPoll <= IO_POLL_INTERVAL_MS) {
        return;
    }

    for (int i = 0; i < PLC_INPUT_COUNT; i++) {
        _inputs[i] = M5StamPLC.readPlcInput(i);
    }
    for (int i = 0; i < PLC_RELAY_COUNT; i++) {
        _relays[i] = M5StamPLC.readPlcRelay(i);
    }

    _lastPoll = millis();
}

void MachineIO::updateButtonTones()
{
    if (M5StamPLC.BtnA.wasPressed() || M5StamPLC.BtnB.wasPressed() || M5StamPLC.BtnC.wasPressed()) {
        M5StamPLC.tone(600, 20);
    } else if (M5StamPLC.BtnA.wasReleased() || M5StamPLC.BtnB.wasReleased() ||
               M5StamPLC.BtnC.wasReleased()) {
        M5StamPLC.tone(800, 20);
    }
}

bool MachineIO::stick(int index) const
{
    return M5StamPLC.readPlcInput(stickPin(index));
}

bool MachineIO::rawInput(int pin) const
{
    return M5StamPLC.readPlcInput(pin);
}

bool MachineIO::clawButton() const
{
    return M5StamPLC.readPlcInput(CLAW_BUTTON_PIN);
}

bool MachineIO::startJustPressed()
{
    bool input = M5StamPLC.readPlcInput(START_PIN);
    bool edge  = input && !_lastStartInput && (millis() - _lastStartEdge > START_DEBOUNCE_MS);

    if (edge) {
        _lastStartEdge = millis();
    }
    _lastStartInput = input;

    return edge;
}

bool MachineIO::configButtonClicked()
{
    return M5StamPLC.BtnB.wasClicked();
}

void MachineIO::motorRelay(int index, bool on)
{
    M5StamPLC.writePlcRelay(index, on);
}

void MachineIO::allMotorRelaysOff()
{
    for (int i = 0; i < PLC_RELAY_COUNT; i++) {
        M5StamPLC.writePlcRelay(i, false);
    }
}

void MachineIO::creditRelay(bool on)
{
    _ac.writeRelay(on);
    if (on) {
        _ac.setStatusLight(0, 1, 0);
    } else {
        _ac.setStatusLight(1, 0, 0);
    }
}

bool MachineIO::getRtcTime(struct tm* out)
{
    if (out == nullptr) {
        return false;
    }
    M5StamPLC.getRtcTime(out);
    return true;
}
```

- [ ] **Step 3: Static check against the original**

- `creditRelay(true)` matches `relayOn()`: `writeRelay(true)` + `setStatusLight(0, 1, 0)`.
- `creditRelay(false)` matches `relayOff()`: `writeRelay(false)` + `setStatusLight(1, 0, 0)`.
- `allMotorRelaysOff()` matches `allRelaysOff()`: writes `false` to relays 0–3.
- `startJustPressed()` matches `updateStartButton()`'s guard `startInput && !lastStartInput && (millis() - lastStartSend > START_DEBOUNCE_MS)`, and updates `_lastStartInput` unconditionally on every call, as the original does.
- `poll()`'s throttle uses `>` (via the early-return `<=`), matching `if (millis() - time_count > 50)`.
- `begin()` matches setup order: `M5StamPLC.begin()` then the AC retry loop with the identical message.

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. `machine_io.cpp` now compiles as part of the sketch even though nothing calls it. Expected: PASS. A failure here is a real error in the new file.

- [ ] **Step 5: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/machine_io.h claw_machine/machine_io.cpp
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: add MachineIO to own all PLC hardware access"
```

---

### Task 3: TabletLink — serial protocol

**Files:**
- Create: `claw_machine/tablet_link.h`, `claw_machine/tablet_link.cpp`

**Interfaces:**
- Consumes: `config.h` (Task 1) — `SERIAL_BAUD`.
- Produces: `class TabletLink` with `void begin(unsigned long baud = SERIAL_BAUD)`, `void onCommand(CommandHandler)`, `void pump()`, `void send(const char*)`, and the alias `using CommandHandler = std::function<void(const char*)>`.

- [ ] **Step 1: Create `claw_machine/tablet_link.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

/* Sole owner of the serial link to the tablet. No other translation unit
 * touches Serial. */
class TabletLink {
public:
    using CommandHandler = std::function<void(const char*)>;

    void begin(unsigned long baud = SERIAL_BAUD);

    void onCommand(CommandHandler handler) { _handler = std::move(handler); }

    /* Drains everything waiting on the serial port WITHOUT blocking, assembles
     * lines terminated by '\n' or '\r', and dispatches each complete line to
     * the handler.
     *
     * Must be called every loop() iteration, in every state: the USB CDC
     * receive queue holds only 256 bytes, and once it fills, the ISR silently
     * discards the rest of the packet (HWCDC.cpp, xQueueSendFromISR). That was
     * the cause of the "lost" tablet messages. */
    void pump();

    void send(const char* msg) { Serial.println(msg); }

private:
    static constexpr size_t LINE_BUFFER_SIZE = 32;

    char           _line[LINE_BUFFER_SIZE];
    size_t         _len        = 0;
    bool           _discarding = false;
    CommandHandler _handler;
};
```

- [ ] **Step 2: Create `claw_machine/tablet_link.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "tablet_link.h"

void TabletLink::begin(unsigned long baud)
{
    Serial.begin(baud);
}

void TabletLink::pump()
{
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (_discarding) {
                _discarding = false;
            } else {
                /* Trim trailing whitespace (equivalent to the old String::trim) */
                while (_len > 0 && (_line[_len - 1] == ' ' || _line[_len - 1] == '\t')) {
                    _len--;
                }
                if (_len > 0) {
                    _line[_len] = '\0';
                    if (_handler) {
                        _handler(_line);
                    }
                }
            }
            _len = 0;
            continue;
        }

        if (_discarding) {
            continue;
        }

        /* Skip leading whitespace */
        if (_len == 0 && (c == ' ' || c == '\t')) {
            continue;
        }

        if (_len < LINE_BUFFER_SIZE - 1) {
            _line[_len++] = c;
        } else {
            /* Line longer than the buffer: discard through the next terminator
             * so the remainder cannot contaminate the following command. */
            _discarding = true;
            _len        = 0;
        }
    }
}
```

- [ ] **Step 3: Static check against the original**

Compare `pump()` line by line with `pumpSerial()` in the original `.ino`:

- Terminator handling, including clearing `_discarding` on the terminator that ends an over-long line — identical.
- Trailing whitespace trim loop — identical, still `' '` and `'\t'` only.
- Empty lines produce no dispatch (`if (_len > 0)`) — identical.
- Leading whitespace skipped only while the buffer is empty — identical.
- Buffer size 32, guard `_len < LINE_BUFFER_SIZE - 1` leaving room for the NUL — identical to `sizeof(serialLine) - 1`.
- `_discarding` was a function-local `static` in the original and is now a member. Same lifetime, since there was only ever one serial link.
- The only added behavior is the `if (_handler)` null guard, which the original could not need because it called `handleCommand()` directly.

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/tablet_link.h claw_machine/tablet_link.cpp
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: add TabletLink to own the serial protocol"
```

---

### Task 4: StickMapper — joystick mapping and config mode

**Files:**
- Create: `claw_machine/stick_mapper.h`, `claw_machine/stick_mapper.cpp`

**Interfaces:**
- Consumes: `config.h` (Task 1) — `STICK_COUNT`, `stickPin()`, `STICK_RELEASE_DEBOUNCE_MS`; `MachineIO` (Task 2) — `rawInput()`, `motorRelay()`.
- Produces: `class StickMapper` with nested `enum class ConfigResult { Waiting, StepDone, Complete }`, `explicit StickMapper(MachineIO&)`, `void apply()`, `void beginConfig()`, `bool configActive() const`, `ConfigResult updateConfig()`, `const char* currentPrompt() const`, `int currentStep() const`.

- [ ] **Step 1: Create `claw_machine/stick_mapper.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "machine_io.h"

/* Owns the joystick-to-relay mapping and the interactive walkthrough that
 * records it. Deliberately knows nothing about the dashboard: updateConfig()
 * reports progress through ConfigResult and the caller does the logging. */
class StickMapper {
public:
    enum class ConfigResult {
        Waiting,   /* nothing to report this iteration */
        StepDone,  /* a mapping was recorded; more steps remain */
        Complete   /* the final mapping was recorded */
    };

    explicit StickMapper(MachineIO& io) : _io(io) {}

    /* Running mode: each motor relay follows its mapped stick input. */
    void apply();

    void beginConfig();

    bool configActive() const { return _step < STICK_COUNT; }

    /* Non-blocking. Advances the walkthrough by at most one step per call. */
    ConfigResult updateConfig();

    /* Prompt for the step currently being recorded; "" when not configuring. */
    const char* currentPrompt() const;

    int currentStep() const { return _step; }

private:
    MachineIO& _io;

    int _relayToStick[STICK_COUNT] = {STICK1_PIN, STICK2_PIN, STICK3_PIN, STICK4_PIN};

    /* _step == STICK_COUNT means "not configuring". */
    int           _step           = STICK_COUNT;
    int           _pendingPin     = -1;
    bool          _waitingRelease = false;
    unsigned long _releasedAt     = 0;

    bool anyStickPressed(int* whichPin) const;
    bool allSticksReleased() const;
};
```

- [ ] **Step 2: Create `claw_machine/stick_mapper.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stick_mapper.h"

namespace {
const char* const kConfigPrompts[STICK_COUNT] = {
    "Joystick para cima",      /* Relay 0 */
    "Joystick para esquerda",  /* Relay 1 (sentido anti-horario) */
    "Joystick para baixo",     /* Relay 2 */
    "Joystick para direita"    /* Relay 3 */
};
}  // namespace

void StickMapper::apply()
{
    for (int i = 0; i < STICK_COUNT; i++) {
        _io.motorRelay(i, _io.rawInput(_relayToStick[i]));
    }
}

void StickMapper::beginConfig()
{
    _step           = 0;
    _pendingPin     = -1;
    _waitingRelease = false;
    _releasedAt     = 0;
}

const char* StickMapper::currentPrompt() const
{
    return configActive() ? kConfigPrompts[_step] : "";
}

bool StickMapper::anyStickPressed(int* whichPin) const
{
    for (int i = 0; i < STICK_COUNT; i++) {
        if (_io.rawInput(stickPin(i))) {
            *whichPin = stickPin(i);
            return true;
        }
    }
    return false;
}

bool StickMapper::allSticksReleased() const
{
    for (int i = 0; i < STICK_COUNT; i++) {
        if (_io.rawInput(stickPin(i))) {
            return false;
        }
    }
    return true;
}

StickMapper::ConfigResult StickMapper::updateConfig()
{
    if (!configActive()) {
        return ConfigResult::Complete;
    }

    /* 1. Waiting for the user to PRESS a stick. */
    if (!_waitingRelease) {
        int pin = -1;
        if (anyStickPressed(&pin)) {
            _pendingPin     = pin;
            _waitingRelease = true;
            _releasedAt     = 0;
        }
        return ConfigResult::Waiting;
    }

    /* 2. Waiting for the user to RELEASE, then a settling window before we
     *    commit. Replaces the original blocking delay(50): the spring returning
     *    to center bounces, so we require the sticks to stay released for the
     *    whole window instead of sampling once. */
    if (!allSticksReleased()) {
        _releasedAt = 0;
        return ConfigResult::Waiting;
    }

    if (_releasedAt == 0) {
        _releasedAt = millis();
        /* 0 is our "not started" sentinel, so never store it as a timestamp. */
        if (_releasedAt == 0) {
            _releasedAt = 1;
        }
        return ConfigResult::Waiting;
    }

    if (millis() - _releasedAt < STICK_RELEASE_DEBOUNCE_MS) {
        return ConfigResult::Waiting;
    }

    /* 3. Commit the mapping and advance. */
    _relayToStick[_step] = _pendingPin;
    _pendingPin          = -1;
    _waitingRelease      = false;
    _releasedAt          = 0;
    _step++;

    return configActive() ? ConfigResult::StepDone : ConfigResult::Complete;
}
```

- [ ] **Step 3: Static check against the original**

- `apply()` matches `mapSticksToRelays()`: `writePlcRelay(i, readPlcInput(relayToStickMap[i]))` for i in 0–3.
- The default map `{STICK1_PIN, STICK2_PIN, STICK3_PIN, STICK4_PIN}` matches the original `relayToStickMap` initializer.
- Prompt strings match `configPrompts` byte for byte, including `"Joystick para cima"` with no accents. The accented word `anti-horário` appeared only in a comment and is now written `anti-horario` to keep the source ASCII; no user-visible string changed.
- Press detection scans sticks 1→4 in order and takes the first match, matching the original `if/else if` chain.
- `_pendingPin` was a `static` declared inside `case STATUS_CONFIG_MAPPING:` and is now a member — this is intentional (spec item 5).
- **Intentional behavior change (spec item 1):** the original called `delay(50)` and then committed unconditionally. This version requires the sticks to remain released for the full 50 ms and restarts the window if any stick re-asserts. Same debounce duration, no blocking, and more robust against spring bounce.

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/stick_mapper.h claw_machine/stick_mapper.cpp
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: add StickMapper with non-blocking config debounce"
```

---

### Task 5: GameStateMachine — game logic

**Files:**
- Create: `claw_machine/game_state.h`, `claw_machine/game_state.cpp`

**Interfaces:**
- Consumes: `config.h` (Task 1); `MachineIO` (Task 2) — `creditRelay()`, `allMotorRelaysOff()`, `stick()`, `clawButton()`, `startJustPressed()`, `configButtonClicked()`, `getRtcTime()`; `TabletLink` (Task 3) — `send()`; `StickMapper` (Task 4) — `apply()`, `beginConfig()`, `updateConfig()`, `currentPrompt()`, `currentStep()`; `DashboardUI` (existing) — `console_log()`, `statusTime`, `statusDate`.
- Produces: `enum class MachineStatus { Idle, RelayOn, Screen, WaitTimer, Running, ConfigMapping }` and `class GameStateMachine` with `GameStateMachine(MachineIO&, TabletLink&, StickMapper&, DashboardUI&)`, `void begin()`, `void update()`, `void onCommand(const char*)`, `void enterConfigMode()`.

- [ ] **Step 1: Create `claw_machine/game_state.h`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "dashboard_ui.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"

enum class MachineStatus {
    Idle,
    RelayOn,
    Screen,
    WaitTimer,
    Running,
    ConfigMapping
};

class GameStateMachine {
public:
    GameStateMachine(MachineIO& io, TabletLink& tablet, StickMapper& mapper, DashboardUI& ui)
        : _io(io), _tablet(tablet), _mapper(mapper), _ui(ui)
    {
    }

    void begin();
    void update();

    /* Handles a complete command from the tablet. Called by TabletLink::pump()
     * in ANY machine state. */
    void onCommand(const char* cmd);

    /* Reachable from any state, including STATUS_RUNNING. */
    void enterConfigMode();

private:
    MachineIO&   _io;
    TabletLink&  _tablet;
    StickMapper& _mapper;
    DashboardUI& _ui;

    MachineStatus _status       = MachineStatus::Idle;
    unsigned long _lastChange   = 0;
    bool          _entryPending = false;

    /* Every transition goes through here so that each state's entry action
     * fires exactly once. */
    void transitionTo(MachineStatus next);

#if ENABLE_CLOCK_DISPLAY
    void updateClock();
#endif
};
```

- [ ] **Step 2: Create `claw_machine/game_state.cpp`**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "game_state.h"

#include <string.h>

void GameStateMachine::begin()
{
    /* Explicit, known initial state: credit relay off and machine idle.
     * transitionTo() de-energizes the motor relays. */
    _io.creditRelay(false);
    transitionTo(MachineStatus::Idle);
}

void GameStateMachine::transitionTo(MachineStatus next)
{
    _status       = next;
    _lastChange   = millis();
    _entryPending = true;

    /* Safety: Running is the only state that energizes the motor relays (via
     * StickMapper::apply). We de-energize here, on entry to EVERY other state,
     * rather than at each state's exit point -- so no exit path, present or
     * future, can leave a motor latched. Config mode, for example, is reachable
     * from Running and skips that state's exit block. */
    if (_status != MachineStatus::Running) {
        _io.allMotorRelaysOff();
    }
}

void GameStateMachine::enterConfigMode()
{
    _ui.console_log("Modo Config Iniciado");

    /* Config mode can be entered from ANY state, including RelayOn, skipping
     * that state's cleanup. Release the credit relay so it cannot stay latched.
     * (The motor relays are handled by transitionTo.) */
    _io.creditRelay(false);

    transitionTo(MachineStatus::ConfigMapping);
    _mapper.beginConfig();
    _ui.console_log(_mapper.currentPrompt());
}

void GameStateMachine::onCommand(const char* cmd)
{
    if (strcmp(cmd, Protocol::CMD_COIN) == 0) {
        if (_status == MachineStatus::Idle) {
            _ui.console_log("Coin inserted!");
            _io.creditRelay(true);
            _ui.console_log("Relay On");
            transitionTo(MachineStatus::RelayOn);
        } else {
            /* Credit outside Idle: ignored, but visible on the console. */
            _ui.console_log("Coin ignorado (ocupado)");
        }
        return;
    }

    /* Unknown command: log it for diagnostics. */
    String msg = "RX? ";
    msg += cmd;
    _ui.console_log(msg.c_str());
}

#if ENABLE_CLOCK_DISPLAY
void GameStateMachine::updateClock()
{
    static uint32_t lastUpdate = 0;

    if (millis() - lastUpdate <= CLOCK_UPDATE_INTERVAL_MS) {
        return;
    }

    struct tm t;
    if (_io.getRtcTime(&t)) {
        char buf[100];

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        _ui.statusTime = buf;

        snprintf(buf, sizeof(buf), "%04d.%02d.%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        _ui.statusDate = buf;
    }

    lastUpdate = millis();
}
#endif

void GameStateMachine::update()
{
#if ENABLE_CLOCK_DISPLAY
    updateClock();
#endif

    if (_io.configButtonClicked()) {
        enterConfigMode();
    }

    /* Start button: sends its byte once per press (rising edge).
     *
     * An earlier version sent it on every iteration while the button was held.
     * That filled the 256-byte transmit buffer, made HWCDC::write block ~1 ms
     * per attempt, and stalled the loop at exactly the moment the tablet would
     * have replied. */
    if (_io.startJustPressed()) {
        _ui.console_log("Botao de inicio apertado");
        _tablet.send(Protocol::START_BUTTON);
    }

    switch (_status) {
    case MachineStatus::Idle:
        /* "coin" arrives via TabletLink::pump() -> onCommand(), which run in
         * every state. Nothing to poll here. */
        break;

    case MachineStatus::RelayOn:
        if (millis() - _lastChange >= RELAY_ON_TIME) {
            _io.creditRelay(false);
            _ui.console_log("Relay Off");
            transitionTo(MachineStatus::Screen);
        }
        break;

    case MachineStatus::Screen:
        /* Send "ready" exactly once, on entry. */
        if (_entryPending) {
            _tablet.send(Protocol::READY);
            _entryPending = false;
        }
        /* Hold 2 s for the tablet's screen transition. */
        if (millis() - _lastChange >= SCREEN_HOLD_MS) {
            _ui.console_log("Aguardando inicio");
            transitionTo(MachineStatus::WaitTimer);
        }
        break;

    case MachineStatus::WaitTimer:
        if (_io.stick(0) || _io.stick(1) || _io.stick(2) || _io.stick(3)) {
            _tablet.send(Protocol::START);
            _ui.console_log("Contagem iniciada");
            transitionTo(MachineStatus::Running);
        }
        break;

    case MachineStatus::Running:
        /* Log only on entry, not every iteration. */
        if (_entryPending) {
            _ui.console_log("Time running");
            _entryPending = false;
        }

        _mapper.apply();

        if (_io.clawButton() || millis() - _lastChange > RUNNING_TIMEOUT) {
            _tablet.send(Protocol::CLAW);
            _ui.console_log("Win/Lose");
            transitionTo(MachineStatus::Idle);
        }
        break;

    case MachineStatus::ConfigMapping:
        switch (_mapper.updateConfig()) {
        case StickMapper::ConfigResult::StepDone: {
            String msg = "Relay " + String(_mapper.currentStep() - 1) + " OK!";
            _ui.console_log(msg.c_str());
            _ui.console_log(_mapper.currentPrompt());
            break;
        }

        case StickMapper::ConfigResult::Complete: {
            String msg = "Relay " + String(STICK_COUNT - 1) + " OK!";
            _ui.console_log(msg.c_str());
            _ui.console_log("Mapeamento concluido!");
            transitionTo(MachineStatus::Idle);
            break;
        }

        case StickMapper::ConfigResult::Waiting:
        default:
            break;
        }
        break;
    }
}
```

- [ ] **Step 3: Static check — transition table**

Verify each row against the original `switch (machineStatus)`:

| From | Trigger | Emits | Logs | To |
|---|---|---|---|---|
| `Idle` | `"coin"` received | — | `Coin inserted!`, `Relay On` | `RelayOn` |
| non-`Idle` | `"coin"` received | — | `Coin ignorado (ocupado)` | unchanged |
| any | unknown command | — | `RX? <cmd>` | unchanged |
| any | start button rising edge | `0` | `Botao de inicio apertado` | unchanged |
| any | BtnB clicked | — | `Modo Config Iniciado`, prompt[0] | `ConfigMapping` |
| `RelayOn` | `>= RELAY_ON_TIME` | — | `Relay Off` | `Screen` |
| `Screen` | entry | `ready` | — | — |
| `Screen` | `>= SCREEN_HOLD_MS` | — | `Aguardando inicio` | `WaitTimer` |
| `WaitTimer` | any stick | `start` | `Contagem iniciada` | `Running` |
| `Running` | entry | — | `Time running` | — |
| `Running` | every iteration | — | — | `mapper.apply()` |
| `Running` | claw button or `> RUNNING_TIMEOUT` | `claw` | `Win/Lose` | `Idle` |
| `ConfigMapping` | step recorded, more remain | — | `Relay N OK!`, next prompt | unchanged |
| `ConfigMapping` | final step recorded | — | `Relay 3 OK!`, `Mapeamento concluido!` | `Idle` |

Also confirm:
- Comparison operators carried over exactly: `>=` for `RELAY_ON_TIME` and `SCREEN_HOLD_MS`, `>` for `RUNNING_TIMEOUT`.
- `transitionTo()` sets `_status` **before** the `!= Running` test, matching the original where `markStatusChanged()` was always called *after* `machineStatus = ...`. Getting this backwards would de-energize the motors on entry to `Running` and leave them latched on exit — the exact bug commit `5b334f1` fixed.
- `enterConfigMode()` releases the credit relay before transitioning, matching the original `relayOff()` in `update_button_events()`.
- The `"Relay N OK!"` index: the original logged `configStep` *before* incrementing it. Here `_step` has already advanced, so `StepDone` logs `currentStep() - 1` and `Complete` logs `STICK_COUNT - 1`. Confirm both produce `Relay 0 OK!` … `Relay 3 OK!` in order.
- `STATUS_NUMCOUNT` is absent (spec item 3).

- [ ] **Step 4: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/game_state.h claw_machine/game_state.cpp
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: add GameStateMachine with the six-state game logic"
```

---

### Task 6: Switch the sketch over

This is the only task that changes runtime behavior. Everything the old `.ino` did is now implemented in Tasks 1–5; this replaces the file wholesale.

**Files:**
- Modify: `claw_machine/claw_machine.ino` (replace entire contents)

**Interfaces:**
- Consumes: everything produced by Tasks 1–5, plus `DashboardUI::init()`, `DashboardUI::render()`, `DashboardUI::inputStateList`, `DashboardUI::relayStateList`.
- Produces: nothing.

- [ ] **Step 1: Replace `claw_machine/claw_machine.ino` entirely**

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <M5StamPLC.h>

#include "config.h"
#include "dashboard_ui.h"
#include "game_state.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"

MachineIO   machineIo;
TabletLink  tablet;
DashboardUI dashboardUi;

StickMapper      mapper(machineIo);
GameStateMachine game(machineIo, tablet, mapper, dashboardUi);

void setup()
{
    tablet.begin(SERIAL_BAUD);
    tablet.send("StamPLC connected to PC!");

    machineIo.begin();

    dashboardUi.init(&M5StamPLC.Display);

    tablet.onCommand([](const char* cmd) { game.onCommand(cmd); });

    game.begin();
}

void loop()
{
    /* Drain the serial port in EVERY state, every iteration. Never blocks. */
    tablet.pump();

    M5StamPLC.update();
    machineIo.poll();

    dashboardUi.inputStateList = machineIo.inputs();
    dashboardUi.relayStateList = machineIo.relays();
    dashboardUi.render();

    game.update();
}
```

- [ ] **Step 2: Static check — nothing was dropped**

Confirm every element of the original `.ino` is either reimplemented or deliberately deleted:

| Original | Now |
|---|---|
| `#define RELAY_ON_TIME` etc. | `config.h` |
| `DashboardUI dashboard_ui` | `dashboardUi` |
| `M5StamPLC_AC stamplc_ac` | `MachineIO::_ac` |
| `MachineStatus` enum | `enum class MachineStatus` (minus `STATUS_NUMCOUNT`) |
| `relayToStickMap`, `configStep`, `waitingForStickRelease`, `configPrompts` | `StickMapper` members |
| `serialLine`, `serialLineLen` | `TabletLink` members |
| `lastStartInput`, `lastStartSend` | `MachineIO` members |
| `machineStatus`, `lastStatusChange`, `statusEntryPending` | `GameStateMachine` members |
| `update_time_and_date()` | `GameStateMachine::updateClock()`, gated on `ENABLE_CLOCK_DISPLAY` |
| `update_button_events()` | split: tones → `MachineIO::updateButtonTones()`, BtnB → `GameStateMachine::update()` |
| `update_plc_io_state()` | `MachineIO::poll()` + the two assignments in `loop()` |
| `markStatusChanged()` | `GameStateMachine::transitionTo()` |
| `handleCommand()` | `GameStateMachine::onCommand()` |
| `pumpSerial()` | `TabletLink::pump()` |
| `updateStartButton()` | `MachineIO::startJustPressed()` + the send in `GameStateMachine::update()` |
| `allRelaysOff()` | `MachineIO::allMotorRelaysOff()` |
| `mapSticksToRelays()` | `StickMapper::apply()` |
| `relayOn()` / `relayOff()` | `MachineIO::creditRelay(bool)` |
| `task_write_relay_regularly()` | **deleted** (spec item 2) |
| `//Button myButton`, `//myButton.read()`, `//checkInputs()` | **deleted** (spec item 2) |

Also confirm:
- The dashboard snapshot assignments in `loop()` replace what `update_plc_io_state()` used to write directly into `dashboard_ui.inputStateList` / `relayStateList`. `PLC_INPUT_COUNT` (8) and `PLC_RELAY_COUNT` (4) must match `DashboardUI`'s `std::array<int, 8>` and `std::array<int, 4>` for the assignments to compile.
- `setup()` order matches the original: Serial, banner, `M5StamPLC.begin()`, AC retry, dashboard init, credit relay off, `Idle`.
- **Known ordering change:** the BtnB config check now runs after `dashboardUi.render()` instead of before it, because it moved into `game.update()`. This delays entering config mode by at most one frame and has no functional effect.
- Global construction order is safe: `machineIo`, `tablet`, and `dashboardUi` are declared before `mapper` and `game`, whose constructors only bind references and call nothing.

- [ ] **Step 3: Compile gate**

Ask the developer to Verify in the Arduino IDE. Expected: PASS. If the `io`-style short global name collides with anything from the ESP32 headers, that would surface here — this is why the instance is named `machineIo`.

- [ ] **Step 4: Hardware acceptance test**

Ask the developer to flash the board and walk through:

1. Idle → send `coin` from the tablet → credit relay closes, status light green, console shows `Coin inserted!` / `Relay On`.
2. After ~1 s → relay opens, light red, console `Relay Off`; `ready` reaches the tablet.
3. After a further ~2 s → console `Aguardando inicio`.
4. Push any joystick direction → `start` reaches the tablet, console `Contagem iniciada`.
5. Joystick drives the correct motors per the current mapping.
6. Press the claw button (or wait 50 s) → `claw` reaches the tablet, console `Win/Lose`, back to idle.
7. Press the physical start button → tablet receives `0` exactly once per press.
8. Press BtnB mid-game → console `Modo Config Iniciado`, credit relay released, **no motor stays energized**; walk all four prompts and confirm `Relay 0 OK!` … `Relay 3 OK!` then `Mapeamento concluido!`.
9. Send several `coin` commands back to back and confirm none are lost — this is the regression the serial pump exists to prevent.

- [ ] **Step 5: Commit**

```bash
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" add claw_machine/claw_machine.ino
git -C "C:/Users/Win 11/Documents/github/claw_machine_arduino" commit -m "refactor: reduce the sketch to wiring, setup and loop"
```

---

## Self-review notes

- **Spec coverage:** all six modules mapped to tasks 1–6; all five listed behavior changes assigned (item 1 → Task 4, items 2/3 → Task 6 and Task 5, item 4 → Tasks 1/5, item 5 → Task 4). Verification section → per-task compile gates plus Task 6 step 4.
- **Type consistency:** `creditRelay`, `allMotorRelaysOff`, `motorRelay`, `rawInput`, `stick`, `startJustPressed`, `configButtonClicked`, `apply`, `beginConfig`, `updateConfig`, `currentPrompt`, `currentStep`, `transitionTo`, `onCommand` are spelled identically in the headers that define them and every call site that uses them.
- **Known deviation from spec:** `MachineIO::begin()` returns `void` rather than `bool`, justified in Task 2.
