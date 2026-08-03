/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "machine_io.h"

void MachineIO::begin()
{
    M5StamPLC.begin();

    #ifndef DISABLE_AC
    while (!_ac.begin()) {
        printf("M5StamPLC-AC init failed, retry in 1s...\n");
        delay(1000);
    }
    #endif // DISABLE_AC
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
    /* Tracked outside the guard so the dashboard shows the intended state even
     * when the AC module is compiled out. */
    _creditRelay = on;

    #ifndef DISABLE_AC
    _ac.writeRelay(on);
    if (on) {
        _ac.setStatusLight(0, 1, 0);
    } else {
        _ac.setStatusLight(1, 0, 0);
    }
    #endif // DISABLE_AC
}

bool MachineIO::getRtcTime(struct tm* out)
{
    if (out == nullptr) {
        return false;
    }
    M5StamPLC.getRtcTime(out);
    return true;
}
