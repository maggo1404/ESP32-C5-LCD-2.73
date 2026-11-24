/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t cb_total;      /* promiscuous callbacks (all types) */
    uint32_t accepted;      /* broadcast frames queued to the worker */
    uint32_t drop_pool;     /* dropped: no free buffer / queue full */
    uint32_t drop_size;     /* dropped: frame larger than RX_FRAME_MAX */
    uint32_t usb_frames;    /* frames written to USB */
    uint32_t usb_dropped;   /* frames not written (host not connected counts too) */
} rx_stats_t;

/* Initialises Wi-Fi in 802.11p receive mode (no association, promiscuous),
 * starts the worker task (ingest, USB output, SD log, 1 Hz store tick). */
esp_err_t rx_start(uint16_t channel_mhz);

/* Switch the 5.9 GHz channel live (5860..5920 MHz). */
void rx_set_channel(uint16_t channel_mhz);

void rx_get_stats(rx_stats_t *out);
