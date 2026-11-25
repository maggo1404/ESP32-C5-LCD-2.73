/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

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

/* Request a channel switch (5860..5920 MHz); done by the worker task. */
void rx_set_channel(uint16_t channel_mhz);

void rx_get_stats(rx_stats_t *out);
void rx_reset_stats(void);

/* Diagnose: print the first bytes of received frames (rejected and accepted,
 * with ITS parse result) to the log, rate-limited to 20 lines / 10 s.
 * Default from CONFIG_ITS_RX_DIAG (Kconfig, default on while bringing up). */
#ifdef CONFIG_ITS_RX_DIAG
#define RX_DIAG_DEFAULT true
#else
#define RX_DIAG_DEFAULT false
#endif
void rx_set_diag(bool on);
bool rx_get_diag(void);
