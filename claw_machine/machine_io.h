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

    unsigned long _lastPoll       = 0;
    bool          _lastStartInput = false;
    unsigned long _lastStartEdge  = 0;

    void updateButtonTones();
};
