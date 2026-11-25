/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "esp_log.h"
#include "lvgl.h"
#include "ui_common.h"

extern const uint8_t ttf_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t ttf_end[] asm("_binary_Montserrat_Medium_ttf_end");

const lv_font_t *ui_f12;
const lv_font_t *ui_f14;
const lv_font_t *ui_f20;
const lv_font_t *ui_f28;

/* TinyTTF "size" is the line height in pixels; the sizes below roughly match
 * Montserrat 12/14/20/28 in visual size. Glyph bitmaps are cached (LRU).
 *
 * The cache size of lv_tiny_ttf_create_data_ex() is a BYTE budget, not a glyph
 * count: lv_lru_create() sizes its hash table as cache_size / (px * px). A
 * value like 120 gave 120 / 196 = 0 slots, the allocation of the empty table
 * failed, lv_tiny_ttf_create_data_ex() returned NULL and every font silently
 * fell back to the built-in Montserrat (no umlauts). `glyphs` is therefore
 * converted to bytes here (one slot per glyph, ~px*px bytes each). */
static const lv_font_t *make(int px, size_t glyphs, const lv_font_t *fallback)
{
    const size_t cache_bytes = glyphs * (size_t)px * (size_t)px;
    ESP_LOGI("ui_fonts", "TinyTTF %d px, cache %u glyphs = %u bytes", px, (unsigned)glyphs, (unsigned)cache_bytes);
    lv_font_t *f = lv_tiny_ttf_create_data_ex(ttf_start, (size_t)(ttf_end - ttf_start), px, cache_bytes);
    if (!f) {
        ESP_LOGW("ui_fonts", "TinyTTF %d px fehlgeschlagen, nutze eingebaute Schrift", px);
        return fallback;
    }
    f->fallback = fallback; /* LV_SYMBOL_* glyphs come from the built-in font */
    return f;
}

void ui_fonts_init(void)
{
    ui_f12 = make(14, 120, &lv_font_montserrat_14);
    ui_f14 = make(17, 120, &lv_font_montserrat_14);
    ui_f20 = make(24, 80, &lv_font_montserrat_20);
    ui_f28 = make(33, 40, &lv_font_montserrat_28);
}
