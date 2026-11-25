#!/usr/bin/env bash
# Regenerates the pre-rendered LVGL 8 bitmap fonts from Montserrat-Medium.ttf
# (SIL OFL 1.1, see README.md). Needs node/npm; lv_font_conv is pinned.
# LV_SYMBOL_* glyphs are NOT included: ui_fonts.c chains the built-in LVGL
# Montserrat fonts (which contain FontAwesome) via .fallback.
set -euo pipefail
cd "$(dirname "$0")"
LV_FONT_CONV="lv_font_conv@1.5.3"
# ASCII, Latin-1 supplement, en dash, quotes, ellipsis, bullet, <= >=, middle dot is in Latin-1
RANGE="0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x201E,0x2022,0x2026,0x2264,0x2265"
for sz in 12 14 20 28; do
  fb=$sz; [ "$sz" = 12 ] && fb=14   # no built-in 12 px font enabled
  npx --yes "$LV_FONT_CONV" --font Montserrat-Medium.ttf --size "$sz" --bpp 4 \
    --range "$RANGE" --no-kerning --format lvgl --lv-include lvgl.h \
    --lv-font-name "ui_font_montserrat_${sz}" -o "ui_font_montserrat_${sz}.c" --force-fast-kern-format --no-compress
  # chain the built-in font (LV_SYMBOL_*) as fallback; the struct is const, so patch the source
  sed -i "s/\.fallback = NULL,/.fallback = \&lv_font_montserrat_${fb},/" "ui_font_montserrat_${sz}.c"
done
