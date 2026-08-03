/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "dashboard_ui.h"
#include "game_log.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"

enum class MachineStatus {
    Idle,
    RelayOn,
    Screen,
    WaitTimer,
    Running,
    WaitPrize,
    Freeplay,
    ConfigMapping
};

class GameStateMachine {
public:
    GameStateMachine(MachineIO& io, TabletLink& tablet, StickMapper& mapper, DashboardUI& ui,
                     GameLog& log)
        : _io(io), _tablet(tablet), _mapper(mapper), _ui(ui), _log(log)
    {
    }

    void begin();
    void update();

    /* Handles a complete command from the tablet. Called by TabletLink::pump()
     * in ANY machine state. */
    void onCommand(const char* cmd);

    /* Reachable from any state, including Running. */
    void enterConfigMode();

    /* Freeplay toggle. Reachable from any state and from either entry point
     * (front-panel BtnA or the web panel), so -- exactly like enterConfigMode()
     * -- it releases the credit relay before transitioning, and cannot leave it
     * latched when toggled mid-credit. */
    void setFreeplay(bool on);

    /* Read by WebPortal through the providers the sketch installs. */
    String statusText() const;

    /* The CSV transfer blocks loop(), so it is only allowed between games. */
    bool downloadAllowed() const;

private:
    MachineIO&   _io;
    TabletLink&  _tablet;
    StickMapper& _mapper;
    DashboardUI& _ui;
    GameLog&     _log;

    MachineStatus _status       = MachineStatus::Idle;
    unsigned long _lastChange   = 0;
    bool          _entryPending = false;

    bool          _freeplayMode  = false;
    /* Start of play. _lastChange cannot serve this role: entering WaitPrize
     * overwrites it before the duration is computed. */
    unsigned long _gameStartedAt = 0;

    /* Every transition goes through here so that each state's entry action
     * fires exactly once. */
    void transitionTo(MachineStatus next);

    /* Shared exit from WaitPrize: logs the outcome, records the row, and routes
     * back to Freeplay or Idle. */
    void finishGame(bool won);

#if ENABLE_CLOCK_DISPLAY
    void updateClock();
#endif
};
