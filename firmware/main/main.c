/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ITS-G5 (802.11p) receiver with display for the Waveshare/Spotpear
 * ESP32-C5-LCD-2.73.
 */
#include <stdarg.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
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

/* ---- log routing -----------------------------------------------------
 * The console is UART0 (primary) + USB-Serial-JTAG (secondary, see
 * sdkconfig.defaults). The USB port also carries the binary SLIP stream when
 * "USB-Ausgabe" is on; log text in the middle of a SLIP frame would corrupt
 * it. So while SLIP is enabled, runtime logs go to UART0 only (ROM tx, the
 * same path the panic handler uses for UART). Boot (ROM/early) output and
 * panic dumps still reach USB; they cannot be routed. */
static int log_vprintf(const char *fmt, va_list ap)
{
    if (!settings_get().usb_out) {
        return vprintf(fmt, ap);
    }
    char line[200];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    for (int i = 0; i < n && i < (int)sizeof(line) - 1; i++) {
        if (line[i] == '\n') {
            esp_rom_output_tx_one_char('\r');
        }
        esp_rom_output_tx_one_char((uint8_t)line[i]);
    }
    return n;
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW (esp_restart)";
    case ESP_RST_PANIC:     return "PANIC (exception/abort)";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_USB:       return "USB";
    default:                return "UNKNOWN";
    }
}

/* Boot breadcrumbs: the last "[boot] ..." line in the log is the last step that
 * was reached; the crash is in (or right after) the step that is missing. */
#define BOOT_STEP(fmt, ...)                                                                          \
    ESP_LOGI(TAG, "[boot %5lld ms] " fmt " | free int %u psram %u", (long long)(esp_timer_get_time() / 1000), \
             ##__VA_ARGS__, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),    \
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM))

/* Once per 2 s for the first minute: stack headroom of the long-living tasks.
 * A task whose high-water mark approaches 0 is about to overflow. */
static void diag_heartbeat(void)
{
    static const char *const names[] = {"main", "lvgl", "its_worker", "sd_log", "buttons", "Tmr Svc", "esp_timer"};
    for (int round = 0; round < 30; round++) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        char b[160];
        int n = snprintf(b, sizeof(b), "alive %ds, free int %u (min %u) psram %u; stack min free:", (round + 1) * 2,
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && n > 0 && n < (int)sizeof(b); i++) {
            TaskHandle_t t = xTaskGetHandle(names[i]);
            if (t) {
                n += snprintf(b + n, sizeof(b) - n, " %s=%u", names[i], (unsigned)uxTaskGetStackHighWaterMark(t));
            }
        }
        ESP_LOGI(TAG, "%s", b);
    }
}

void app_main(void)
{
    esp_log_set_vprintf(log_vprintf);
    esp_reset_reason_t rr = esp_reset_reason();
    ESP_LOGI(TAG, "ITS-G5 Empfaenger startet, letzter Reset: %s (%d)", reset_reason_str(rr), (int)rr);
    BOOT_STEP("app_main");
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
    BOOT_STEP("NVS/settings ok (channel %u, sd %d, usb %d, bright %u)", cfg.channel, cfg.sd_log, cfg.usb_out,
              cfg.brightness);

    s_store_mutex = xSemaphoreCreateMutex();
    its_store_init();
    BOOT_STEP("decoder store init ok");

    /* display (also creates the shared SPI bus used by the SD card) */
    if (board_init() == ESP_OK) {
        BOOT_STEP("board_init ok (I2C, IO expander, SPI, LCD)");
        s_btn_q = buttons_start();
        BOOT_STEP("buttons started");
        lvgl_port_start(ui_init_cb);
        BOOT_STEP("lvgl task started");
        vTaskDelay(pdMS_TO_TICKS(150)); /* let the first frame render before the backlight comes on */
        board_set_backlight(cfg.brightness);
        BOOT_STEP("backlight on");
    } else {
        ESP_LOGE(TAG, "Display-Init fehlgeschlagen, laufe ohne UI");
    }

    esp_err_t uerr = usb_out_init();
    BOOT_STEP("usb_out_init: %s", esp_err_to_name(uerr));
    esp_err_t serr = sd_log_start();
    BOOT_STEP("sd_log_start: %s", esp_err_to_name(serr));

    /* 802.11p receive + worker. Not fatal: keep the UI alive and show the error
     * in the log instead of an abort() that is invisible on USB. */
    err = rx_start(cfg.channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rx_start fehlgeschlagen: %s", esp_err_to_name(err));
    }
    BOOT_STEP("rx_start: %s", esp_err_to_name(err));
    diag_heartbeat();
}
