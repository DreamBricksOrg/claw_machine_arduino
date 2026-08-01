/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stick_mapper.h"

namespace {
const char* const kConfigPrompts[STICK_COUNT] = {
    "Joystick para cima",      /* Relay 0 */
    "Joystick para esquerda",  /* Relay 1 (sentido anti-horario) */
    "Joystick para baixo",     /* Relay 2 */
    "Joystick para direita"    /* Relay 3 */
};
}  // namespace

void StickMapper::apply()
{
    for (int i = 0; i < STICK_COUNT; i++) {
        _io.motorRelay(i, _io.rawInput(_relayToStick[i]));
    }
}

void StickMapper::beginConfig()
{
    _step           = 0;
    _pendingPin     = -1;
    _waitingRelease = false;
    _releasedAt     = 0;
}

const char* StickMapper::currentPrompt() const
{
    return configActive() ? kConfigPrompts[_step] : "";
}

bool StickMapper::anyStickPressed(int* whichPin) const
{
    for (int i = 0; i < STICK_COUNT; i++) {
        if (_io.rawInput(stickPin(i))) {
            *whichPin = stickPin(i);
            return true;
        }
    }
    return false;
}

bool StickMapper::allSticksReleased() const
{
    for (int i = 0; i < STICK_COUNT; i++) {
        if (_io.rawInput(stickPin(i))) {
            return false;
        }
    }
    return true;
}

StickMapper::ConfigResult StickMapper::updateConfig()
{
    if (!configActive()) {
        return ConfigResult::Complete;
    }

    /* 1. Waiting for the user to PRESS a stick. */
    if (!_waitingRelease) {
        int pin = -1;
        if (anyStickPressed(&pin)) {
            _pendingPin     = pin;
            _waitingRelease = true;
            _releasedAt     = 0;
        }
        return ConfigResult::Waiting;
    }

    /* 2. Waiting for the user to RELEASE, then a settling window before we
     *    commit. Replaces the original blocking delay(50): the spring returning
     *    to center bounces, so we require the sticks to stay released for the
     *    whole window instead of sampling once. */
    if (!allSticksReleased()) {
        _releasedAt = 0;
        return ConfigResult::Waiting;
    }

    if (_releasedAt == 0) {
        _releasedAt = millis();
        /* 0 is our "not started" sentinel, so never store it as a timestamp. */
        if (_releasedAt == 0) {
            _releasedAt = 1;
        }
        return ConfigResult::Waiting;
    }

    if (millis() - _releasedAt < STICK_RELEASE_DEBOUNCE_MS) {
        return ConfigResult::Waiting;
    }

    /* 3. Commit the mapping and advance. */
    _relayToStick[_step] = _pendingPin;
    _pendingPin          = -1;
    _waitingRelease      = false;
    _releasedAt          = 0;
    _step++;

    return configActive() ? ConfigResult::StepDone : ConfigResult::Complete;
}
