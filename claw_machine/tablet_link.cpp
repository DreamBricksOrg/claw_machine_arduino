/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "tablet_link.h"

#include <M5StamPLC.h>
#include <driver/twai.h>
#include <cstring>

namespace {
constexpr TickType_t CAN_QUEUE_WAIT = 0;  // non-blocking
}  // namespace

void TabletLink::begin()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)STAMPLC_PIN_CAN_TX, (gpio_num_t)STAMPLC_PIN_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    /* Fatal at startup if the driver can't come up -- matches M5StamPLC's
     * own can_init(), which does the same for the identical call pair. */
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void TabletLink::pump()
{
    twai_message_t msg;
    while (twai_receive(&msg, CAN_QUEUE_WAIT) == ESP_OK) {
        if (msg.identifier != CanBus::ID_TABLET_TO_PLC) {
            continue;
        }

        size_t len = msg.data_length_code;
        if (len > MAX_PAYLOAD) {
            len = MAX_PAYLOAD;
        }

        char line[MAX_PAYLOAD + 1];
        memcpy(line, msg.data, len);
        line[len] = '\0';

        if (len > 0 && _handler) {
            _handler(line);
        }
    }
}

void TabletLink::send(const char* msg)
{
    size_t len = strlen(msg);
    if (len > MAX_PAYLOAD) {
        len = MAX_PAYLOAD;
    }

    twai_message_t frame = {};
    frame.identifier       = CanBus::ID_PLC_TO_TABLET;
    frame.extd             = 0;
    frame.rtr              = 0;
    frame.data_length_code = len;
    memcpy(frame.data, msg, len);

    /* Non-blocking: a momentarily full TX queue drops this frame rather
     * than stalling loop(). */
    twai_transmit(&frame, CAN_QUEUE_WAIT);
}
