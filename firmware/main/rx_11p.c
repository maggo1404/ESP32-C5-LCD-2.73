/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rx_11p.h"

#include <stdbool.h>
#include <stdio.h>
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
static volatile bool s_diag_on = RX_DIAG_DEFAULT;
static const uint8_t BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* ---- Diagnose: first bytes of received frames in the log ----
 * Frames the callback rejects (MISC, rx_state != 0, not broadcast) are copied
 * into a tiny ring (no malloc, short critical section) and printed by the
 * worker; accepted frames are printed by the worker together with the ITS
 * parse result. Rate-limited so a busy channel cannot flood the console. */
#define DIAG_BYTES      32
#define DIAG_RING       8
#define DIAG_MAX_PER_10S 20

typedef enum { DIAG_MISC = 0, DIAG_RXERR, DIAG_SHORT, DIAG_UNICAST } diag_reason_t;

typedef struct {
    uint8_t reason;
    uint8_t type;      /* wifi_promiscuous_pkt_type_t */
    uint8_t rx_state;
    int8_t rssi;
    uint16_t sig_len;
    uint8_t n;         /* bytes captured */
    uint8_t data[DIAG_BYTES];
} diag_item_t;

static diag_item_t s_diag[DIAG_RING];
static uint8_t s_diag_head, s_diag_count;
static uint32_t s_diag_lost;
static portMUX_TYPE s_diag_mux = portMUX_INITIALIZER_UNLOCKED;
static const char *const DIAG_REASON[] = {"MISC", "RX-Fehler", "zu kurz", "kein Broadcast"};

static void diag_capture(diag_reason_t reason, wifi_promiscuous_pkt_type_t type,
                         const wifi_promiscuous_pkt_t *pkt)
{
    portENTER_CRITICAL(&s_diag_mux);
    if (s_diag_count >= DIAG_RING) {
        s_diag_lost++;
        portEXIT_CRITICAL(&s_diag_mux);
        return;
    }
    diag_item_t *d = &s_diag[(s_diag_head + s_diag_count) % DIAG_RING];
    s_diag_count++;
    d->reason = (uint8_t)reason;
    d->type = (uint8_t)type;
    d->rx_state = (uint8_t)pkt->rx_ctrl.rx_state;
    d->rssi = (int8_t)pkt->rx_ctrl.rssi;
    d->sig_len = (uint16_t)pkt->rx_ctrl.sig_len;
    /* Only copy bytes the driver really delivered: MISC packets have a
     * zero-length payload (esp_wifi_types_generic.h), and for errored frames
     * sig_len is just the PHY SIG field. On HE chips dump_len is the delivered
     * length (0 = nothing); without it, copy only from valid frames. */
    uint32_t n = 0;
    if (type != WIFI_PKT_MISC) {
#if CONFIG_SOC_WIFI_HE_SUPPORT
        n = pkt->rx_ctrl.sig_len;
        if (pkt->rx_ctrl.dump_len < n) {
            n = pkt->rx_ctrl.dump_len;
        }
#else
        n = pkt->rx_ctrl.rx_state == 0 ? pkt->rx_ctrl.sig_len : 0;
#endif
    }
    if (n > DIAG_BYTES) n = DIAG_BYTES;
    d->n = (uint8_t)n;
    memcpy(d->data, pkt->payload, n);
    portEXIT_CRITICAL(&s_diag_mux);
}

static int s_diag_budget = DIAG_MAX_PER_10S;
static int64_t s_diag_window_us;
static uint32_t s_diag_suppressed;

static bool diag_allow(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_diag_window_us > 10000000) {
        if (s_diag_suppressed) {
            ESP_LOGI(TAG, "diag: %u weitere Frames nicht ausgegeben (Limit)", (unsigned)s_diag_suppressed);
        }
        s_diag_window_us = now;
        s_diag_budget = DIAG_MAX_PER_10S;
        s_diag_suppressed = 0;
    }
    if (s_diag_budget <= 0) {
        s_diag_suppressed++;
        return false;
    }
    s_diag_budget--;
    return true;
}

static void diag_hex(char *out, size_t out_sz, const uint8_t *p, unsigned n)
{
    size_t o = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < n && o + 4 < out_sz; i++) {
        o += (size_t)snprintf(out + o, out_sz - o, "%02x%s", p[i], (i % 4 == 3 && i + 1 < n) ? " " : "");
    }
}

static void diag_print_rejected(void)
{
    for (;;) {
        diag_item_t d;
        uint32_t lost;
        portENTER_CRITICAL(&s_diag_mux);
        if (s_diag_count == 0) {
            portEXIT_CRITICAL(&s_diag_mux);
            return;
        }
        d = s_diag[s_diag_head];
        s_diag_head = (uint8_t)((s_diag_head + 1) % DIAG_RING);
        s_diag_count--;
        lost = s_diag_lost;
        s_diag_lost = 0;
        portEXIT_CRITICAL(&s_diag_mux);
        if (lost) {
            ESP_LOGI(TAG, "diag: %u verworfene Frames ohne Platz im Puffer", (unsigned)lost);
        }
        if (!diag_allow()) continue;
        char hex[DIAG_BYTES * 3 + 1];
        diag_hex(hex, sizeof(hex), d.data, d.n);
        ESP_LOGI(TAG, "diag verworfen (%s) typ=%u rx_state=%u len=%u rssi=%d: %s",
                 d.reason < 4 ? DIAG_REASON[d.reason] : "?", d.type, d.rx_state, d.sig_len, d.rssi, hex);
    }
}

static void diag_print_accepted(const uint8_t *frame, uint16_t len, int8_t rssi)
{
    if (!diag_allow()) return;
    its_packet_t pkt;
    its_err_t err = its_parse_frame(frame, len, &pkt);
    char hex[DIAG_BYTES * 3 + 1];
    diag_hex(hex, sizeof(hex), frame, len < DIAG_BYTES ? len : DIAG_BYTES);
    if (err == ITS_OK && pkt.pdu) {
        ESP_LOGI(TAG, "diag ITS %s station=%u btp=%u%s len=%u rssi=%d: %s",
                 its_msg_name(pkt.message_id), (unsigned)pkt.station_id, pkt.btp_dst_port,
                 pkt.secured ? " signiert" : "", len, rssi, hex);
    } else {
        ESP_LOGI(TAG, "diag Broadcast, kein ITS (parse=%d) len=%u rssi=%d: %s", (int)err, len, rssi, hex);
    }
}

/* ---- Wi-Fi promiscuous callback (runs in the Wi-Fi task, no malloc) ---- */
static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    s_stats.cb_total++;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (type == WIFI_PKT_MISC) {
        if (s_diag_on) diag_capture(DIAG_MISC, type, pkt);
        return;
    }
    if (pkt->rx_ctrl.rx_state != 0 || pkt->rx_ctrl.sig_len <= 4) {
        if (s_diag_on) diag_capture(pkt->rx_ctrl.rx_state != 0 ? DIAG_RXERR : DIAG_SHORT, type, pkt);
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
        if (s_diag_on) diag_capture(len < 10 ? DIAG_SHORT : DIAG_UNICAST, type, pkt);
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
            if (s_diag_on) diag_print_accepted(frame, it.len, it.rssi);

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
        if (s_diag_on) diag_print_rejected();
        uint16_t ch = __atomic_exchange_n(&s_pending_channel, 0, __ATOMIC_ACQ_REL);
        if (ch) {
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
    __atomic_store_n(&s_pending_channel, channel_mhz, __ATOMIC_RELEASE);
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

    /* Same bring-up as the OpenTrafficMap / simple-its receivers. Every step
     * is logged, and an error is returned instead of abort()ing (the abort
     * text would only show on UART0): the caller keeps the UI running. */
#define RX_STEP(call)                                                  \
    do {                                                               \
        esp_err_t e_ = (call);                                         \
        if (e_ != ESP_OK) {                                            \
            ESP_LOGE(TAG, "%s -> %s", #call, esp_err_to_name(e_));     \
            return e_;                                                 \
        }                                                              \
        ESP_LOGI(TAG, "ok: %s", #call);                                \
    } while (0)

    RX_STEP(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    RX_STEP(esp_wifi_init(&cfg));
    RX_STEP(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    RX_STEP(esp_wifi_set_mode(WIFI_MODE_NULL));

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL & ~WIFI_PROMIS_FILTER_MASK_FCSFAIL,
    };
    RX_STEP(esp_wifi_set_promiscuous_filter(&filter));
    RX_STEP(esp_wifi_set_promiscuous_rx_cb(rx_cb));
    RX_STEP(esp_wifi_set_promiscuous(true));
    ESP_LOGI(TAG, "phy_11p_set(1, 0)");
    phy_11p_set(1, 0);
    /* a channel close to 5.9 GHz so the radio is in the right band ... */
    RX_STEP(esp_wifi_set_channel(140, WIFI_SECOND_CHAN_NONE));
    /* ... then tune to the real centre frequency */
    ESP_LOGI(TAG, "phy_change_channel(%u)", channel_mhz);
    phy_change_channel(channel_mhz, 1, 0, 0);
    ESP_LOGI(TAG, "802.11p Empfang aktiv, %u MHz", channel_mhz);
#undef RX_STEP
    return ESP_OK;
}

void rx_set_diag(bool on) { s_diag_on = on; }
bool rx_get_diag(void) { return s_diag_on; }
