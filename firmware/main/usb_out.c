/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usb_out.h"

#include "app_config.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "usb_out";

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

static bool s_installed;
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
    size_t n = usb_out_slip_encode(frame, len, s_enc);
    /* Short timeout: if the host stops reading we drop instead of stalling
     * the decoder. */
    int w = usb_serial_jtag_write_bytes(s_enc, n, pdMS_TO_TICKS(20));
    return w == (int)n;
}
