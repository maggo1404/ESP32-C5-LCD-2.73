/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Installs the usb_serial_jtag driver. The console/log must NOT use the
 * USB-Serial-JTAG port (see sdkconfig.defaults), otherwise the SLIP stream
 * would be mixed with log text. */
esp_err_t usb_out_init(void);

/* True if a USB host currently has the port open/enumerated. */
bool usb_out_connected(void);

/* SLIP-encode one raw 802.11 frame (END + escaped payload + END) and write it.
 * Returns true if the whole frame was queued, false if dropped. */
bool usb_out_send_frame(const uint8_t *frame, size_t len);

/* Pure helper (also used for tests): returns the encoded length, `out` must
 * hold at least 2*len + 2 bytes. */
size_t usb_out_slip_encode(const uint8_t *in, size_t len, uint8_t *out);
