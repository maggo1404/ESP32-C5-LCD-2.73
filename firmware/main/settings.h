/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t channel;      /* MHz, 5860..5920 */
    bool     sd_log;       /* write pcap to SD card */
    bool     usb_out;      /* SLIP raw frames on USB-Serial-JTAG */
    uint8_t  brightness;   /* 10..100 % */
    uint8_t  battery_type; /* battery_type_t: 0 = LiPo, 1 = LiFePO4, 2 = none (USB) */
    bool     auto_off;     /* low-battery deep sleep (opt-in, default off) */
} settings_t;

/* Load from NVS (call after nvs_flash_init). Falls back to defaults. */
void settings_load(void);
/* Persist the current values to NVS. */
void settings_save(void);
/* Returns a copy; the individual fields may be changed by other tasks at any time. */
settings_t settings_get(void);

void settings_set_channel(uint16_t mhz);
void settings_set_sd_log(bool on);
void settings_set_usb_out(bool on);
void settings_set_brightness(uint8_t percent);
void settings_set_auto_off(bool on);
void settings_set_battery_type(uint8_t type); /* ignored if not a valid battery_type_t */
