/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "machine_io.h"

/* Owns the joystick-to-relay mapping and the interactive walkthrough that
 * records it. Deliberately knows nothing about the dashboard: updateConfig()
 * reports progress through ConfigResult and the caller does the logging. */
class StickMapper {
public:
    enum class ConfigResult {
        Waiting,   /* nothing to report this iteration */
        StepDone,  /* a mapping was recorded; more steps remain */
        Complete   /* the final mapping was recorded */
    };

    explicit StickMapper(MachineIO& io) : _io(io) {}

    /* Running mode: each motor relay follows its mapped stick input. */
    void apply();

    void beginConfig();

    bool configActive() const { return _step < STICK_COUNT; }

    /* Non-blocking. Advances the walkthrough by at most one step per call. */
    ConfigResult updateConfig();

    /* Prompt for the step currently being recorded; "" when not configuring. */
    const char* currentPrompt() const;

    int currentStep() const { return _step; }

private:
    MachineIO& _io;

    int _relayToStick[STICK_COUNT] = {STICK1_PIN, STICK2_PIN, STICK3_PIN, STICK4_PIN};

    /* _step == STICK_COUNT means "not configuring". */
    int           _step           = STICK_COUNT;
    int           _pendingPin     = -1;
    bool          _waitingRelease = false;
    unsigned long _releasedAt     = 0;

    bool anyStickPressed(int* whichPin) const;
    bool allSticksReleased() const;
};
