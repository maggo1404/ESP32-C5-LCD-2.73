/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    BTN_EV_PREV = 1,       /* PREV pressed (auto-repeats while held) */
    BTN_EV_NEXT,           /* NEXT pressed (auto-repeats while held) */
    BTN_EV_ENTER_SHORT,    /* ENTER released before the long-press time */
    BTN_EV_ENTER_LONG,     /* ENTER held >= 700 ms (fires once, while held) */
} btn_event_t;

/* Creates the polling task. Events (uint8_t btn_event_t) are sent to the
 * returned queue (length 16). */
QueueHandle_t buttons_start(void);
