/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Builds the whole UI. Must be called from the LVGL task (see lvgl_port).
 * `button_queue` delivers btn_event_t values (uint8_t). */
void ui_init(QueueHandle_t button_queue);
/* Thread-safe: makes the UI show a full-screen message (e.g. low battery
 * shutdown) on its next refresh. */
void ui_show_message(const char *text);
