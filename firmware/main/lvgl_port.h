/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"

/* Called once inside the LVGL task after lv_init() and display registration.
 * All LVGL calls in this firmware happen in that task (no mutex needed). */
typedef void (*lvgl_port_ui_init_cb_t)(void);

esp_err_t lvgl_port_start(lvgl_port_ui_init_cb_t ui_init);
