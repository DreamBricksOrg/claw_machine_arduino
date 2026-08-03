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
    _freeplayMode = false;
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

    /* Config and freeplay must not overlap: leaving freeplay armed would send
     * the walkthrough's exit straight back into a free game. */
    _freeplayMode = false;

    /* Config mode can be entered from ANY state, including RelayOn, skipping
     * that state's cleanup. Release the credit relay so it cannot stay latched.
     * (The motor relays are handled by transitionTo.) */
    _io.creditRelay(false);

    transitionTo(MachineStatus::ConfigMapping);
    _mapper.beginConfig();
    _ui.console_log(_mapper.currentPrompt());
}

void GameStateMachine::setFreeplay(bool on)
{
    _freeplayMode = on;

    /* Same hazard as config mode: reachable from RelayOn and Running, skipping
     * their cleanup. Release the credit relay so it cannot stay latched.
     * (transitionTo handles the motor relays.) */
    _io.creditRelay(false);

    if (_freeplayMode) {
        _ui.console_log("Modo Freeplay ATIVADO");
        transitionTo(MachineStatus::Freeplay);
    } else {
        _ui.console_log("Modo Freeplay DESATIVADO");
        transitionTo(MachineStatus::Idle);
    }
}

String GameStateMachine::statusText() const
{
    switch (_status) {
    case MachineStatus::Idle:          return "AGUARDANDO FICHA";
    case MachineStatus::RelayOn:       return "LIBERANDO CREDITO";
    case MachineStatus::Screen:        return "TELA DE ESPERA";
    case MachineStatus::WaitTimer:     return "AGUARDANDO INICIO";
    case MachineStatus::Running:       return "EM JOGO";
    case MachineStatus::WaitPrize:     return "AGUARDANDO PREMIO";
    case MachineStatus::Freeplay:      return "MODO FREEPLAY";
    case MachineStatus::ConfigMapping: return "MODO CONFIGURACAO";
    }
    return "DESCONHECIDO";
}

bool GameStateMachine::downloadAllowed() const
{
    return _status == MachineStatus::Idle || _status == MachineStatus::Freeplay;
}

void GameStateMachine::finishGame(bool won)
{
    _ui.console_log(won ? "VITORIA! Premio detectado" : "DERROTA! Nenhum premio");

    if (_log.record(millis() - _gameStartedAt, won)) {
        _ui.console_log("SD: Registro Salvo");
    } else {
        _ui.console_log("SD: Erro ao Salvar");
    }

    transitionTo(_freeplayMode ? MachineStatus::Freeplay : MachineStatus::Idle);
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

    if (strcmp(cmd, Protocol::CMD_FREEPLAY) == 0) {
        setFreeplay(!_freeplayMode);
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

    /* BtnA and the web panel's Freeplay button reach the same method, so the two
     * entry points cannot drift apart. */
    if (_io.freeplayButtonClicked()) {
        setFreeplay(!_freeplayMode);
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
            /* Freeplay already sent "ready" on entry to its own state, so it
             * skips the tablet's screen handshake and goes straight to play. */
            transitionTo(_freeplayMode ? MachineStatus::Running : MachineStatus::Screen);
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
            _ui.console_log("Contagem iniciada");
            transitionTo(MachineStatus::Running);
        }
        break;

    case MachineStatus::Freeplay:
        /* On entry, put the tablet on its attract screen and wait. */
        if (_entryPending) {
            _ui.console_log("Freeplay: aguardando jogada");
            _tablet.send(Protocol::READY);
            _entryPending = false;
        }

        if (_io.stick(0) || _io.stick(1) || _io.stick(2) || _io.stick(3)) {
            /* A free game still issues a real credit, so the machine's own
             * counters stay consistent with the tablet's. RelayOn releases it
             * after RELAY_ON_TIME and routes back here to Running. */
            _io.creditRelay(true);
            _ui.console_log("Relay On (Freeplay)");
            transitionTo(MachineStatus::RelayOn);
        }
        break;

    case MachineStatus::Running:
        if (_entryPending) {
            /* "start" is sent HERE, not at the stick press, so both entry paths
             * -- WaitTimer and the freeplay credit pulse -- start the tablet's
             * timer at the instant the joystick actually drives the motors. In
             * the monolith the freeplay path sent it a full second early. */
            _tablet.send(Protocol::START);
            _ui.console_log("Time running");
            _gameStartedAt = millis();
            _entryPending  = false;
        }

        _mapper.apply();

        if (_io.clawButton() || millis() - _lastChange > RUNNING_TIMEOUT) {
            _tablet.send(Protocol::CLAW);
            _ui.console_log("Aguardando sensor...");
            transitionTo(MachineStatus::WaitPrize);
        }
        break;

    case MachineStatus::WaitPrize:
        /* Replaces the monolith's blocking 5 s while-loop. As a state, it lets
         * loop() keep calling TabletLink::pump() throughout: the USB CDC receive
         * queue holds 256 bytes and the ISR silently discards the overflow, so
         * five blocked seconds cost real tablet messages.
         *
         * transitionTo() has already de-energized the motor relays on entry. */
        if (_io.prizeSensor()) {
            finishGame(true);
        } else if (millis() - _lastChange >= PRIZE_WAIT_MS) {
            finishGame(false);
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
