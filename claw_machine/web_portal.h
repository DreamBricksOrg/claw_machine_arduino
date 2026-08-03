/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <functional>
#include "config.h"
#include "dashboard_ui.h"
#include "game_log.h"
#include "machine_io.h"

/* Sole owner of WiFi and of the HTTP server.
 *
 * Deliberately knows nothing about GameStateMachine. Commands arrive through the
 * same CommandHandler signature TabletLink uses, and status is read through a
 * provider the sketch supplies -- so the web panel is structurally incapable of
 * writing machine state. Every state change still goes through transitionTo().
 * This is what makes the monolith's "handler sets machineStatus directly" bug
 * class impossible here. */
class WebPortal {
public:
    using CommandHandler = std::function<void(const char*)>;
    using StatusProvider = std::function<String()>;
    using DownloadGate   = std::function<bool()>;

    WebPortal(MachineIO& io, GameLog& log, DashboardUI& ui)
        : _io(io), _log(log), _ui(ui), _server(WEB_SERVER_PORT)
    {
    }

    /* Brings up the access point and registers the routes. Never blocks: a radio
     * failure is logged to the console and the machine plays on without it. */
    void begin();

    /* Services at most the pending request. Call every loop(), next to
     * TabletLink::pump(). */
    void pump() { _server.handleClient(); }

    void onCommand(CommandHandler h) { _handler = std::move(h); }
    void setStatusProvider(StatusProvider p) { _status = std::move(p); }

    /* Returns false while the CSV download must be refused. streamFile() blocks
     * loop() for the whole transfer, so it cannot run during a game. */
    void setDownloadGate(DownloadGate g) { _gate = std::move(g); }

private:
    MachineIO&   _io;
    GameLog&     _log;
    DashboardUI& _ui;
    WebServer    _server;

    CommandHandler _handler;
    StatusProvider _status;
    DownloadGate   _gate;

    void handleRoot();
    void handleStatus();
    void handleCmd();
    void handleInput();
    void handleSyncTime();
    void handleDownloadCsv();
};
