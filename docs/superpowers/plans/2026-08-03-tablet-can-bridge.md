# Tablet Link Over CAN (Nano/MCP2515 Bridge) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the StamPLC's USB-CDC serial link to the tablet with CAN, relayed through a new Arduino Nano + MCP2515 bridge sketch that talks CAN to the StamPLC and Serial to the tablet.

**Architecture:** `TabletLink` on the StamPLC keeps its existing public interface (`begin`/`onCommand`/`pump`/`send`) but its internals move from `Serial` to the ESP32's native TWAI (CAN) peripheral. A new, independent Arduino Nano sketch (`nano_can_bridge/`) is a protocol-agnostic byte relay: CAN frame in one direction, Serial line in the other, and vice versa.

**Tech Stack:** ESP32-S3 (StamPLC) via PlatformIO/`espressif32`, ESP-IDF `driver/twai.h`; Arduino Nano (ATmega328P) via PlatformIO/`atmelavr`, `coryjfowler/mcp_can` library over SPI to an MCP2515 module.

## Global Constraints

- CAN bus speed: 500 kbps (both ends).
- CAN ID, StamPLC → tablet: `0x100`. CAN ID, tablet → StamPLC: `0x101`. Standard (non-extended) 11-bit IDs.
- Every message is a single CAN frame, payload = raw ASCII, DLC = string length, max 8 bytes. No multi-frame reassembly.
- Nano ↔ tablet Serial: 115200 baud, newline-terminated ASCII — unchanged from the current tablet-facing protocol.
- MCP2515 module assumed to have an 8 MHz crystal; CS on Nano pin D10; standard Nano SPI pins otherwise.
- The boot banner `"StamPLC connected to PC!"` is removed, not carried over CAN.
- Reference: `docs/superpowers/specs/2026-08-03-tablet-can-bridge-design.md`.

---

## Task 1: Replace TabletLink's serial transport with CAN (TWAI)

**Files:**
- Modify: `claw_machine/config.h:57-59`
- Modify: `claw_machine/tablet_link.h` (full rewrite)
- Modify: `claw_machine/tablet_link.cpp` (full rewrite)
- Modify: `claw_machine/claw_machine.ino:30-31`

**Interfaces:**
- Produces: `TabletLink::begin()` (no args), `TabletLink::onCommand(CommandHandler)`, `TabletLink::pump()`, `TabletLink::send(const char*)` — identical call sites to today, used by `claw_machine.ino` and `game_state.cpp`/`game_state.h` (`GameStateMachine` holds a `TabletLink&` and calls `.send(...)` — no changes needed there).
- Produces: `CanBus::ID_PLC_TO_TABLET`, `CanBus::ID_TABLET_TO_PLC` (`uint32_t`, in `claw_machine/config.h`) — also consumed conceptually (as literal values, not a shared header) by `nano_can_bridge/nano_can_bridge.ino` in Task 2.

- [ ] **Step 1: Replace the `SERIAL_BAUD` constant with the CAN bus IDs in `config.h`**

In `claw_machine/config.h`, replace:

```cpp
/* ---- Serial ------------------------------------------------------------ */

constexpr unsigned long SERIAL_BAUD = 115200;
```

with:

```cpp
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
```

- [ ] **Step 2: Rewrite `tablet_link.h`**

Replace the full contents of `claw_machine/tablet_link.h` with:

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

/* Sole owner of the CAN link to the tablet bridge (an Arduino Nano relaying
 * to/from the tablet over its own Serial connection -- see
 * nano_can_bridge/). No other translation unit touches the TWAI driver. */
class TabletLink {
public:
    using CommandHandler = std::function<void(const char*)>;

    void begin();

    void onCommand(CommandHandler handler) { _handler = std::move(handler); }

    /* Drains every pending CAN frame WITHOUT blocking and dispatches each
     * one addressed to us to the handler as a null-terminated string.
     *
     * Must be called every loop() iteration, in every state, so frames
     * don't back up in the driver's internal RX queue. */
    void pump();

    /* msg must be <= 8 bytes -- CAN frames carry at most 8 data bytes.
     * Every Protocol:: command string satisfies this; longer strings are
     * truncated defensively rather than dropped. */
    void send(const char* msg);

private:
    static constexpr size_t MAX_PAYLOAD = 8;

    CommandHandler _handler;
};
```

- [ ] **Step 3: Rewrite `tablet_link.cpp`**

Replace the full contents of `claw_machine/tablet_link.cpp` with:

```cpp
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "tablet_link.h"

#include <M5StamPLC.h>
#include <driver/twai.h>
#include <cstring>

namespace {
constexpr TickType_t CAN_QUEUE_WAIT = 0;  // non-blocking
}  // namespace

void TabletLink::begin()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)STAMPLC_PIN_CAN_TX, (gpio_num_t)STAMPLC_PIN_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    /* Fatal at startup if the driver can't come up -- matches M5StamPLC's
     * own can_init(), which does the same for the identical call pair. */
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void TabletLink::pump()
{
    twai_message_t msg;
    while (twai_receive(&msg, CAN_QUEUE_WAIT) == ESP_OK) {
        if (msg.identifier != CanBus::ID_TABLET_TO_PLC) {
            continue;
        }

        size_t len = msg.data_length_code;
        if (len > MAX_PAYLOAD) {
            len = MAX_PAYLOAD;
        }

        char line[MAX_PAYLOAD + 1];
        memcpy(line, msg.data, len);
        line[len] = '\0';

        if (len > 0 && _handler) {
            _handler(line);
        }
    }
}

void TabletLink::send(const char* msg)
{
    size_t len = strlen(msg);
    if (len > MAX_PAYLOAD) {
        len = MAX_PAYLOAD;
    }

    twai_message_t frame = {};
    frame.identifier       = CanBus::ID_PLC_TO_TABLET;
    frame.extd             = 0;
    frame.rtr              = 0;
    frame.data_length_code = len;
    memcpy(frame.data, msg, len);

    /* Non-blocking: a momentarily full TX queue drops this frame rather
     * than stalling loop(). */
    twai_transmit(&frame, CAN_QUEUE_WAIT);
}
```

- [ ] **Step 4: Update `claw_machine.ino`'s `setup()`**

In `claw_machine/claw_machine.ino`, replace:

```cpp
    tablet.begin(SERIAL_BAUD);
    tablet.send("StamPLC connected to PC!");

    machineIo.begin();
```

with:

```cpp
    tablet.begin();

    machineIo.begin();
```

(`tablet.onCommand(...)` a few lines below is unchanged.)

- [ ] **Step 5: Build the StamPLC firmware**

Run: `pio run`
Expected: `SUCCESS` — no references to `Serial`, `SERIAL_BAUD`, or the old `begin(unsigned long)` signature remain in `claw_machine/`.

- [ ] **Step 6: Commit**

```bash
git add claw_machine/config.h claw_machine/tablet_link.h claw_machine/tablet_link.cpp claw_machine/claw_machine.ino
git commit -m "feat: move the tablet link from serial to CAN (TWAI)"
```

---

## Task 2: Create the Nano CAN↔Serial bridge sketch

**Files:**
- Create: `nano_can_bridge/platformio.ini`
- Create: `nano_can_bridge/nano_can_bridge.ino`

**Interfaces:**
- Consumes: `CanBus::ID_PLC_TO_TABLET` / `ID_TABLET_TO_PLC` values from Task 1 (`0x100`/`0x101`) as literal constants — this is a separate toolchain (AVR) so it cannot `#include` the ESP32-side header; the values are duplicated with a comment pointing at the source of truth.
- Produces: nothing consumed by other tasks — this sketch is a standalone leaf.

- [ ] **Step 1: Create the standalone PlatformIO project file**

Create `nano_can_bridge/platformio.ini`:

```ini
; PlatformIO project configuration for the Nano<->tablet CAN bridge.
;
; Standalone from the root platformio.ini on purpose: this is a different
; board and a different toolchain (atmelavr vs espressif32), relaying bytes
; between the StamPLC's CAN bus and the tablet's Serial connection. See
; ../docs/superpowers/specs/2026-08-03-tablet-can-bridge-design.md.
;
; https://docs.platformio.org/page/projectconf.html

[platformio]
src_dir = .

[env:nano_can_bridge]
platform  = atmelavr
board     = nanoatmega328
framework = arduino

lib_deps =
    coryjfowler/mcp_can@^1.5.1

monitor_speed = 115200
```

- [ ] **Step 2: Write the bridge sketch**

Create `nano_can_bridge/nano_can_bridge.ino`:

```cpp
/*
 * Bridges the StamPLC's CAN bus to the tablet's Serial connection.
 *
 * Dumb relay: forwards each complete Serial line from the tablet as a CAN
 * frame on ID_TABLET_TO_PLC, and each CAN frame received on ID_PLC_TO_TABLET
 * as a Serial line (+ '\n') to the tablet. Carries no knowledge of what the
 * command strings mean -- see
 * docs/superpowers/specs/2026-08-03-tablet-can-bridge-design.md for the
 * protocol this relays.
 *
 * IDs and CAN bus speed must match claw_machine/config.h and
 * claw_machine/tablet_link.cpp on the StamPLC side -- they are not
 * negotiated.
 */
#include <SPI.h>
#include <mcp_can.h>

/* ---- Wiring -- adjust to match your board ------------------------------ */
constexpr uint8_t CAN_CS_PIN     = 10;         // MCP2515 CS; SPI is the Nano's default (SCK13/MOSI11/MISO12)
constexpr uint8_t CAN_CLOCK_MHZ  = MCP_8MHZ;   // crystal on the MCP2515 module
constexpr long     CAN_BUS_SPEED = CAN_500KBPS;

/* ---- Shared with claw_machine/config.h's CanBus namespace --------------- */
constexpr unsigned long ID_PLC_TO_TABLET = 0x100;  // StamPLC -> Nano -> tablet
constexpr unsigned long ID_TABLET_TO_PLC = 0x101;  // tablet -> Nano -> StamPLC

constexpr unsigned long SERIAL_BAUD      = 115200;  // Nano <-> tablet, matches the tablet app
constexpr size_t        LINE_BUFFER_SIZE = 9;       // 8 CAN data bytes + null terminator

MCP_CAN CAN0(CAN_CS_PIN);

char   lineBuf[LINE_BUFFER_SIZE];
size_t lineLen    = 0;
bool   discarding = false;

/* Assembles Serial bytes from the tablet into lines and forwards each
 * complete one as a CAN frame. Mirrors TabletLink::pump()'s old
 * serial-buffering logic on the StamPLC side, just at an 8-byte limit
 * instead of 32: an overlong line is discarded whole, not truncated. */
void pumpSerialToCan()
{
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (discarding) {
                discarding = false;
            } else if (lineLen > 0) {
                lineBuf[lineLen] = '\0';
                CAN0.sendMsgBuf(ID_TABLET_TO_PLC, 0, lineLen, (uint8_t*)lineBuf);
            }
            lineLen = 0;
            continue;
        }

        if (discarding) {
            continue;
        }

        if (lineLen == 0 && (c == ' ' || c == '\t')) {
            continue;
        }

        if (lineLen < LINE_BUFFER_SIZE - 1) {
            lineBuf[lineLen++] = c;
        } else {
            discarding = true;
            lineLen    = 0;
        }
    }
}

/* Forwards every CAN frame addressed to the tablet as a Serial line. Frames
 * on any other ID are ignored. */
void pumpCanToSerial()
{
    while (CAN0.checkReceive() == CAN_MSGAVAIL) {
        unsigned long id;
        uint8_t       len;
        uint8_t       data[8];

        CAN0.readMsgBuf(&id, &len, data);

        if (id != ID_PLC_TO_TABLET) {
            continue;
        }

        Serial.write(data, len);
        Serial.write('\n');
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);

    while (CAN0.begin(MCP_ANY, CAN_BUS_SPEED, CAN_CLOCK_MHZ) != CAN_OK) {
        delay(200);  // retry until the MCP2515 answers
    }
    CAN0.setMode(MCP_NORMAL);
}

void loop()
{
    pumpSerialToCan();
    pumpCanToSerial();
}
```

- [ ] **Step 3: Build the Nano bridge firmware**

Run: `cd nano_can_bridge && pio run`
Expected: `SUCCESS` (first run downloads the `atmelavr` platform and `mcp_can` library — requires network access once).

- [ ] **Step 4: Return to the repo root**

Run: `cd ..`

- [ ] **Step 5: Commit**

```bash
git add nano_can_bridge/platformio.ini nano_can_bridge/nano_can_bridge.ino
git commit -m "feat: add the Nano CAN<->Serial bridge sketch"
```

---

## Task 3: Document the Nano bridge in the README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: nothing (documentation only).
- Produces: nothing consumed by other tasks.

- [ ] **Step 1: Append a new section to `README.md`**

Add this section at the end of `README.md` (after the existing "### Version note" section):

```markdown

## Nano CAN bridge (tablet link)

The StamPLC talks to the tablet over CAN, not USB serial -- USB-CDC serial
was dropping bytes and prompting frequent tablet reconnects (see
`docs/superpowers/specs/2026-08-03-tablet-can-bridge-design.md`). An Arduino
Nano with an MCP2515 CAN controller bridges the StamPLC's CAN bus to the
tablet's Serial connection; the sketch lives in `nano_can_bridge/`.

Wiring assumptions (see the constants at the top of `nano_can_bridge.ino` to
change these):

- MCP2515 module with an 8 MHz crystal.
- SPI on the Nano's standard pins (SCK=13, MOSI=11, MISO=12), CS on D10.
- CAN bus at 500 kbps, terminated with 120 ohm resistors at both ends.

### Arduino IDE

1. **Tools -> Manage Libraries** -> install **mcp_can** (by Cory J Fowler).
2. Open `nano_can_bridge/nano_can_bridge.ino`.
3. Select an **Arduino Nano** board (old or new bootloader, matching your
   board) and upload.

### PlatformIO

```sh
cd nano_can_bridge
pio run                 # compile
pio run -t upload       # compile and flash
```

This is a separate PlatformIO project from the one at the repo root (a
different board and toolchain), with its own `platformio.ini`.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document the Nano CAN bridge build and wiring"
```
