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

#define LED_PIN 3
unsigned long ledOnStart = 0;

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
            } else {
                while (lineLen > 0 && (lineBuf[lineLen - 1] == ' ' || lineBuf[lineLen - 1] == '\t')) {
                    lineLen--;
                }
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    CAN0.sendMsgBuf(ID_TABLET_TO_PLC, 0, lineLen, (uint8_t*)lineBuf);
                }
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
        ledOnStart = millis();
        digitalWrite(LED_PIN, HIGH);
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);

    while (CAN0.begin(MCP_ANY, CAN_BUS_SPEED, CAN_CLOCK_MHZ) != CAN_OK) {
        delay(200);  // retry until the MCP2515 answers
    }
    CAN0.setMode(MCP_NORMAL);
    pinMode(LED_PIN, OUTPUT);
}

void loop()
{
    pumpSerialToCan();
    pumpCanToSerial();

    if (digitalRead(LED_PIN) && millis() - ledOnStart > 1000) {
      digitalWrite(LED_PIN, LOW);
    }
}
