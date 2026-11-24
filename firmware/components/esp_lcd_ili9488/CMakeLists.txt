#
# SPDX-FileCopyrightText: 2022 atanisoft (github.com/atanisoft)
#
# SPDX-License-Identifier: MIT
#
# Copy of atanisoft/esp_lcd_ili9488 1.0.11 as shipped in the Waveshare
# ESP32-C5-LCD-2.73 demo (with the Waveshare init sequence / byte swap).
# Local change: the RGB565->RGB666 conversion buffer is allocated from
# DMA-capable internal RAM first (PSRAM only as fallback), init table is
# iterated by array length (the Waveshare table had no terminator).

cmake_minimum_required(VERSION 3.20)

idf_component_register(SRCS "esp_lcd_ili9488.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_lcd)
