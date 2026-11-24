# Schrift

`Montserrat-Medium.ttf` stammt aus dem LVGL-Repository (`scripts/built_in_font/`),
Montserrat von Julieta Ulanovsky, Lizenz SIL Open Font License 1.1.
Die Datei wird per `EMBED_FILES` ins Image eingebettet und zur Laufzeit mit
LVGL TinyTTF in 14/20/28 px gerastert (enthält Latin-1, also Umlaute und ß).
Die eingebauten LVGL-Montserrat-Schriften dienen als Fallback für die
`LV_SYMBOL_*`-Symbole.
