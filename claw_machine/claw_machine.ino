/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <M5StamPLC.h>

#include "config.h"
#include "dashboard_ui.h"
#include "game_state.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"

MachineIO   machineIo;
TabletLink  tablet;
DashboardUI dashboardUi;

StickMapper      mapper(machineIo);
GameStateMachine game(machineIo, tablet, mapper, dashboardUi);

void setup()
{
    tablet.begin(SERIAL_BAUD);
    tablet.send("StamPLC connected to PC!");

    machineIo.begin();

    dashboardUi.init(&M5StamPLC.Display);

    tablet.onCommand([](const char* cmd) { game.onCommand(cmd); });

    game.begin();
}

void loop()
{
    /* Drain the serial port in EVERY state, every iteration. Never blocks. */
    tablet.pump();

    M5StamPLC.update();
    machineIo.poll();

    dashboardUi.inputStateList = machineIo.inputs();
    dashboardUi.relayStateList = machineIo.relays();
    dashboardUi.render();

    game.update();
}
