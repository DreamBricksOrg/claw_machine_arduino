/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

/* Sole owner of the CAN link to the tablet bridge (an Arduino Nano relaying
 * to/from the tablet over its own Serial connection -- see
 * nano_can_bridge/). No other translation unit touches the TWAI driver. */
class TabletLink {
public:
    using CommandHandler = std::function<void(const char*)>;

    void begin();

    void onCommand(CommandHandler handler) { _handler = std::move(handler); }

    /* Drains every pending CAN frame WITHOUT blocking and dispatches each
     * one addressed to us to the handler as a null-terminated string.
     *
     * Must be called every loop() iteration, in every state, so frames
     * don't back up in the driver's internal RX queue. */
    void pump();

    /* msg must be <= 8 bytes -- CAN frames carry at most 8 data bytes.
     * Every Protocol:: command string satisfies this; longer strings are
     * truncated defensively rather than dropped. */
    void send(const char* msg);

private:
    static constexpr size_t MAX_PAYLOAD = 8;

    CommandHandler _handler;
};
