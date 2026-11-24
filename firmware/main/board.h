/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/* Brings up I2C, the CH32V003 IO expander (LCD reset, backlight, battery ADC),
 * the shared SPI bus (LCD + SD card) and the ILI9488 panel. */
esp_err_t board_init(void);

esp_lcd_panel_handle_t board_lcd_panel(void);
esp_lcd_panel_io_handle_t board_lcd_io(void);

/* 0..100 % (the expander caps at 97 % so the panel never goes fully dark). */
void board_set_backlight(int percent);

/* Battery voltage in volts, or a negative value if it cannot be read. */
float board_battery_volts(void);
