# claw_machine_arduino

Firmware for a claw machine controller running on an **M5Stack StamPLC**
(StampS3 module — ESP32-S3, 8 MB flash).

The sketch lives in `claw_machine/`. `monolito_v01.ino` and `monolito_v02.ino`
at the repo root are earlier single-file versions kept for reference; they are
not part of any build.

## Building

The same sources build under both toolchains — nothing is generated or moved
between them. Pick whichever you prefer.

### Arduino IDE

1. Add the ESP32 boards URL under **File → Preferences → Additional Boards
   Manager URLs**:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. **Tools → Board → Boards Manager** → install **esp32 by Espressif Systems**
   (2.0.x — see the version note below).
3. **Tools → Manage Libraries** → install **M5StamPLC**. It pulls in M5Unified
   and M5GFX as dependencies.
4. Open `claw_machine/claw_machine.ino`.
5. Select **M5Stack StampS3** as the board, then Verify / Upload.

### PlatformIO

```sh
pio run                 # compile
pio run -t upload       # compile and flash
pio device monitor      # serial console at 115200 baud
```

`platformio.ini` sets `src_dir = claw_machine`, so PlatformIO compiles the
Arduino sketch folder in place — there is no `src/` directory and no
`main.cpp`. Dependencies are declared in `lib_deps` and are fetched on the
first build.

### Version note

`platformio.ini` pins `espressif32@6.7.0`, which ships Arduino-ESP32 core
2.0.17. Keep the Arduino IDE on a 2.0.x core to match; core 3.x is a breaking
change to the ESP32 Arduino API and is not what this code is built against.

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
