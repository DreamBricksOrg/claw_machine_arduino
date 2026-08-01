/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "game_state.h"

#include <string.h>

void GameStateMachine::begin()
{
    /* Explicit, known initial state: credit relay off and machine idle.
     * transitionTo() de-energizes the motor relays. */
    _io.creditRelay(false);
    transitionTo(MachineStatus::Idle);
}

void GameStateMachine::transitionTo(MachineStatus next)
{
    _status       = next;
    _lastChange   = millis();
    _entryPending = true;

    /* Safety: Running is the only state that energizes the motor relays (via
     * StickMapper::apply). We de-energize here, on entry to EVERY other state,
     * rather than at each state's exit point -- so no exit path, present or
     * future, can leave a motor latched. Config mode, for example, is reachable
     * from Running and skips that state's exit block. */
    if (_status != MachineStatus::Running) {
        _io.allMotorRelaysOff();
    }
}

void GameStateMachine::enterConfigMode()
{
    _ui.console_log("Modo Config Iniciado");

    /* Config mode can be entered from ANY state, including RelayOn, skipping
     * that state's cleanup. Release the credit relay so it cannot stay latched.
     * (The motor relays are handled by transitionTo.) */
    _io.creditRelay(false);

    transitionTo(MachineStatus::ConfigMapping);
    _mapper.beginConfig();
    _ui.console_log(_mapper.currentPrompt());
}

void GameStateMachine::onCommand(const char* cmd)
{
    if (strcmp(cmd, Protocol::CMD_COIN) == 0) {
        if (_status == MachineStatus::Idle) {
            _ui.console_log("Coin inserted!");
            _io.creditRelay(true);
            _ui.console_log("Relay On");
            transitionTo(MachineStatus::RelayOn);
        } else {
            /* Credit outside Idle: ignored, but visible on the console. */
            _ui.console_log("Coin ignorado (ocupado)");
        }
        return;
    }

    /* Unknown command: log it for diagnostics. */
    String msg = "RX? ";
    msg += cmd;
    _ui.console_log(msg.c_str());
}

#if ENABLE_CLOCK_DISPLAY
void GameStateMachine::updateClock()
{
    static uint32_t lastUpdate = 0;

    if (millis() - lastUpdate <= CLOCK_UPDATE_INTERVAL_MS) {
        return;
    }

    struct tm t;
    if (_io.getRtcTime(&t)) {
        char buf[100];

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        _ui.statusTime = buf;

        snprintf(buf, sizeof(buf), "%04d.%02d.%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        _ui.statusDate = buf;
    }

    lastUpdate = millis();
}
#endif

void GameStateMachine::update()
{
#if ENABLE_CLOCK_DISPLAY
    updateClock();
#endif

    if (_io.configButtonClicked()) {
        enterConfigMode();
    }

    /* Start button: sends its byte once per press (rising edge).
     *
     * An earlier version sent it on every iteration while the button was held.
     * That filled the 256-byte transmit buffer, made HWCDC::write block ~1 ms
     * per attempt, and stalled the loop at exactly the moment the tablet would
     * have replied. */
    if (_io.startJustPressed()) {
        _ui.console_log("Botao de inicio apertado");
        _tablet.send(Protocol::START_BUTTON);
    }

    switch (_status) {
    case MachineStatus::Idle:
        /* "coin" arrives via TabletLink::pump() -> onCommand(), which run in
         * every state. Nothing to poll here. */
        break;

    case MachineStatus::RelayOn:
        if (millis() - _lastChange >= RELAY_ON_TIME) {
            _io.creditRelay(false);
            _ui.console_log("Relay Off");
            transitionTo(MachineStatus::Screen);
        }
        break;

    case MachineStatus::Screen:
        /* Send "ready" exactly once, on entry. */
        if (_entryPending) {
            _tablet.send(Protocol::READY);
            _entryPending = false;
        }
        /* Hold 2 s for the tablet's screen transition. */
        if (millis() - _lastChange >= SCREEN_HOLD_MS) {
            _ui.console_log("Aguardando inicio");
            transitionTo(MachineStatus::WaitTimer);
        }
        break;

    case MachineStatus::WaitTimer:
        if (_io.stick(0) || _io.stick(1) || _io.stick(2) || _io.stick(3)) {
            _tablet.send(Protocol::START);
            _ui.console_log("Contagem iniciada");
            transitionTo(MachineStatus::Running);
        }
        break;

    case MachineStatus::Running:
        /* Log only on entry, not every iteration. */
        if (_entryPending) {
            _ui.console_log("Time running");
            _entryPending = false;
        }

        _mapper.apply();

        if (_io.clawButton() || millis() - _lastChange > RUNNING_TIMEOUT) {
            _tablet.send(Protocol::CLAW);
            _ui.console_log("Win/Lose");
            transitionTo(MachineStatus::Idle);
        }
        break;

    case MachineStatus::ConfigMapping:
        switch (_mapper.updateConfig()) {
        case StickMapper::ConfigResult::StepDone: {
            String msg = "Relay " + String(_mapper.currentStep() - 1) + " OK!";
            _ui.console_log(msg.c_str());
            _ui.console_log(_mapper.currentPrompt());
            break;
        }

        case StickMapper::ConfigResult::Complete: {
            String msg = "Relay " + String(STICK_COUNT - 1) + " OK!";
            _ui.console_log(msg.c_str());
            _ui.console_log("Mapeamento concluido!");
            transitionTo(MachineStatus::Idle);
            break;
        }

        case StickMapper::ConfigResult::Waiting:
        default:
            break;
        }
        break;
    }
}
