/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "buttons.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#define POLL_MS          10
#define DEBOUNCE_SAMPLES 3      /* 30 ms stable */
#define LONG_PRESS_MS    700
#define REPEAT_DELAY_MS  450
#define REPEAT_PERIOD_MS 140

typedef struct {
    int gpio;
    bool stable;        /* debounced state, true = pressed */
    uint8_t cnt;        /* samples that differ from `stable` */
    int64_t t_press_ms;
    int64_t t_repeat_ms;
    bool long_fired;
} btn_t;

static QueueHandle_t s_q;
static btn_t s_btn[3] = {
    {.gpio = PIN_BTN_PREV},
    {.gpio = PIN_BTN_NEXT},
    {.gpio = PIN_BTN_ENTER},
};

static void post(btn_event_t ev)
{
    uint8_t e = ev;
    xQueueSend(s_q, &e, 0); /* drop if the UI is not keeping up */
}

static void buttons_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        for (int i = 0; i < 3; i++) {
            btn_t *b = &s_btn[i];
            bool raw = gpio_get_level(b->gpio) == 0; /* active low */
            if (raw != b->stable) {
                if (++b->cnt >= DEBOUNCE_SAMPLES) {
                    b->stable = raw;
                    b->cnt = 0;
                    if (raw) { /* pressed */
                        b->t_press_ms = now;
                        b->t_repeat_ms = now + REPEAT_DELAY_MS;
                        b->long_fired = false;
                        if (i == 0) post(BTN_EV_PREV);
                        if (i == 1) post(BTN_EV_NEXT);
                    } else if (i == 2 && !b->long_fired) { /* released */
                        post(BTN_EV_ENTER_SHORT);
                    }
                }
            } else {
                b->cnt = 0;
                if (b->stable) {
                    if (i == 2) {
                        if (!b->long_fired && now - b->t_press_ms >= LONG_PRESS_MS) {
                            b->long_fired = true;
                            post(BTN_EV_ENTER_LONG);
                        }
                    } else if (now >= b->t_repeat_ms) {
                        b->t_repeat_ms = now + REPEAT_PERIOD_MS;
                        post(i == 0 ? BTN_EV_PREV : BTN_EV_NEXT);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

QueueHandle_t buttons_start(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_PREV) | (1ULL << PIN_BTN_NEXT) | (1ULL << PIN_BTN_ENTER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    s_q = xQueueCreate(16, sizeof(uint8_t));
    xTaskCreate(buttons_task, "buttons", 2560, NULL, 6, NULL);
    return s_q;
}
