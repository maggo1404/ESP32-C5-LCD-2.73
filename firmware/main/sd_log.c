/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sd_log.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "settings.h"

static const char *TAG = "sd_log";

#define MSGBUF_SIZE    (192 * 1024)   /* in PSRAM, absorbs SD write latency */
#define BATCH_SIZE     (8 * 1024)
#define PCAP_HDR_LEN   16
#define RETRY_MS       4000
#define SYNC_MS        2000
#define BATCH_MAX_MS   500

static MessageBufferHandle_t s_mb;
static volatile bool s_active;
static volatile bool s_halt; /* sd_log_shutdown(): behave like "disabled in settings", for good */
static sdmmc_card_t *s_card;
static FILE *s_file;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sd_log_status_t s_status;
static volatile uint32_t s_dropped;

static void status_update(sd_log_state_t st, bool err, const char *file, uint32_t bytes, uint32_t card_mb)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = st;
    s_status.error = err;
    s_status.bytes = bytes;
    s_status.card_mb = card_mb;
    if (file) {
        strlcpy(s_status.file, file, sizeof(s_status.file));
    } else {
        s_status.file[0] = 0;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void status_bytes(uint32_t bytes)
{
    portENTER_CRITICAL(&s_lock);
    s_status.bytes = bytes;
    portEXIT_CRITICAL(&s_lock);
}

void sd_log_get_status(sd_log_status_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
    out->dropped = s_dropped;
}

void sd_log_reset_dropped(void) { s_dropped = 0; }

bool sd_log_active(void) { return s_active; }

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

void sd_log_submit(int64_t ts_us, const uint8_t *frame, size_t len)
{
    if (!s_active || !s_mb || len == 0 || len > RX_FRAME_MAX) {
        return;
    }
    uint8_t buf[PCAP_HDR_LEN + RX_FRAME_MAX];
    put_le32(buf + 0, (uint32_t)(ts_us / 1000000));
    put_le32(buf + 4, (uint32_t)(ts_us % 1000000));
    put_le32(buf + 8, (uint32_t)len);
    put_le32(buf + 12, (uint32_t)len);
    memcpy(buf + PCAP_HDR_LEN, frame, len);
    /* A message buffer message is atomic: either the whole record fits or
     * nothing is written. Never block the decoder. */
    if (xMessageBufferSend(s_mb, buf, PCAP_HDR_LEN + len, 0) == 0) {
        s_dropped++;
    }
}

/* ------------------------------------------------------------------ */

static void card_unmount(void)
{
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
}

static esp_err_t card_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = BOARD_SPI_HOST;
    const esp_vfs_fat_mount_config_t mnt = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    return esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mnt, &s_card);
}

/* Next free its_NNNN.pcap */
static int next_file_index(void)
{
    int max = 0;
    DIR *d = opendir(SD_MOUNT_POINT);
    if (!d) {
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int n;
        char tail[8];
        if (sscanf(e->d_name, "its_%d.%7s", &n, tail) == 2 && strcasecmp(tail, "pcap") == 0 && n > max) {
            max = n;
        }
    }
    closedir(d);
    return max + 1;
}

static bool file_open(char *name_out, size_t name_len)
{
    int idx = next_file_index();
    snprintf(name_out, name_len, "its_%04d.pcap", idx);
    char path[48];
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name_out);
    s_file = fopen(path, "wb");
    if (!s_file) {
        ESP_LOGW(TAG, "fopen %s: errno %d", path, errno);
        return false;
    }
    /* pcap global header: magic a1b2c3d4 (microsecond timestamps), v2.4,
     * snaplen 65535, linktype 105 (IEEE 802.11, no radiotap) */
    uint8_t h[24] = {0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00};
    put_le32(h + 16, 65535);
    put_le32(h + 20, 105);
    if (fwrite(h, 1, sizeof(h), s_file) != sizeof(h) || fflush(s_file) != 0) {
        fclose(s_file);
        s_file = NULL;
        return false;
    }
    ESP_LOGI(TAG, "Aufzeichnung: %s", path);
    return true;
}

static void drain_discard(void)
{
    static uint8_t tmp[PCAP_HDR_LEN + RX_FRAME_MAX];
    while (xMessageBufferReceive(s_mb, tmp, sizeof(tmp), 0) > 0) {
    }
}

static void sd_task(void *arg)
{
    (void)arg;
    /* internal DMA-capable RAM: the SD driver sends from this buffer */
    uint8_t *batch = heap_caps_malloc(BATCH_SIZE + PCAP_HDR_LEN + RX_FRAME_MAX, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!batch) {
        batch = heap_caps_malloc(BATCH_SIZE + PCAP_HDR_LEN + RX_FRAME_MAX, MALLOC_CAP_8BIT);
    }
    uint8_t *rec = malloc(PCAP_HDR_LEN + RX_FRAME_MAX);
    if (!batch || !rec) {
        ESP_LOGE(TAG, "kein Speicher");
        vTaskDelete(NULL);
        return;
    }

    size_t batch_len = 0;
    uint32_t file_bytes = 0;
    int64_t last_try = -RETRY_MS * 1000LL;
    int64_t last_sync = 0, batch_start = 0;
    char name[24] = "";

    for (;;) {
        bool want = settings_get().sd_log && !s_halt;

        if (!s_active) {
            /* OFF or NO_CARD */
            drain_discard();
            if (!want) {
                if (s_card) {
                    card_unmount();
                }
                status_update(SD_LOG_OFF, false, NULL, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            int64_t now = esp_timer_get_time();
            if (now - last_try >= RETRY_MS * 1000LL) {
                last_try = now;
                if (card_mount() == ESP_OK && file_open(name, sizeof(name))) {
                    uint32_t mb = (uint32_t)(((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
                    batch_len = 0;
                    file_bytes = 24;
                    last_sync = batch_start = now;
                    status_update(SD_LOG_RECORDING, false, name, file_bytes, mb);
                    s_active = true;
                    continue;
                }
                card_unmount();
                status_update(SD_LOG_NO_CARD, false, NULL, 0, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* RECORDING */
        bool fail = false;
        if (!want) {
            /* write out what we have, then close cleanly */
            if (batch_len) {
                fwrite(batch, 1, batch_len, s_file);
                batch_len = 0;
            }
            s_active = false;
            fflush(s_file);
            fsync(fileno(s_file));
            card_unmount();
            status_update(SD_LOG_OFF, false, NULL, 0, 0);
            continue;
        }

        size_t n = xMessageBufferReceive(s_mb, rec, PCAP_HDR_LEN + RX_FRAME_MAX, pdMS_TO_TICKS(100));
        if (n > 0 && batch_len + n <= BATCH_SIZE + PCAP_HDR_LEN + RX_FRAME_MAX) {
            if (batch_len == 0) {
                batch_start = esp_timer_get_time();
            }
            memcpy(batch + batch_len, rec, n);
            batch_len += n;
        }
        int64_t now = esp_timer_get_time();
        if (batch_len >= BATCH_SIZE || (batch_len && now - batch_start >= BATCH_MAX_MS * 1000LL)) {
            if (fwrite(batch, 1, batch_len, s_file) != batch_len) {
                fail = true;
            } else {
                file_bytes += batch_len;
                status_bytes(file_bytes);
            }
            batch_len = 0;
        }
        if (!fail && now - last_sync >= SYNC_MS * 1000LL) {
            last_sync = now;
            if (fflush(s_file) != 0 || fsync(fileno(s_file)) != 0) {
                fail = true;
            }
        }
        if (fail) {
            ESP_LOGW(TAG, "Schreibfehler (Karte entfernt/voll?), schliesse Datei");
            s_active = false;
            batch_len = 0;
            card_unmount();
            drain_discard();
            last_try = esp_timer_get_time();
            status_update(SD_LOG_NO_CARD, true, NULL, 0, 0);
        }
    }
}

void sd_log_shutdown(void)
{
    s_halt = true;
    for (int i = 0; i < 30 && (s_active || s_card); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

esp_err_t sd_log_start(void)
{
    s_mb = xMessageBufferCreateWithCaps(MSGBUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mb) {
        s_mb = xMessageBufferCreate(32 * 1024);
    }
    if (!s_mb) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(sd_task, "sd_log", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
