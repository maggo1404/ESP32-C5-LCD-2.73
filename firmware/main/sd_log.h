/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SD_LOG_OFF = 0,     /* disabled in the settings (card unmounted) */
    SD_LOG_NO_CARD,     /* enabled, but no usable card (retrying) */
    SD_LOG_RECORDING,   /* file open, frames are written */
} sd_log_state_t;

typedef struct {
    sd_log_state_t state;
    bool error;              /* last attempt/write failed (card removed, full ...) */
    char file[24];           /* "its_0003.pcap" while recording */
    uint32_t bytes;          /* bytes written to the current file */
    uint32_t dropped;        /* frames dropped because the SD task was too slow */
    uint32_t card_mb;        /* card capacity, 0 if unknown */
} sd_log_status_t;

/* Starts the SD task. The SPI bus must already be initialised by board_init(). */
esp_err_t sd_log_start(void);

/* True while a file is open (cheap check for the worker). */
bool sd_log_active(void);

/* Queue one frame as a pcap record. Never blocks. `ts_us` is time since boot. */
void sd_log_submit(int64_t ts_us, const uint8_t *frame, size_t len);

/* Flushes and closes the pcap file, unmounts the card and keeps logging off
 * (until reboot). Blocks up to ~3 s until the SD task has finished. Call before
 * deep sleep / power loss. */
void sd_log_shutdown(void);

void sd_log_get_status(sd_log_status_t *out);
void sd_log_reset_dropped(void);
