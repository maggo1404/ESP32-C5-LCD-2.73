/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings.h"

#include "app_config.h"
#include "battery.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";
static const char *NS = "itscfg";

static volatile settings_t s_cfg = {
    .channel = RX_CHANNEL_DEFAULT,
    .sd_log = true,
    .usb_out = false,
    .brightness = 80,
    .battery_type = DEFAULT_BATTERY_TYPE,
};

static bool channel_valid(uint16_t ch)
{
    return ch >= RX_CHANNEL_MIN_MHZ && ch <= RX_CHANNEL_MAX_MHZ &&
           (ch - RX_CHANNEL_MIN_MHZ) % RX_CHANNEL_STEP_MHZ == 0;
}

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "keine gespeicherten Einstellungen, nutze Standardwerte");
        return;
    }
    uint16_t ch;
    uint8_t v;
    if (nvs_get_u16(h, "channel", &ch) == ESP_OK && channel_valid(ch)) {
        s_cfg.channel = ch;
    }
    if (nvs_get_u8(h, "sd", &v) == ESP_OK) {
        s_cfg.sd_log = v != 0;
    }
    if (nvs_get_u8(h, "usb", &v) == ESP_OK) {
        s_cfg.usb_out = v != 0;
    }
    if (nvs_get_u8(h, "bright", &v) == ESP_OK && v >= 10 && v <= 100) {
        s_cfg.brightness = v;
    }
    if (nvs_get_u8(h, "battype", &v) == ESP_OK && v < BATTERY_TYPE_COUNT) {
        s_cfg.battery_type = v;
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
        return;
    }
    settings_t c = settings_get();
    nvs_set_u16(h, "channel", c.channel);
    nvs_set_u8(h, "sd", c.sd_log);
    nvs_set_u8(h, "usb", c.usb_out);
    nvs_set_u8(h, "bright", c.brightness);
    nvs_set_u8(h, "battype", c.battery_type);
    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

settings_t settings_get(void)
{
    settings_t c;
    c.channel = s_cfg.channel;
    c.sd_log = s_cfg.sd_log;
    c.usb_out = s_cfg.usb_out;
    c.brightness = s_cfg.brightness;
    c.battery_type = s_cfg.battery_type;
    return c;
}

void settings_set_channel(uint16_t mhz)
{
    if (channel_valid(mhz)) {
        s_cfg.channel = mhz;
    }
}
void settings_set_sd_log(bool on) { s_cfg.sd_log = on; }
void settings_set_usb_out(bool on) { s_cfg.usb_out = on; }
void settings_set_brightness(uint8_t p)
{
    if (p < 10) p = 10;
    if (p > 100) p = 100;
    s_cfg.brightness = p;
}
void settings_set_battery_type(uint8_t type)
{
    if (type < BATTERY_TYPE_COUNT) {
        s_cfg.battery_type = type;
    }
}
