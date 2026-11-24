/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Builds the whole UI. Must be called from the LVGL task (see lvgl_port).
 * `button_queue` delivers btn_event_t values (uint8_t). */
void ui_init(QueueHandle_t button_queue);
