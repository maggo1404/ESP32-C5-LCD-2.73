/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* ---- Board: Waveshare/Spotpear ESP32-C5-LCD-2.73 ---- */
#define PIN_SPI_SCLK   6
#define PIN_SPI_MOSI   7
#define PIN_SPI_MISO   5
#define PIN_LCD_DC     4
#define PIN_LCD_CS     8
#define PIN_SD_CS      9
#define PIN_I2C_SDA    27
#define PIN_I2C_SCL    26
#define PIN_BTN_PREV   2
#define PIN_BTN_NEXT   3
#define PIN_BTN_ENTER  28   /* BOOT */

#define LCD_W          320
#define LCD_H          320
#define LCD_DRAW_LINES 20            /* height of one LVGL draw buffer */
#define LCD_SPI_HZ     (40 * 1000 * 1000)
#define BOARD_SPI_HOST SPI2_HOST     /* shared by LCD and SD card */

#define SD_MOUNT_POINT "/sdcard"

/* ---- Receiver ---- */
#define RX_CHANNEL_MIN_MHZ   5860
#define RX_CHANNEL_MAX_MHZ   5920
#define RX_CHANNEL_STEP_MHZ  10
#define RX_CHANNEL_DEFAULT   5900
#define RX_FRAME_MAX         1536    /* larger frames are dropped */
