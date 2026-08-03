/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "web_portal.h"

#include "web_page.h"

void WebPortal::begin()
{
    _ui.console_log("Iniciando WiFi (AP)...");

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        /* Non-fatal: no routes are registered and the machine plays normally. */
        _ui.console_log("Falha ao criar AP");
        return;
    }

    String ipLog = "IP: " + WiFi.softAPIP().toString();
    _ui.console_log("Rede Criada!");
    _ui.console_log(ipLog.c_str());

    _server.on("/", [this]() { handleRoot(); });
    _server.on("/status", [this]() { handleStatus(); });
    _server.on("/cmd", [this]() { handleCmd(); });
    _server.on("/input", [this]() { handleInput(); });
    _server.on("/sync_time", [this]() { handleSyncTime(); });
    _server.on("/download_csv", [this]() { handleDownloadCsv(); });
    _server.begin();
}

void WebPortal::handleRoot()
{
    _server.send_P(200, "text/html", kWebPage);
}

void WebPortal::handleStatus()
{
    String json = "{\"status\":\"";
    json += _status ? _status() : String("DESCONHECIDO");
    json += "\",\"sd\":";
    json += _log.available() ? "true" : "false";
    json += "}";

    _server.send(200, "application/json", json);
}

void WebPortal::handleCmd()
{
    if (!_server.hasArg("action")) {
        _server.send(400, "text/plain", "missing action");
        return;
    }

    /* Forwarded to the same handler the tablet's serial commands reach. This
     * class never touches machine state itself. */
    if (_handler) {
        _handler(_server.arg("action").c_str());
    }

    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleInput()
{
    if (!_server.hasArg("pin") || !_server.hasArg("state")) {
        _server.send(400, "text/plain", "missing pin or state");
        return;
    }

    int pin = _server.arg("pin").toInt();
    if (pin < 0 || pin > WEB_MAX_INPUT_PIN) {
        _server.send(400, "text/plain", "pin out of range");
        return;
    }

    _io.setVirtualInput(pin, _server.arg("state").toInt() == 1);
    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleSyncTime()
{
    if (!_server.hasArg("y") || !_server.hasArg("m") || !_server.hasArg("d") ||
        !_server.hasArg("h") || !_server.hasArg("min") || !_server.hasArg("s")) {
        _server.send(400, "text/plain", "Bad Request");
        return;
    }

    /* Zero-initialized, not merely assigned: MachineIO::setRtcTime() normalizes
     * with mktime(), which needs a struct with no indeterminate fields. The
     * monolith passed an uninitialized one, handing the RTC a garbage
     * day-of-week. */
    struct tm t = {};
    t.tm_year   = _server.arg("y").toInt() - 1900;
    t.tm_mon    = _server.arg("m").toInt() - 1;
    t.tm_mday   = _server.arg("d").toInt();
    t.tm_hour   = _server.arg("h").toInt();
    t.tm_min    = _server.arg("min").toInt();
    t.tm_sec    = _server.arg("s").toInt();

    if (!_io.setRtcTime(&t)) {
        _server.send(400, "text/plain", "invalid time");
        return;
    }

    _ui.console_log("Relogio web sincronizado!");
    _server.send(200, "text/plain", "OK");
}

void WebPortal::handleDownloadCsv()
{
    if (_gate && !_gate()) {
        _server.send(409, "text/plain", "Maquina ocupada");
        return;
    }

    File file = _log.openForRead();
    if (!file) {
        _server.send(404, "text/plain", "Historico nao encontrado");
        return;
    }

    _server.sendHeader("Content-Disposition", "attachment; filename=\"historico.csv\"");
    _server.streamFile(file, "text/csv");
    file.close();

    _ui.console_log("Web: Download CSV concluido");
}
