# Tablet link over CAN (via Nano/MCP2515 bridge)

## Problem

The StamPLC talks to the tablet over USB-CDC serial. The tablet frequently
prompts to reconnect, and `platformio.ini` already documents a related
firmware bug (the 256-byte USB-CDC RX queue silently drops bytes once full,
losing "coin" commands until a power cycle). Both point at the same fix:
stop routing tablet traffic over the ESP32's USB-CDC serial port.

## Architecture

```
StamPLC (ESP32)  ──CAN bus (500 kbps)──  Arduino Nano + MCP2515  ──USB Serial (115200)──  Tablet
   TabletLink                                 nano_can_bridge
```

The tablet's side of the link is unchanged: newline-terminated ASCII
commands at 115200 baud. Only the ESP32↔tablet hop changes — it now goes
over CAN to a Nano, which relays bytes between CAN and its Serial port. The
Nano carries no protocol knowledge; it does not know or care what `coin` or
`ready` mean, only where to forward bytes.

## Components

### 1. `TabletLink` (StamPLC side — `claw_machine/tablet_link.h`, `.cpp`)

Public interface is unchanged from the serial version, so nothing outside
this class needs to change:

```cpp
void begin();
void onCommand(CommandHandler handler);
void pump();
void send(const char* msg);
```

`begin()` drops its `baud` parameter (CAN bus speed is a fixed shared
constant, not configurable per call site). Internals swap `Serial` for the
ESP32's native TWAI peripheral (`driver/twai.h`), on the CAN TX/RX pins
already defined by the `M5StamPLC` library (`STAMPLC_PIN_CAN_TX` = GPIO42,
`STAMPLC_PIN_CAN_RX` = GPIO43).

`TabletLink` installs and starts the TWAI driver itself in `begin()`,
independent of `M5StamPLC`'s own optional `config.enableCan` flag. This
keeps "`TabletLink` is the sole owner of the tablet link" literally true
(no dependency on `machineIo.begin()`'s call order) and matches the class's
existing ownership doc-comment, just for CAN instead of `Serial`.

- `pump()`: drains all pending RX frames with `twai_receive(&msg, 0)`
  (non-blocking, returns immediately when the queue is empty). Frames not
  addressed to `ID_TABLET_TO_PLC` are ignored. Each frame's data bytes
  become a null-terminated string (DLC ≤ 8, buffer is 9 bytes) and are
  passed to the registered handler — the same shape `onCommand` already
  expects. Still called every `loop()` iteration for the same reason as
  today: draining promptly avoids backlog in the driver's internal RX
  queue.
- `send()`: builds a single `twai_message_t` with `identifier =
  ID_PLC_TO_TABLET`, standard (non-extended) ID, DLC = `strlen(msg)`
  capped at 8, and transmits non-blocking (`twai_transmit(&msg, 0)`). The
  cap is a defensive guard — every `Protocol::` constant is ≤ 8 bytes
  today — not a normal code path.

### 2. `nano_can_bridge/nano_can_bridge.ino` (new sketch)

A new Arduino-IDE-only sketch, following the existing repo convention of
plain reference `.ino` files at the root that aren't wired into
`platformio.ini`. Requires the `mcp_can` library (Cory J Fowler,
Library Manager name `mcp_can`) and standard `SPI`.

Two non-blocking pumps, called every `loop()` iteration:

- `pumpSerialToCan()`: assembles bytes from `Serial` into a line, mirroring
  `TabletLink::pump()`'s buffering logic (skip leading whitespace, trim
  trailing whitespace, terminate on `\n`/`\r`). Buffer is sized for the
  8-byte CAN payload (9 bytes incl. terminator); an overlong line is
  discarded whole (through the next terminator) rather than truncated,
  same policy `TabletLink` already applies to its own overflow case, just
  at a tighter limit. Complete lines are sent as a CAN frame with
  `id = ID_TABLET_TO_PLC`, `len = strlen(line)`.
- `pumpCanToSerial()`: on each received frame with `id ==
  ID_PLC_TO_TABLET`, writes the frame's data bytes to `Serial` followed by
  `\n`. Frames with any other ID are ignored.

Default wiring assumption: MCP2515 module on SPI with `CS` on D10 (Nano's
standard SPI pins: SCK=13, MOSI=11, MISO=12), 8 MHz crystal. These are
`constexpr`s at the top of the sketch and easy to change if the actual
wiring differs.

## Wire protocol

| | |
|---|---|
| CAN bus speed | 500 kbps (works cleanly with both the Nano's 8 MHz MCP2515 crystal and the ESP32 TWAI peripheral) |
| ID: PLC → tablet | `0x100` (standard 11-bit) |
| ID: tablet → PLC | `0x101` (standard 11-bit) |
| Payload | Raw ASCII command string, no null terminator on the wire. DLC = string length (1–8 bytes) |
| Nano ↔ tablet (Serial) | 115200 baud, newline-terminated ASCII — identical to the current tablet-facing protocol |

Commands carried are unchanged: `coin`, `freeplay` (tablet→PLC), `ready`,
`start`, `claw` (PLC→tablet) — all ≤ 8 bytes, so every message fits in a
single CAN frame. No multi-frame reassembly is implemented.

The boot banner (`"StamPLC connected to PC!"`, 25 bytes) is dropped
entirely — it doesn't fit in one frame and isn't part of the real
protocol, just a diagnostic string.

## Config changes (`claw_machine/config.h`)

Remove `SERIAL_BAUD` (dead once `TabletLink` no longer touches `Serial`).
Add:

```cpp
namespace CanBus {
/* Shared with nano_can_bridge/ -- keep identical on both sides, they are
 * not negotiated. */
constexpr uint32_t ID_PLC_TO_TABLET = 0x100;
constexpr uint32_t ID_TABLET_TO_PLC = 0x101;
}
```

(500 kbps is a TWAI timing-config selector on the StamPLC side and a
library enum on the Nano side, not a shared numeric constant — it's
documented in both places instead.)

`claw_machine.ino` changes: `tablet.begin(SERIAL_BAUD)` → `tablet.begin()`;
the `tablet.send("StamPLC connected to PC!")` line is removed.

## Error handling

- Outbound strings > 8 bytes are truncated defensively rather than
  dropped, since `send()` is only ever called with known-good
  `Protocol::` constants.
- Inbound serial lines on the Nano longer than 8 bytes are dropped whole,
  not truncated — avoids forwarding a corrupt partial command.
- Both `pump()`/relay loops remain non-blocking every iteration, matching
  the current serial implementation's approach to not stalling `loop()`.
- CAN bus wiring note (for the build, not the firmware): a 2-node bus
  needs 120 Ω termination at both ends — most StamPLC and MCP2515 boards
  expose this as a jumper/solder pad.

## Testing

No unit test infrastructure exists in this repo — it's hardware-driven
Arduino firmware. Verification is manual/on-bench:

1. Both sketches compile (`pio run` for the StamPLC env, Arduino IDE
   verify for the Nano sketch).
2. CAN loopback bench test between the two boards (send a known string
   each direction, confirm receipt) before involving the tablet.
3. Full round-trip with the tablet attached to the Nano, replaying the
   existing manual test the serial link used (coin insert, freeplay
   toggle, start, claw actuation).

This is called out explicitly as manual verification, not automated
coverage.

## Out of scope

- Multi-frame CAN messages (nothing in the current protocol needs them).
- CAN bus arbitration/filtering beyond software ID matching (only two
  nodes exist on this bus).
- Any change to the tablet-side application — its serial protocol is
  unchanged.
