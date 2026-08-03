/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <M5StamPLC.h>

#include "config.h"
#include "dashboard_ui.h"
#include "game_log.h"
#include "game_state.h"
#include "machine_io.h"
#include "stick_mapper.h"
#include "tablet_link.h"
#include "web_portal.h"

MachineIO   machineIo;
TabletLink  tablet;
DashboardUI dashboardUi;
GameLog     gameLog(machineIo);

StickMapper      mapper(machineIo);
GameStateMachine game(machineIo, tablet, mapper, dashboardUi, gameLog);

WebPortal web(machineIo, gameLog, dashboardUi);

void setup()
{
    tablet.begin();

    machineIo.begin();

    dashboardUi.init(&M5StamPLC.Display);

    /* After machineIo.begin(), which is what enables the SD reader, and after
     * the dashboard exists -- GameLog does not own the console, so the one boot
     * report of card health belongs here. */
    gameLog.begin();
    dashboardUi.console_log(gameLog.available() ? "SD Card OK" : "SD Card indisponivel");

    tablet.onCommand([](const char* cmd) { game.onCommand(cmd); });

    game.begin();

    /* The portal reaches the state machine only through these three callables:
     * it can submit a command, read the status string, and ask whether a
     * download is allowed. It cannot write state. Installed after game.begin()
     * so the AP only advertises once the machine is in a known state. */
    web.onCommand([](const char* cmd) { game.onCommand(cmd); });
    web.setStatusProvider([]() { return game.statusText(); });
    web.setDownloadGate([]() { return game.downloadAllowed(); });
    web.begin();
}

void loop()
{
    /* Drain the serial port in EVERY state, every iteration. Never blocks. */
    tablet.pump();

    /* Same cadence as the serial drain. */
    web.pump();

    M5StamPLC.update();
    machineIo.poll();

    dashboardUi.inputStateList = machineIo.inputs();
    dashboardUi.relayStateList   = machineIo.relays();
    dashboardUi.creditRelayState = machineIo.creditRelayState();
    dashboardUi.render();

    game.update();
}
