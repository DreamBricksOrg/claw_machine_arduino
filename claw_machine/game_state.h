/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include "config.h"
#include "dashboard_ui.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"

enum class MachineStatus {
    Idle,
    RelayOn,
    Screen,
    WaitTimer,
    Running,
    ConfigMapping
};

class GameStateMachine {
public:
    GameStateMachine(MachineIO& io, TabletLink& tablet, StickMapper& mapper, DashboardUI& ui)
        : _io(io), _tablet(tablet), _mapper(mapper), _ui(ui)
    {
    }

    void begin();
    void update();

    /* Handles a complete command from the tablet. Called by TabletLink::pump()
     * in ANY machine state. */
    void onCommand(const char* cmd);

    /* Reachable from any state, including Running. */
    void enterConfigMode();

private:
    MachineIO&   _io;
    TabletLink&  _tablet;
    StickMapper& _mapper;
    DashboardUI& _ui;

    MachineStatus _status       = MachineStatus::Idle;
    unsigned long _lastChange   = 0;
    bool          _entryPending = false;

    /* Every transition goes through here so that each state's entry action
     * fires exactly once. */
    void transitionTo(MachineStatus next);

#if ENABLE_CLOCK_DISPLAY
    void updateClock();
#endif
};
