/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <M5StamPLC.h>
#include <array>
#include <time.h>
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

    /* Front-panel BtnA -- toggles freeplay. */
    bool freeplayButtonClicked();

    /* Prize sensor. Physical read only: the virtual-input layer deliberately
     * cannot reach this pin, so a web client cannot fake a win. */
    bool prizeSensor() const;

    /* ---- Virtual (web) inputs ----
     *
     * The web panel presses a control by calling setVirtualInput(pin, true)
     * repeatedly while it is held. Each call stamps millis(); the stamp expires
     * VIRTUAL_INPUT_TTL_MS later. A client that drops off WiFi, backgrounds its
     * browser, or loses its release message therefore releases the control
     * instead of latching it on.
     *
     * Every input reader above ORs the physical pin with its virtual stamp, so
     * StickMapper and the configuration walkthrough get web support with no
     * changes of their own. Pins outside 0..WEB_MAX_INPUT_PIN are ignored. */
    void setVirtualInput(int pin, bool state);
    void clearVirtualInputs();
    bool virtualActive(int pin) const;

    /* ---- Outputs ---- */
    void motorRelay(int index, bool on);
    void allMotorRelaysOff();

    /* Credit relay and its status light move together: green energized,
     * red released. */
    void creditRelay(bool on);

    /* ---- Misc ---- */
    bool getRtcTime(struct tm* out);

    /* Writes the RTC. Normalizes the caller's struct first -- see the .cpp. */
    bool setRtcTime(const struct tm* t);

    /* Snapshot refreshed by poll(), rendered by the dashboard. */
    const std::array<int, PLC_INPUT_COUNT>& inputs() const { return _inputs; }
    const std::array<int, PLC_RELAY_COUNT>& relays() const { return _relays; }

    /* The AC module's relay cannot be read back, so this mirrors the last
     * value written by creditRelay(). */
    bool creditRelayState() const { return _creditRelay; }

private:

    #ifndef DISABLE_AC
    M5StamPLC_AC _ac;
    #endif

    std::array<int, PLC_INPUT_COUNT> _inputs{};
    std::array<int, PLC_RELAY_COUNT> _relays{};
    bool _creditRelay = false;

    /* millis() of the last press per pin; 0 means "not pressed". */
    std::array<unsigned long, PLC_INPUT_COUNT> _virtualStamp{};

    unsigned long _lastPoll       = 0;
    bool          _lastStartInput = false;
    unsigned long _lastStartEdge  = 0;

    void updateButtonTones();
};
