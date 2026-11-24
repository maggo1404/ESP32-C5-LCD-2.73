/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rx_11p.h"

#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "its.h"
#include "sd_log.h"
#include "settings.h"
#include "usb_out.h"

/* Not declared in any public header: 802.11p switches inside the PHY library
 * (libphy.a). Signatures as used by OpenTrafficMap's ITS-G5 receiver. */
extern void phy_11p_set(int enable, int unknown);
extern void phy_change_channel(int freq_mhz, int unknown1, int unknown2, int ht_mode);

static const char *TAG = "rx_11p";

#define POOL_SLOTS 24
#define SLOT_SIZE  RX_FRAME_MAX

typedef struct {
    int64_t ts_us;
    int8_t rssi;
    uint16_t len;
    uint8_t slot;
} rx_item_t;

static uint8_t *s_pool;               /* POOL_SLOTS * SLOT_SIZE, PSRAM */
static QueueHandle_t s_free_q;        /* uint8_t slot indices */
static QueueHandle_t s_work_q;        /* rx_item_t */
static rx_stats_t s_stats;
static volatile uint16_t s_pending_channel; /* 0 = none */
static const uint8_t BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* ---- Wi-Fi promiscuous callback (runs in the Wi-Fi task, no malloc) ---- */
static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    s_stats.cb_total++;
    if (type == WIFI_PKT_MISC) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (pkt->rx_ctrl.rx_state != 0 || pkt->rx_ctrl.sig_len <= 4) {
        return;
    }
    uint32_t len = pkt->rx_ctrl.sig_len - 4; /* strip FCS */
#if CONFIG_SOC_WIFI_HE_SUPPORT
    /* never read beyond what the driver actually delivered */
    if (pkt->rx_ctrl.dump_len > 0 && pkt->rx_ctrl.dump_len < len) {
        len = pkt->rx_ctrl.dump_len;
    }
#endif
    /* broadcast only: addr1 (bytes 4..9) = ff:ff:ff:ff:ff:ff */
    if (len < 10 || memcmp(&pkt->payload[4], BCAST, 6) != 0) {
        return;
    }
    if (len > SLOT_SIZE) {
        s_stats.drop_size++;
        return;
    }
    uint8_t slot;
    if (xQueueReceive(s_free_q, &slot, 0) != pdTRUE) {
        s_stats.drop_pool++;
        return;
    }
    memcpy(s_pool + (size_t)slot * SLOT_SIZE, pkt->payload, len);
    const rx_item_t it = {
        .ts_us = esp_timer_get_time(),
        .rssi = (int8_t)pkt->rx_ctrl.rssi,
        .len = (uint16_t)len,
        .slot = slot,
    };
    if (xQueueSend(s_work_q, &it, 0) != pdTRUE) {
        xQueueSend(s_free_q, &slot, 0);
        s_stats.drop_pool++;
        return;
    }
    s_stats.accepted++;
}

/* ---- Worker: decode, USB, SD, 1 Hz tick ---- */
static void worker_task(void *arg)
{
    (void)arg;
    int64_t last_tick = esp_timer_get_time();
    for (;;) {
        rx_item_t it;
        if (xQueueReceive(s_work_q, &it, pdMS_TO_TICKS(100)) == pdTRUE) {
            const uint8_t *frame = s_pool + (size_t)it.slot * SLOT_SIZE;
            its_store_ingest(frame, it.len, it.rssi, it.ts_us);

            if (settings_get().usb_out) {
                if (usb_out_send_frame(frame, it.len)) {
                    s_stats.usb_frames++;
                } else {
                    s_stats.usb_dropped++;
                }
            }
            if (sd_log_active()) {
                sd_log_submit(it.ts_us, frame, it.len);
            }
            xQueueSend(s_free_q, &it.slot, 0);
        }
        uint16_t ch = s_pending_channel;
        if (ch) {
            s_pending_channel = 0;
            ESP_LOGI(TAG, "Kanal %u MHz", ch);
            phy_change_channel(ch, 1, 0, 0);
            /* drop frames queued from the previous channel */
            rx_item_t old;
            while (xQueueReceive(s_work_q, &old, 0) == pdTRUE) {
                xQueueSend(s_free_q, &old.slot, 0);
            }
        }
        int64_t now = esp_timer_get_time();
        if (now - last_tick >= 1000000) {
            last_tick = now;
            its_store_tick(now);
        }
    }
}

void rx_set_channel(uint16_t channel_mhz)
{
    /* executed by the worker task (not by the caller, e.g. the LVGL task) */
    s_pending_channel = channel_mhz;
}

void rx_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

void rx_get_stats(rx_stats_t *out)
{
    *out = s_stats;
}

esp_err_t rx_start(uint16_t channel_mhz)
{
    /* Frame pool lives in PSRAM: internal RAM is scarce (Wi-Fi, LVGL, SPI DMA). */
    s_pool = heap_caps_malloc((size_t)POOL_SLOTS * SLOT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pool) {
        s_pool = heap_caps_malloc((size_t)POOL_SLOTS * SLOT_SIZE, MALLOC_CAP_8BIT);
    }
    s_free_q = xQueueCreate(POOL_SLOTS, sizeof(uint8_t));
    s_work_q = xQueueCreate(POOL_SLOTS, sizeof(rx_item_t));
    if (!s_pool || !s_free_q || !s_work_q) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < POOL_SLOTS; i++) {
        xQueueSend(s_free_q, &i, 0);
    }
    if (xTaskCreate(worker_task, "its_worker", 10 * 1024, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* Same bring-up as the OpenTrafficMap / simple-its receivers. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL & ~WIFI_PROMIS_FILTER_MASK_FCSFAIL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(rx_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    phy_11p_set(1, 0);
    /* a channel close to 5.9 GHz so the radio is in the right band ... */
    ESP_ERROR_CHECK(esp_wifi_set_channel(140, WIFI_SECOND_CHAN_NONE));
    /* ... then tune to the real centre frequency */
    phy_change_channel(channel_mhz, 1, 0, 0);
    ESP_LOGI(TAG, "802.11p Empfang aktiv, %u MHz", channel_mhz);
    return ESP_OK;
}
