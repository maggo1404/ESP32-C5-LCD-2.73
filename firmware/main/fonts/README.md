# Schriften

`ui_font_montserrat_{12,14,20,28}.c` sind vorgerenderte LVGL-8-Bitmap-Schriften
(4 bpp, ASCII + Latin-1 inkl. Umlaute/ß/°/·, – „ “ ” … ≤ ≥, ohne Kerning), erzeugt
mit `./gen_fonts.sh` (`lv_font_conv@1.5.3` per npx) aus `Montserrat-Medium.ttf`.
Die Schrift stammt aus dem LVGL-Repository (`scripts/built_in_font/`), Montserrat von
Julieta Ulanovsky, Lizenz SIL Open Font License 1.1. Die TTF liegt nur noch fuer das
Skript im Repo und wird nicht mehr ins Image eingebettet. Zur Laufzeit wird nichts
gerastert (frueher TinyTTF, das den einzigen CPU-Kern blockierte).
`LV_SYMBOL_*` kommen ueber `.fallback` aus den eingebauten LVGL-Montserrat-Schriften.
