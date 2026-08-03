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
    /* This class is the sole owner of the TWAI driver. M5StamPLC's own
     * config.enableCan flag (see MachineIO::begin() in machine_io.cpp) must
     * never be set true elsewhere in this codebase: M5_STAMPLC::can_init()
     * would call twai_driver_install() a second time, which fails and
     * aborts/reboots the board via ESP_ERROR_CHECK, and it defaults to
     * 1 Mbps rather than the 500 kbps used here. */
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
    /* TWAI_MODE_NORMAL requires a peer to ACK every transmitted frame. If the
     * Nano bridge is unpowered or the tablet is asleep/rebooting, failed
     * transmits push the TX error counter to BUS_OFF in well under a second
     * (send() is called on every start-button press). ESP-IDF's TWAI driver
     * does not auto-recover from BUS_OFF -- twai_transmit()/twai_receive()
     * both return ESP_ERR_INVALID_STATE until we explicitly recover. Poll
     * for that here, every loop() iteration, and drive the recovery
     * sequence (initiate -> wait for STOPPED -> restart) to completion. */
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        if (status.state == TWAI_STATE_BUS_OFF) {
            twai_initiate_recovery();
            return;
        }
        if (status.state == TWAI_STATE_STOPPED) {
            /* Recovery finished (BUS_OFF -> STOPPED); bring the driver back up. */
            twai_start();
            return;
        }
    }

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
