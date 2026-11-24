/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "lvgl.h"

/* Fonts (Montserrat via TinyTTF with Latin-1; fall back to the built-in
 * Montserrat for LV_SYMBOL_* glyphs). Valid after ui_fonts_init(). */
extern const lv_font_t *ui_f12;   /* hints, tabs */
extern const lv_font_t *ui_f14;
extern const lv_font_t *ui_f20;
extern const lv_font_t *ui_f28;
void ui_fonts_init(void);

#define UI_W 320
#define UI_H 320

#define C_BG      lv_color_hex(0x10141a)
#define C_BAR     lv_color_hex(0x1c222c)
#define C_TEXT    lv_color_hex(0xe6e9ee)
#define C_DIM     lv_color_hex(0x8b94a3)
#define C_ACCENT  lv_color_hex(0x3ea6ff)
#define C_SEL     lv_color_hex(0x23405e)
#define C_OK      lv_color_hex(0x39c26a)
#define C_WARN    lv_color_hex(0xf2b83a)
#define C_ERR     lv_color_hex(0xe5483f)
