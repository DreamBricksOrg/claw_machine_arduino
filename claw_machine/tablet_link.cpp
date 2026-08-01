/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "tablet_link.h"

void TabletLink::begin(unsigned long baud)
{
    Serial.begin(baud);
}

void TabletLink::pump()
{
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (_discarding) {
                _discarding = false;
            } else {
                /* Trim trailing whitespace (equivalent to the old String::trim) */
                while (_len > 0 && (_line[_len - 1] == ' ' || _line[_len - 1] == '\t')) {
                    _len--;
                }
                if (_len > 0) {
                    _line[_len] = '\0';
                    if (_handler) {
                        _handler(_line);
                    }
                }
            }
            _len = 0;
            continue;
        }

        if (_discarding) {
            continue;
        }

        /* Skip leading whitespace */
        if (_len == 0 && (c == ' ' || c == '\t')) {
            continue;
        }

        if (_len < LINE_BUFFER_SIZE - 1) {
            _line[_len++] = c;
        } else {
            /* Line longer than the buffer: discard through the next terminator
             * so the remainder cannot contaminate the following command. */
            _discarding = true;
            _len        = 0;
        }
    }
}
