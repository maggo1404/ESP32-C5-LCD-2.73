/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usb_out.h"

#include "app_config.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "usb_out";

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

static bool s_installed;
static int64_t s_backoff_until_us; /* only used from the worker task */
/* Only used from the worker task. */
static EXT_RAM_BSS_ATTR uint8_t s_enc[2 * RX_FRAME_MAX + 2];

esp_err_t usb_out_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 8192;
    cfg.rx_buffer_size = 256;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver install: %s", esp_err_to_name(err));
        return err;
    }
    s_installed = true;
    return ESP_OK;
}

bool usb_out_connected(void)
{
    return s_installed && usb_serial_jtag_is_connected();
}

size_t usb_out_slip_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t n = 0;
    out[n++] = SLIP_END;
    for (size_t i = 0; i < len; i++) {
        switch (in[i]) {
        case SLIP_END:
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_END;
            break;
        case SLIP_ESC:
            out[n++] = SLIP_ESC;
            out[n++] = SLIP_ESC_ESC;
            break;
        default:
            out[n++] = in[i];
        }
    }
    out[n++] = SLIP_END;
    return n;
}

bool usb_out_send_frame(const uint8_t *frame, size_t len)
{
    if (!usb_out_connected() || len == 0 || len > RX_FRAME_MAX) {
        return false;
    }
    int64_t now = esp_timer_get_time();
    if (now < s_backoff_until_us) {
        return false; /* host enumerated the port but does not read: stay out of the way */
    }
    size_t n = usb_out_slip_encode(frame, len, s_enc);
    /* Never wait: ingest must not be delayed by USB. If the TX buffer is
     * full (nobody reads), drop and back off for 1 s. */
    int w = usb_serial_jtag_write_bytes(s_enc, n, 0);
    if (w != (int)n) {
        s_backoff_until_us = now + 1000000;
        return false;
    }
    return true;
}
