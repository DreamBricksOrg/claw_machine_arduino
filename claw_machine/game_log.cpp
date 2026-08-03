/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "game_log.h"

void GameLog::begin()
{
    /* NVS lives in internal flash, independent of the card: the id keeps
     * advancing even across a period with no card fitted. */
    if (_prefs.begin(NVS_NAMESPACE, false)) {
        _gameId = _prefs.getUInt(NVS_KEY_GAME_ID, 1);
    }

    File file = SD.open(HISTORY_CSV_PATH, FILE_READ);
    if (file) {
        file.close();
        _available = true;
        return;
    }

    /* No file yet: create it with the header row. This doubles as the mount
     * test -- if no card is fitted, the open fails and logging stays off. */
    file = SD.open(HISTORY_CSV_PATH, FILE_WRITE);
    if (!file) {
        _available = false;
        return;
    }

    file.println(HISTORY_CSV_HEADER);
    file.close();
    _available = true;
}

bool GameLog::record(unsigned long durationMs, bool won)
{
    if (!_available) {
        return false;
    }

    struct tm t;
    if (!_io.getRtcTime(&t)) {
        return false;
    }

    /* GAME_ID,DD/MM/YYYY,HH:MM,SECONDS,RESULT -- matches HISTORY_CSV_HEADER. */
    char line[128];
    snprintf(line, sizeof(line), "%06lu,%02d/%02d/%04d,%02d:%02d,%02lu,%s",
             (unsigned long)_gameId,
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
             t.tm_hour, t.tm_min,
             durationMs / 1000UL,
             won ? "GANHOU" : "PERDEU");

    File file = SD.open(HISTORY_CSV_PATH, FILE_APPEND);
    if (!file) {
        return false;
    }

    file.println(line);
    file.close();

    /* Only now is the id spent. */
    _gameId++;
    _prefs.putUInt(NVS_KEY_GAME_ID, _gameId);

    return true;
}

File GameLog::openForRead()
{
    if (!_available) {
        return File();
    }
    return SD.open(HISTORY_CSV_PATH, FILE_READ);
}
