# Claw Machine Firmware Modularization — Design

**Date:** 2026-08-01
**Status:** Approved, pending implementation
**Scope:** `claw_machine/claw_machine.ino` → six focused translation units

## Problem

The entire firmware lives in `claw_machine.ino` (13226 bytes): pin `#define`s, a
six-state machine implemented as a `switch` inside `loop()`, non-blocking serial
line assembly, the tablet wire protocol, joystick→relay mapping with an
interactive configuration mode, and raw `M5StamPLC` / `stamplc_ac` hardware calls
scattered throughout. `DashboardUI` is the only extracted class.

Consequences: hardware access has no single owner, so a relay can be written from
any point in the file; the protocol strings are spread across five call sites; and
the state machine cannot be read without also reading the serial parser.

## Goals

- One responsibility per translation unit, dependencies pointing one direction.
- All `M5StamPLC` / `stamplc_ac` access behind a single class.
- All `Serial` access behind a single class.
- Preserve current runtime behavior, except where explicitly listed below.

## Non-goals

- No new features.
- No changes to `DashboardUI`.
- No changes to the tablet-side protocol.

## Architecture

Dependency direction — nothing points back up:

```
claw_machine.ino
      │
      ▼
GameStateMachine ──┬──► MachineIO ────► M5StamPLC, stamplc_ac
                   ├──► TabletLink ───► Serial
                   ├──► StickMapper ──► MachineIO
                   └──► DashboardUI (unchanged)
                            │
                   all ─────┴──► config.h
```

### `config.h`

`constexpr` replacements for the current `#define`s: stick/button/start pin
numbers, `RELAY_ON_TIME`, `RUNNING_TIMEOUT`, `START_DEBOUNCE_MS`, and serial baud.
Absorbs two magic numbers currently inline in `loop()`: the 2000 ms screen hold
(`SCREEN_HOLD_MS`) and the 50 ms stick-release debounce
(`STICK_RELEASE_DEBOUNCE_MS`).

Adds `namespace Protocol` holding the wire strings — `"coin"` inbound; `"0"`,
`"ready"`, `"start"`, `"claw"` outbound — so no string literal for the protocol
appears anywhere else.

Adds `ENABLE_CLOCK_DISPLAY`, default `0`. See "Behavior changes" item 4.

### `MachineIO` — `machine_io.h/.cpp`

The only file that includes `M5StamPLC.h` or references `stamplc_ac`.

- `bool begin()` — `M5StamPLC.begin()` plus the AC retry loop.
- `void poll()` — the throttled 50 ms input/relay snapshot, plus the BtnA/B/C
  press/release tone feedback.
- `bool stick(int i) const`, `bool clawButton() const`, `bool rawInput(int pin) const`
- `bool startJustPressed()` — rising-edge detection with `START_DEBOUNCE_MS`.
  Returns the edge; does *not* send anything.
- `bool configButtonClicked()` — `BtnB.wasClicked()`.
- `void motorRelay(int i, bool on)`, `void allMotorRelaysOff()`
- `void creditRelay(bool on)` — writes the AC relay and the status light together
  (green on, red off), exactly as `relayOn()`/`relayOff()` do today.
- `bool getRtcTime(struct tm* out)`
- `const std::array<int,8>& inputs() const`, `const std::array<int,4>& relays() const`
  — the snapshot the dashboard renders.

`startJustPressed()` deliberately returns an edge rather than sending `"0"`, so
that protocol emission stays in one class.

### `TabletLink` — `tablet_link.h/.cpp`

The only file that references `Serial`.

- `void begin(unsigned long baud)`
- `void pump()` — the current non-blocking line assembly carried over verbatim:
  32-byte buffer, leading/trailing whitespace stripping, and the `discardingLine`
  overflow recovery that swallows an over-long line up to its terminator so it
  cannot contaminate the next command. Must be called every `loop()` iteration in
  every state; the USB CDC receive queue is 256 bytes and its ISR silently drops
  the remainder of a packet once full.
- `void onCommand(std::function<void(const char*)>)`
- `void send(const char* msg)`

### `StickMapper` — `stick_mapper.h/.cpp`

Owns `_relayToStick[4]` (default `{STICK1..STICK4}`) and the four prompt strings.

- `void apply()` — running mode: each motor relay follows its mapped stick input.
- `void beginConfig()` — resets step and pending state.
- `bool configActive() const`
- `ConfigResult updateConfig()` — returns `Waiting`, `StepDone`, or `Complete`.
- `const char* currentPrompt() const`

Returning an enum keeps `StickMapper` free of any `DashboardUI` dependency; the
state machine owns all console logging.

### `GameStateMachine` — `game_state.h/.cpp`

- `enum class MachineStatus { Idle, RelayOn, Screen, WaitTimer, Running, ConfigMapping }`
- Constructor takes `MachineIO&`, `TabletLink&`, `StickMapper&`, `DashboardUI&`.
- `void begin()` — credit relay off, `Idle`, initial transition.
- `void update()` — start-button handling, then the six-state switch.
- `void onCommand(const char* cmd)` — the `"coin"` handler; ignores coins outside
  `Idle` but logs them, as today.
- `void enterConfigMode()` — reachable from any state.
- `void transitionTo(MachineStatus)` — private.

**Safety invariant, preserved exactly.** `transitionTo()` assigns the new status,
stamps `_lastChange`, sets `_entryPending`, and then — if the new status is not
`Running` — calls `allMotorRelaysOff()`. Killing motors on *entry to every other
state*, rather than on exit from `Running`, means no exit path can leave a motor
latched. This matters because config mode is reachable from `Running` and skips
that state's exit block.

### `claw_machine.ino`

Reduces to instance declarations, `setup()`, and:

```cpp
void loop() {
  tablet.pump();
  M5StamPLC.update();
  io.poll();
  ui.render();
  game.update();
}
```

## Behavior changes

Everything else is a pure move. These five are intentional:

1. **Blocking `delay(50)` removed.** Config-mode release debounce becomes a
   timestamp in `StickMapper`. The original blocks 50 ms then commits
   unconditionally; the replacement waits 50 ms and re-confirms all sticks are
   still released before committing. This is a deliberate robustness improvement,
   not a pure translation. The blocking version also stalled `pump()`.
2. **Deleted:** `task_write_relay_regularly()` (dead, its `xTaskCreate` already
   commented out), `//myButton`, `//checkInputs()`.
3. **`STATUS_NUMCOUNT` deleted** — a leftover sentinel sitting mid-enum between
   `RUNNING` and `CONFIG_MAPPING`, never assigned or compared.
4. **`update_time_and_date()` kept** as `GameStateMachine::updateClock()`. Its
   call site is commented out today; instead of a commented line it is gated on
   `ENABLE_CLOCK_DISPLAY`, default `0` — identical behavior, flip to `1` to enable.
5. **`static int pendingPin` moved out of the `switch` case** into a member.
   Declaring a variable inside a `case` without braces is fragile and compiles
   today only because it is `static`.

## Verification

There is no Arduino toolchain on the development machine, so the refactor cannot
be compiled locally and no build claim will be made. Verification is:

1. Static pass: every call site checked against the new headers.
2. State-transition table diffed against the original, edge for edge.
3. Protocol strings diffed against the original call sites.
4. Developer compiles in the Arduino IDE; failures fixed from there.

Functional acceptance on hardware: coin → credit relay 1 s → `ready` → stick
input → `start` → joystick drives mapped motors → claw button or 50 s timeout →
`claw` → `Idle`; and BtnB from any state enters config mode with the credit relay
released and no motor latched.

## Workspace

Canonical repo: `C:\Users\Win 11\Documents\github\claw_machine_arduino`
(`DreamBricksOrg/claw_machine_arduino`). The Google Drive copy at
`G:\.shortcut-targets-by-id\...\monolito\monolito.ino` is a stale snapshot that
silently reverts edits and must not be used.
