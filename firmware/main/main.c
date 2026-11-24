/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ITS-G5 (802.11p) receiver with display for the Waveshare/Spotpear
 * ESP32-C5-LCD-2.73.
 */
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "its.h"
#include "nvs_flash.h"

#include "board.h"
#include "buttons.h"
#include "lvgl_port.h"
#include "rx_11p.h"
#include "sd_log.h"
#include "settings.h"
#include "ui.h"
#include "usb_out.h"

static const char *TAG = "main";

/* ---- decoder store lock: strong overrides of the weak no-op hooks ---- */
static SemaphoreHandle_t s_store_mutex;

void its_store_lock(void)
{
    if (s_store_mutex) {
        xSemaphoreTake(s_store_mutex, portMAX_DELAY);
    }
}

void its_store_unlock(void)
{
    if (s_store_mutex) {
        xSemaphoreGive(s_store_mutex);
    }
}

static QueueHandle_t s_btn_q;

static void ui_init_cb(void)
{
    ui_init(s_btn_q);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ITS-G5 Empfaenger startet");
    /* the SD driver logs errors on every retry while no card is inserted */
    esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_init", ESP_LOG_NONE);
    esp_log_level_set("sdspi_transaction", ESP_LOG_NONE);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings_load();
    settings_t cfg = settings_get();

    s_store_mutex = xSemaphoreCreateMutex();
    its_store_init();

    /* display (also creates the shared SPI bus used by the SD card) */
    if (board_init() == ESP_OK) {
        s_btn_q = buttons_start();
        lvgl_port_start(ui_init_cb);
        vTaskDelay(pdMS_TO_TICKS(150)); /* let the first frame render before the backlight comes on */
        board_set_backlight(cfg.brightness);
    } else {
        ESP_LOGE(TAG, "Display-Init fehlgeschlagen, laufe ohne UI");
    }

    usb_out_init();
    sd_log_start();

    /* 802.11p receive + worker */
    ESP_ERROR_CHECK(rx_start(cfg.channel));
}
