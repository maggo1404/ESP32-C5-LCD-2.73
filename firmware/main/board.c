/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io_extension.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool s_expander_ok;

esp_lcd_panel_handle_t board_lcd_panel(void) { return s_panel; }
esp_lcd_panel_io_handle_t board_lcd_io(void) { return s_io; }

esp_err_t board_init(void)
{
    /* ---- I2C + IO expander ---- */
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "I2C bus");
    s_expander_ok = i2c_master_probe(s_i2c, IO_EXTENSION_ADDR, 100) == ESP_OK &&
                    IO_EXTENSION_Init(I2C_NUM_0) == ESP_OK;
    if (!s_expander_ok) {
        ESP_LOGW(TAG, "IO expander nicht erreichbar");
    }
    /* LCD reset on expander IO0 (same sequence as the Waveshare demo) */
    IO_EXTENSION_Output(IO_EXTENSION_IO_0, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    IO_EXTENSION_Output(IO_EXTENSION_IO_0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Keep the SD card deselected (CS high) until the SD driver takes the pin. */
    gpio_reset_pin(PIN_SD_CS);
    const gpio_config_t cs_cfg = {.pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cs_cfg);
    gpio_set_level(PIN_SD_CS, 1);

    /* ---- shared SPI bus (LCD + SD card) ---- */
    const spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SPI_SCLK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* largest LCD transfer: converted RGB666 buffer of one draw buffer */
        .max_transfer_sz = LCD_W * LCD_DRAW_LINES * 3,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI bus");

    /* ---- LCD panel IO ---- */
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1, /* reset is wired to the IO expander */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 18,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9488(s_io, &panel_cfg, LCD_W * LCD_DRAW_LINES, &s_panel),
                        TAG, "ili9488");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_swap_xy(s_panel, false);
    esp_lcd_panel_mirror(s_panel, false, false);
    esp_lcd_panel_disp_on_off(s_panel, true);
    return ESP_OK;
}

void board_set_backlight(int percent)
{
    if (!s_expander_ok) {
        return;
    }
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    IO_EXTENSION_Pwm_Output((uint8_t)percent);
}

float board_battery_volts(void)
{
    /* cached: the I2C read is only repeated every 3 s so the UI never waits on it */
    static float cached = -1.0f;
    static int64_t last_us;
    if (!s_expander_ok) {
        return -1.0f;
    }
    int64_t now = esp_timer_get_time();
    if (last_us == 0 || now - last_us >= 3000000) {
        last_us = now;
        uint16_t raw = IO_EXTENSION_Adc_Input();
        if (raw == 0 || raw > 4095) {
            cached = -1.0f;
        } else {
            /* 12 bit ADC, 3.3 V reference, 1:3 divider (as in the Waveshare demo) */
            cached = (float)raw * 3.3f / 4095.0f * 3.0f;
        }
    }
    return cached;
}
