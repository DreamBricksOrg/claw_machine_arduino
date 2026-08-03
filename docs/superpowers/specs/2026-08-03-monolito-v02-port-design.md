# Monolito v02 Feature Port — Design

**Date:** 2026-08-03
**Status:** Approved, pending implementation
**Scope:** Port the features added in `monolito_v02.ino` into the modular
`claw_machine/` firmware, without importing its regressions.

## Problem

`monolito_v01.ino` is the sketch the modular firmware was extracted from. In
parallel with that refactor, a second developer evolved the same monolith into
`monolito_v02.ino` (530 → 701 lines), adding six feature areas. Those features
are wanted; the monolith's structure is not, and v02 introduced several defects
that the modular architecture already prevents by construction.

This design specifies what to port, what to fix on the way in, and what to
deliberately leave behind.

## What v02 added

1. **WiFi AP + web control panel** — ESP32 as an access point serving a
   mobile-first HTML page: D-pad, claw, start, coin, freeplay toggle, live
   status, RTC sync, CSV download.
2. **Virtual inputs** — `isInputActive(pin)` returns `physical || web`,
   substituted for `readPlcInput` at every game-logic call site.
3. **Freeplay mode** — a `STATUS_FREEPLAY` state and `isFreeplayMode` flag,
   toggled by front-panel BtnA or from the web page.
4. **Win/lose detection** — a prize sensor on PLC input 7, sampled for 5 s after
   the claw drops.
5. **SD card history** — `/historico.csv`, one row per game, downloadable.
6. **Persistent game counter** — `gameId` in NVS, surviving reboots.

Plus: RTC clock display enabled, render throttled to 100 ms, and the start
button's serial message changed from `"0"` to `"ready"`.

## Defects in v02 (not to be ported)

| # | Defect | Disposition |
|---|--------|-------------|
| D1 | `mapSticksToRelays()` moved into the main loop — motors live in every state, including Idle and config mapping | Not ported. Mapper stays Running-only. |
| D2 | Blocking 5 s win-detection loop never drains Serial; USB CDC RX queue is 256 bytes and the ISR discards overflow | Replaced by a non-blocking `WaitPrize` state. |
| D3 | Web D-pad latches ON forever if the release request is lost | TTL-expiring virtual inputs. |
| D4 | Web handlers and BtnA write `machineStatus` directly, bypassing state cleanup; toggling freeplay mid-credit strands the credit relay | All web input routed through `onCommand()`/`setFreeplay()`, which go through `transitionTo()`. |
| D5 | Freeplay sends `start` ~1 s before Running actually begins | `start` moves to Running's entry action. |
| D6 | ~5.3 s of `delay()` per game | No `delay()` in ported code. |
| D7 | `struct tm` uninitialized in `handleSyncTime` — garbage `tm_wday`/`tm_yday`/`tm_isdst` | Zero-fill + `mktime()` normalize. |
| D8 | `gameId` increments even when the SD write fails, leaving gaps | Increment only on successful write. |
| D9 | `%06d` used for a `uint32_t` | `%06lu`. |
| D10 | `/download_csv` blocks the loop for the whole transfer | Returns 409 unless Idle or Freeplay. |
| D11 | Prize sensor pin, AP credentials, CSV path, NVS keys are magic values inline | All named constants in `config.h`. |

## Goals

- All six feature areas working in the modular firmware.
- Existing invariants preserved: `MachineIO` is the sole owner of hardware
  access; `TabletLink` is the sole owner of `Serial`; every state change goes
  through `transitionTo()`; nothing blocks `loop()`.
- Dependency direction unchanged — nothing points back up.

## Non-goals

- No changes to `DashboardUI` rendering.
- No changes to `StickMapper`'s configuration walkthrough logic.
- No HTTPS, no user accounts, no OTA.
- The monolith files are reference material; they are not built and are not
  maintained after this port.

## Decisions taken during design

| Question | Decision |
|----------|----------|
| Scope | All six feature areas in one pass. |
| Start-button signal | `Protocol::START_BUTTON` becomes `"ready"` — the tablet app was updated in parallel. Still single-send on a debounced rising edge, **not** v02's ×3 repeat. |
| Stuck web input | Auto-expire ~400 ms after the last press message; the page re-sends every 200 ms while held. |
| Freeplay credit relay | Still pulses the credit relay each round (matches v02), with the timing skew fixed. |
| WiFi AP | Always on; SSID/password become named constants in `config.h`. |

## Architecture

```
claw_machine.ino
      │
      ├──► WebPortal ──────► WiFi, WebServer ──► web_page.h
      │        │
      │        ├──► MachineIO            (virtual inputs, RTC set)
      │        └──► GameLog              (CSV download)
      │
      ▼
GameStateMachine ──┬──► MachineIO ────► M5StamPLC, stamplc_ac
                   ├──► TabletLink ───► Serial
                   ├──► StickMapper ──► MachineIO
                   ├──► GameLog ──────► SD, Preferences
                   └──► DashboardUI
                            │
                   all ─────┴──► config.h
```

`WebPortal` does **not** depend on `GameStateMachine`. It holds two callables
supplied by the sketch: a `CommandHandler` (identical in signature to
`TabletLink`'s) and a `StatusProvider` returning the display string. This keeps
the dependency graph acyclic and makes the web panel structurally incapable of
mutating machine state — it can only submit commands, exactly like the tablet.

## Module specifications

### `config.h` — additions

```cpp
constexpr int           PRIZE_SENSOR_PIN       = 7;
constexpr unsigned long PRIZE_WAIT_MS          = 5000;
constexpr unsigned long VIRTUAL_INPUT_TTL_MS   = 400;
constexpr unsigned long WEB_INPUT_REFRESH_MS   = 200;   /* mirrored by hand in web_page.h JS */

constexpr int           WEB_MAX_INPUT_PIN      = START_PIN;  /* highest web-drivable pin */

constexpr const char*   AP_SSID                = "DBAdmin";
constexpr const char*   AP_PASSWORD            = "31773177db";
constexpr uint16_t      WEB_SERVER_PORT        = 80;
constexpr unsigned long WEB_STATUS_POLL_MS     = 500;   /* mirrored by hand in web_page.h JS */

constexpr const char*   HISTORY_CSV_PATH       = "/historico.csv";
constexpr const char*   HISTORY_CSV_HEADER     = "GAME_ID,DATA,HORA,TEMPO,RESULTADO";
constexpr const char*   NVS_NAMESPACE          = "arcade";
constexpr const char*   NVS_KEY_GAME_ID        = "gameId";
```

Changes: `ENABLE_CLOCK_DISPLAY` flips `0` → `1`. `Protocol::START_BUTTON` changes
from `"0"` to `"ready"`. Adds `Protocol::CMD_FREEPLAY = "freeplay"` to the
inbound command set.

`PRIZE_SENSOR_PIN` is 7, the last of the eight PLC inputs. It is read directly,
not through `stickPin()`.

### `MachineIO` — additions

**Virtual input layer.** A parallel `std::array<unsigned long, PLC_INPUT_COUNT>`
of press timestamps.

```cpp
void setVirtualInput(int pin, bool state);   /* stamps millis() or clears */
void clearVirtualInputs();
bool virtualActive(int pin) const;           /* stamped && not expired */
```

Expiry is evaluated lazily inside `virtualActive()` rather than swept in
`poll()`, so a stamp can never be read after its deadline no matter when `poll()`
last ran. `stick()`, `rawInput()`, `clawButton()` and `startJustPressed()` return
`physical || virtualActive(pin)`. The `inputs()` snapshot feeding the dashboard
reflects the merged value, so the IO panel shows web presses — matching v02.

Because `StickMapper` reads through `rawInput()`, both the running game and the
configuration walkthrough gain web support with no changes to that class.

**Deliberate exception:** `prizeSensor()` reads the physical pin only. A virtual
win is a cheat vector, so the virtual layer must not reach it.

```cpp
bool prizeSensor() const;            /* physical PRIZE_SENSOR_PIN only */
bool freeplayButtonClicked();        /* BtnA.wasClicked() */
bool setRtcTime(const struct tm* t); /* zero-fill + mktime() normalize, then write */
```

`setRtcTime` takes a copy, zeroes `tm_wday`/`tm_yday`/`tm_isdst`, calls
`mktime()` to normalize, and only then writes to the RTC. Fixes D7.

`begin()` sets `config.enableSdCard = true` on the `M5StamPLC` config **before**
`M5StamPLC.begin()`, as v02 does. Card mounting is verified by `GameLog`.

### `GameLog` — new (`game_log.h` / `game_log.cpp`)

Sole owner of `SD` and `Preferences`. No other translation unit includes either.

```cpp
class GameLog {
public:
    void begin();                                       /* mount check, header row, load id */
    bool available() const;                             /* card usable */
    uint32_t nextGameId() const;
    bool record(unsigned long durationMs, bool won);    /* append + persist id */
    bool openForRead(File& out);                        /* for the download route */
};
```

`begin()` opens `HISTORY_CSV_PATH` for read; if absent, creates it and writes
`HISTORY_CSV_HEADER`. Loads `gameId` from NVS (`NVS_NAMESPACE`/`NVS_KEY_GAME_ID`,
default 1). Sets `_available` from whether that succeeded.

`record()` reads the RTC, formats

```
%06lu,%02d/%02d/%04d,%02d:%02d,%02lu,%s
```

(id, DD/MM/YYYY, HH:MM, duration in seconds, `GANHOU`/`PERDEU`), appends it, and
**only on a successful write** increments `gameId` and persists it via
`putUInt`. Returns success so the caller can log the outcome to the dashboard.
Fixes D8 and D9.

Failure is non-fatal everywhere: a missing card must not prevent play.

### `WebPortal` — new (`web_portal.h` / `web_portal.cpp` / `web_page.h`)

Sole owner of `WiFi` and `WebServer`. The HTML/CSS/JS document lives in
`web_page.h` as a single `static const char kWebPage[] PROGMEM` raw literal, so
`web_portal.cpp` stays readable.

```cpp
class WebPortal {
public:
    using CommandHandler = std::function<void(const char*)>;
    using StatusProvider = std::function<String()>;
    using DownloadGate   = std::function<bool()>;

    WebPortal(MachineIO& io, GameLog& log, DashboardUI& ui);

    void begin();
    void pump();                                   /* server.handleClient() */
    void onCommand(CommandHandler h);
    void setStatusProvider(StatusProvider p);
    void setDownloadGate(DownloadGate g);
};
```

Routes:

| Route | Behavior |
|-------|----------|
| `/` | serves `kWebPage` |
| `/status` | `{"status":"...","sd":true/false}` from the status provider and `GameLog::available()` |
| `/cmd?action=coin\|freeplay` | forwards the string to the command handler — never touches machine state directly |
| `/input?pin=N&state=0\|1` | `_io.setVirtualInput(pin, state)`; rejects pins outside 0..5 |
| `/sync_time?y&m&d&h&min&s` | `_io.setRtcTime()`; 400 on missing args |
| `/download_csv` | 409 unless the download gate returns true; 404 if no file; otherwise `streamFile` |

`begin()` brings up `WIFI_AP` mode with `AP_SSID`/`AP_PASSWORD` and logs the
resulting IP to the dashboard console. AP failure is non-fatal — the machine
plays normally without a radio.

Page changes versus v02's HTML: while a D-pad button is held, the press message
repeats every `WEB_INPUT_REFRESH_MS` (200 ms) instead of firing once, so the
firmware's 400 ms TTL cannot expire under the user's finger. This is the D3 fix's
other half. A `pointercancel`/`blur` handler also sends an explicit release.

### `GameStateMachine` — changes

Two new states:

```cpp
enum class MachineStatus { Idle, RelayOn, Screen, WaitTimer, Running, WaitPrize,
                           Freeplay, ConfigMapping };
```

New members: `bool _freeplayMode`, and a `GameLog&` reference.

```cpp
void setFreeplay(bool on);   /* releases credit relay, then transitionTo() */
String statusText() const;   /* the /status string */
bool downloadAllowed() const;/* _status == Idle || _status == Freeplay */
```

`setFreeplay()` mirrors `enterConfigMode()`: it releases the credit relay before
transitioning, so a toggle from `RelayOn` or `Running` cannot strand it. This is
the D4 fix. `onCommand()` gains a `Protocol::CMD_FREEPLAY` branch calling
`setFreeplay(!_freeplayMode)`. `update()` checks `_io.freeplayButtonClicked()`
alongside the existing config-button check, routing to the same method — one code
path for both entry points.

Flow:

```
Idle ──coin──▶ RelayOn ──RELAY_ON_TIME, !freeplay──▶ Screen ──SCREEN_HOLD_MS──▶
    WaitTimer ──stick──▶ Running

Idle ──freeplay──▶ Freeplay ──stick──▶ RelayOn ──RELAY_ON_TIME, freeplay──▶ Running

Running ──claw│RUNNING_TIMEOUT──▶ WaitPrize ──sensor│PRIZE_WAIT_MS──▶
    [GameLog::record] ──▶ Freeplay if _freeplayMode else Idle
```

Per-state behavior:

- **`Freeplay`** — on entry, sends `Protocol::READY` (the tablet shows its attract
  screen while waiting). On any stick press, energizes the credit relay and
  transitions to `RelayOn`.
- **`RelayOn`** — after `RELAY_ON_TIME`, releases the credit relay and goes to
  `Screen` normally, or straight to `Running` when `_freeplayMode`.
- **`Running`** — `Protocol::START` moves here, into the `_entryPending` block, so
  both entry paths send it at the instant Running begins. This removes the D5
  skew. The entry block also stamps `_gameStartedAt = millis()`; `_lastChange`
  cannot serve that role because entering `WaitPrize` overwrites it before the
  duration is computed.
- **`WaitPrize`** — entered immediately after sending `Protocol::CLAW`. Each
  iteration polls `_io.prizeSensor()`. Exits on the first HIGH reading (win) or
  after `PRIZE_WAIT_MS` (loss). On exit, logs `"VITORIA! Premio detectado"` or
  `"DERROTA! Nenhum premio"`, calls `GameLog::record(millis() - _gameStartedAt,
  won)`, reports whether the write succeeded, and transitions to `Freeplay` or
  `Idle`. Nothing blocks — `tablet.pump()` runs every iteration throughout, which
  is the D2 fix.

`transitionTo()` is unchanged and keeps de-energizing the motor relays on entry
to every state except `Running`. `_mapper.apply()` stays inside the `Running`
case. This is the D1 decision: v02's move of the mapper into the main loop is
not ported.

Game duration must be measured from `Running` entry, but `_lastChange` is
overwritten when `WaitPrize` is entered, so `Running`'s entry stamps a separate
`_gameStartedAt`.

`statusText()` returns v02's Portuguese strings, extended for the new states, but
unaccented — consistent with every other string literal in the sketch, so source
encoding cannot corrupt them on the way to the page: `AGUARDANDO FICHA`,
`LIBERANDO CREDITO`, `TELA DE ESPERA`, `AGUARDANDO INICIO`, `EM JOGO`,
`AGUARDANDO PREMIO`, `MODO FREEPLAY`, `MODO CONFIGURACAO`.

### `claw_machine.ino` — changes

```cpp
GameLog   gameLog;
WebPortal web(machineIo, gameLog, dashboardUi);
```

`setup()`: `gameLog.begin()` after `machineIo.begin()`; then

```cpp
web.onCommand([](const char* cmd) { game.onCommand(cmd); });
web.setStatusProvider([]() { return game.statusText(); });
web.setDownloadGate([]() { return game.downloadAllowed(); });
web.begin();
```

`loop()`: `web.pump()` immediately after `tablet.pump()`, so HTTP is serviced at
the same cadence as serial.

## Error handling

| Condition | Behavior |
|-----------|----------|
| SD card absent or unwritable | `GameLog::available()` false; console logs once at boot; `record()` returns false and the game continues; `/download_csv` returns 404 |
| NVS read fails | `gameId` defaults to 1 |
| WiFi AP fails to start | console log; machine plays normally with no radio |
| Malformed web request | 400, no state change |
| Download requested mid-game | 409 with a plain-text explanation |
| Web client vanishes mid-press | input expires after `VIRTUAL_INPUT_TTL_MS` |
| Prize sensor never fires | `WaitPrize` times out after `PRIZE_WAIT_MS` and records a loss |

## Verification

There is no unit test harness for this sketch, so verification is a clean
compile plus a bench checklist.

- `arduino-cli compile` clean, no new warnings.
- Flash and RAM headroom reported after the build (WiFi + WebServer + SD on top
  of M5GFX sprites is the main footprint risk; record the numbers).
- Credit → game → CSV row round trip; row contents match the played duration and
  outcome.
- Freeplay: BtnA toggles, credit relay pulses each round, machine returns to
  Freeplay after each game, BtnB exits to config with freeplay cleared.
- Win detection: trigger input 7 during the 5 s window (win) and let it lapse
  (loss); confirm the loop never stalls by watching serial traffic continue.
- Web D-pad with WiFi disconnected mid-press: relay must release within ~400 ms.
- SD card removed at boot: machine plays, console reports the card, download
  returns 404.
- Config mapping driven entirely from the phone.
- Toggle freeplay while `RelayOn` is active: credit relay must not stay latched.

## Risks

- **Memory footprint.** WiFi, WebServer, SD and the existing sprite canvases
  together are the one thing that could fail outright. Measured at the first
  compile, before any bench work.
- **Loop latency.** `server.handleClient()` on every iteration adds jitter to
  joystick→relay response. If it proves visible, the mitigation is to service
  HTTP only outside `Running`, but this is not implemented pre-emptively.
- **Protocol change.** `START_BUTTON` becoming `"ready"` requires the matching
  tablet build. Old tablet firmware will not see the start button at all.
