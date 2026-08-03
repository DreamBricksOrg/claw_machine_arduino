/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include "config.h"
#include "machine_io.h"

/* Sole owner of the SD card and of NVS. No other translation unit includes
 * <SD.h> or <Preferences.h>.
 *
 * Every failure here is non-fatal by design: a machine with no card, or with a
 * card that has gone read-only, must still take coins and play. */
class GameLog {
public:
    explicit GameLog(MachineIO& io) : _io(io) {}

    /* Loads the persisted game id and makes sure the CSV exists with its header
     * row. Call AFTER MachineIO::begin(), which is what enables the SD reader. */
    void begin();

    /* False when the card is missing or unwritable. record() is a no-op and the
     * download route reports 404. */
    bool available() const { return _available; }

    /* The id the next successful record() will use. */
    uint32_t nextGameId() const { return _gameId; }

    /* Appends one row. Increments and persists the game id ONLY on a successful
     * write, so a failed write leaves no gap in the numbering. Returns whether
     * the row reached the card. */
    bool record(unsigned long durationMs, bool won);

    /* Opens the CSV for reading. Returns an invalid File when unavailable; the
     * caller must test it and is responsible for closing it. */
    File openForRead();

private:
    MachineIO&  _io;
    Preferences _prefs;
    bool        _available = false;
    uint32_t    _gameId    = 1;
};
