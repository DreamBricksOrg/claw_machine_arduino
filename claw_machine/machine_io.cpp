/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "machine_io.h"

void MachineIO::begin()
{
    /* The internal microSD reader must be enabled on the config before the
     * driver starts. GameLog mounts nothing itself; it relies on this.
     *
     * config.enableCan must stay false/unset: TabletLink (tablet_link.cpp)
     * installs and owns the TWAI driver directly, at 500 kbps. Letting
     * M5StamPLC.begin() also bring up CAN (its default is 1 Mbps) would
     * install the driver twice and abort the board via ESP_ERROR_CHECK. */
    auto config         = M5StamPLC.config();
    config.enableSdCard = true;
    M5StamPLC.config(config);

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
        _inputs[i] = M5StamPLC.readPlcInput(i) || virtualActive(i);
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

bool MachineIO::configButtonClicked()
{
    return M5StamPLC.BtnB.wasClicked();
}

bool MachineIO::freeplayButtonClicked()
{
    return M5StamPLC.BtnA.wasClicked();
}

bool MachineIO::prizeSensor() const
{
    return M5StamPLC.readPlcInput(PRIZE_SENSOR_PIN);
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
