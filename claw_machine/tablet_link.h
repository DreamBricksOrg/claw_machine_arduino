/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

/* Sole owner of the serial link to the tablet. No other translation unit
 * touches Serial. */
class TabletLink {
public:
    using CommandHandler = std::function<void(const char*)>;

    void begin(unsigned long baud = SERIAL_BAUD);

    void onCommand(CommandHandler handler) { _handler = std::move(handler); }

    /* Drains everything waiting on the serial port WITHOUT blocking, assembles
     * lines terminated by '\n' or '\r', and dispatches each complete line to
     * the handler.
     *
     * Must be called every loop() iteration, in every state: the USB CDC
     * receive queue holds only 256 bytes, and once it fills, the ISR silently
     * discards the rest of the packet (HWCDC.cpp, xQueueSendFromISR). That was
     * the cause of the "lost" tablet messages. */
    void pump();

    void send(const char* msg) { Serial.println(msg); }

private:
    static constexpr size_t LINE_BUFFER_SIZE = 32;

    char           _line[LINE_BUFFER_SIZE];
    size_t         _len        = 0;
    bool           _discarding = false;
    CommandHandler _handler;
};
