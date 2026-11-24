/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "its.h"
#include "lvgl.h"

/* Detail view of one traffic-light intersection: header, lane list with
 * manoeuvre arrows / light colour / countdown, and a map view. */
lv_obj_t *ui_lights_create(lv_obj_t *parent, int w, int h);

/* `is` must stay valid until the next call (the map redraws from it).
 * NULL shows a hint instead. */
void ui_lights_update(const its_store_intersection_t *is, int64_t now_us);

void ui_lights_scroll(int delta);
void ui_lights_toggle_view(void);
bool ui_lights_map_view(void);
void ui_lights_reset(void);
