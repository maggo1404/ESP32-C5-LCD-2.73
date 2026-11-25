/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lvgl_port.h"

#include "app_config.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lvgl_port_ui_init_cb_t s_ui_init;

/* Called by the SPI driver when the colour data of a flush has been sent.
 * The panel driver converts RGB565 into its own buffer *before* queuing, so
 * the LVGL buffer could be reused already, but we keep it simple and wait. */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    lv_disp_flush_ready(&s_disp_drv);
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    (void)drv;
    if (esp_lcd_panel_draw_bitmap(board_lcd_panel(), area->x1, area->y1, area->x2 + 1, area->y2 + 1, px) != ESP_OK) {
        lv_disp_flush_ready(&s_disp_drv); /* never leave LVGL waiting */
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "lvgl task: lv_init");
    lv_init();

    /* The panel driver swaps and converts RGB565 itself (LV_COLOR_16_SWAP=1).
     * Draw buffers live in internal RAM for fast rendering; the SPI/DMA
     * conversion buffer in the panel driver is DMA capable internal RAM. */
    const size_t px = LCD_W * LCD_DRAW_LINES;
    lv_color_t *b1 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_color_t *b2 = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "keine Draw-Buffer");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "lvgl task: draw buffers ok");
    lv_disp_draw_buf_init(&s_draw_buf, b1, b2, px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_W;
    s_disp_drv.ver_res = LCD_H;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_color_trans_done};
    esp_lcd_panel_io_register_event_callbacks(board_lcd_io(), &cbs, NULL);

    const esp_timer_create_args_t targs = {.callback = tick_cb, .name = "lv_tick"};
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, 2000));

    if (s_ui_init) {
        ESP_LOGI(TAG, "lvgl task: ui_init");
        s_ui_init();
        ESP_LOGI(TAG, "lvgl task: ui_init done, stack min free %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }

    for (;;) {
        /* always block >= 5 ms so IDLE (task watchdog) and lower tasks run */
        uint32_t wait = lv_timer_handler();
        if (wait < 5) wait = 5;
        if (wait > 30) wait = 30;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

esp_err_t lvgl_port_start(lvgl_port_ui_init_cb_t ui_init)
{
    s_ui_init = ui_init;
    BaseType_t ok = xTaskCreate(lvgl_task, "lvgl", 10 * 1024, NULL, 3, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
