# Gehäuse für Waveshare ESP32-C5-LCD-2.73 (parametrisch, CadQuery)

Handgerät mit **herausnehmbarem LiPo 503450** und **Lüftungsgitter-Clip** fürs Auto.
Alles wird aus `enclosure.py` erzeugt:

```bash
python -m venv venv && venv/bin/pip install cadquery trimesh numpy matplotlib
venv/bin/python enclosure.py        # schreibt nach enclosure/export/
```

Das Skript prüft auch: jedes Teil ist ein gültiger Einzelkörper, keine Durchdringung in der Baugruppe
(inkl. `board_dummy`), Teile liegen ohne Überlappung auf dem 220x220-Bett (`export/check_report.txt`).

## 3D-Viewer (Web-Seite)

Die Seite (`web/`) zeigt das Gehäuse per Button **„3D ansehen“** in einer Lightbox (three.js r170, lokal unter
`web/vendor/three/`, wird erst beim Klick geladen). Die Modelle liegen als GLB in `export/glb/`
(`assembly`, `body`, `front_bezel`, `battery_door`, `vent_clip`, `assembly_exploded`) plus Vorschaubilder in `export/glb/posters/`;
`pages.yml` kopiert beides nach `gehaeuse/3d/`.

```bash
venv/bin/pip install cascadio==0.0.17 trimesh numpy
venv/bin/python step_to_glb.py      # STEP -> GLB (Toleranz 0,05 mm / 0,3 rad); läuft auch am Ende von enclosure.py, wenn cascadio installiert ist
node cad_posters.mjs                # Poster-PNGs (npm i playwright; npx playwright install chromium)
```

Farben setzt `step_to_glb.py` je Teil (Tabelle `COLORS`; `board_dummy` = dunkelgrüne Platine/Display, sichtbar nur in der Baugruppe).
Die Explosionsansicht entsteht dort durch Z-Versatz der Baugruppen-Knoten (`EXPLODE`).
Hinweis: Die Vorlage-Pipeline nutzt puppeteer-core für die Poster; hier wird stattdessen Playwright verwendet (vorinstalliert).
Nach Änderungen am Modell: `python enclosure.py`, dann ggf. `node cad_posters.mjs`, und die GLBs/Poster mit einchecken.

## Teile

| Teil | Datei | Druckausrichtung |
|---|---|---|
| Frontrahmen | `front_bezel` | Vorderseite nach unten (Fensterfase überhängt nur 45°) |
| Gehäusekörper | `body` | Rückseite nach unten, Bauteilraum offen nach oben |
| Akkudeckel (Schieber) | `battery_door` | Außenseite (mit Fingermulde) nach oben |
| Vent-Clip | `vent_clip` | **auf der Seite** (Platte steht hochkant), Schichten laufen entlang der Zinken-Biegerichtung |
| `board_dummy` | nur `.step`/`.stl` | Referenzkörper (Display+Platine+Bauteile), nicht drucken |

`*.stl` sind in **Druckausrichtung**, `*.step` in **Einbaulage** (`assembly.step` = Baugruppe).
`print_plate.3mf` enthält alle vier Teile fertig angeordnet. `preview_*.png` sind Vorschauen.

**Koordinatenkonvention** (siehe Kopf von `enclosure.py`): Ursprung Platinenmitte, +X rechts, +Y oben
**bei Blick auf die Vorderseite**, +Z zum Betrachter. Die Maßzeichnung zeigt die *Rückseite* (links/rechts
gespiegelt): `X = 34 - bx`, `Y = 34 - by`. Daher liegt der TF-Slot (Rückansicht oben links, bx 10–20)
vorne gesehen **oben rechts** (Käfigmitte bx 18,7 → X = +15,3), der BAT-Stecker (gemessen) bei X = -22,6, Y = -17 (Steckrichtung +Y).

## Maße (Standardparameter)

- Außenmaß Körper/Rahmen **78,0 x 78,0 mm** (Platinentasche 68,6), Gesamtdicke **22,3 mm** (ohne Clip; Clip +18,2 mm nach hinten)
- Stapel von hinten: Lippe 1,0 + Deckelnut 1,6 + Akku 6,0 (=8,6), Trennboden 1,5, Bauteilraum 4,3 (Bauteile 4,0 + 0,3 Luft),
  Platine 1,6, LCD 4,0, 0,5 Luft (Dichtband), Frontrahmen 1,8
- Fenster 50,5 x 50,5 mm mit 1 mm 45°-Fase außen (aktive Fläche 48,96), Offset `WIN_OFF_X/Y` (Standard 0 = mittig; aus der
  Zeichnung ca. mittig gemessen)
- Akkufach 52 x 36 x 6,0 mm (unten, **bewusst nicht über der Antenne**), Schiebedeckel 1,6 mm in Führungsnut
- Rückseite über der PCB-Antenne (X -24…-4, Y 25,5…34) ausgespart: dort nur 1,5 mm Boden, keine Rippen, kein Metall.
  Abschalten mit `ANT_RECESS = False`.

## Befestigung

Das LCD-Glas ist durchgehend, die M2-Löcher gibt es nur in der Platine dahinter. Deshalb:

1. **Platine** liegt von hinten auf 4 Standoffs (Ø5) und wird durch 4 kurze **Zapfen** (Ø2,0, Höhe = Platinendicke − 0,2 mm)
   in den Platinenlöchern zentriert; die Zapfen erreichen die LCD-Rückseite nie.
2. Der **Frontrahmen** liegt über einem **selbstklebenden Schaumstoff-Dichtband** (0,8–1,0 mm dick, ca. 2–3 mm breit, rundum auf die
   Glaskante geklebt; im Rahmen auf 0,5 mm komprimiert, `LCD_FRONT_GAP` = 0,5) und klemmt so Display+Platine nachgiebig auf die
   Standoffs. Dadurch gleicht das Band Dickenabweichungen (±0,2 mm) aus und belastet das Glas nicht starr über die Schrauben.
   Rahmen überdeckt das Glas bei mittigem Fenster ca. 8,75 mm breit.
3. Rahmen und Körper werden mit **4x M2-Senkkopfschrauben in den Ecken außerhalb der 68x68-Platine** verschraubt
   (Schraubenmitte ±35,8 mm, ≥2,5 mm Wand zur Platinentasche). Daher wuchs das Außenmaß auf 78 mm.
   Länge: Rahmen 1,8 + Bohrung 8,0 − 0,5 = 9,3 → **M2 x 8** (selbstschneidend in Ø1,8-Bohrung, PETG/ASA).
   Mit `USE_INSERTS = True`: M2-Gewindeeinsätze (Bohrung Ø3,2 x 5), Schraube dann **M2 x 6**.
   Zapfen mit 0,3 mm Einführfase; Senkung 1,0 mm tief (0,8 mm Restdicke unter dem Kopf). Das Skript gibt die Länge aus. Seitliche Ausrichtung des Rahmens erfolgt über die Schrauben (Loch Ø2,4).

## Öffnungen und Bedienelemente

- **USB-C** an der Unterkante, Mitte (`USB_X` = -0,7), Fenster 14 x 8 mm für den Stecker-Overmold.
- **TF-Karte** an der Oberkante bei X ≈ +15,3 (14 mm breit, 3,4 mm hoch) plus 18 mm breite **Fingermulde** außen
  (die Wand ist ca. 5 mm dick), damit eine ausgeworfene Karte greifbar ist.
- **Seitentaster** (beide Seiten, Y = -18,7 und -25,5 = 52,7/59,5 mm von oben in der Rückansicht): in die Wand sind gedruckte
  **Biegezungen** (1,2 mm dick, 10 mm lang) mit Stößel eingeschnitten (Stößel 2 mm vom freien Ende, Abstand zur Platinenkante
  `BTN_NUB` = 0,25 mm); Zungen laufen auseinander. Tastenhöhe `BTN_Z_OFF` bitte nachmessen. Zu weich/hart: `BTN_TAB_T`, `BTN_TAB_L`.
- **Kabeldurchlass** im Trennboden (8 x 19 mm, Y -26,5…-7,5) über dem BAT-Stecker (X -22,6, Y -17). Der Stecker zieht nach +Y
  und ist durch den Schlitz erreichbar. **Reihenfolge:** Akkukabel durch den Schlitz fädeln und am BAT-Stecker einstecken, *bevor* die
  Platine eingesetzt wird bzw. solange der Schlitz noch zugänglich ist.
- Gewicht/Druckzeit: Rückseite hinter dem Display ist **hohl** (nur 1,5 mm Boden; stehen bleiben 3 mm Rand, Wand um das Akkufach,
  9-mm-Bänder um die Schwalbenschwanz-Nuten und 2-mm-Rippen im 15-mm-Raster, keine Freispannweite > 20 mm). Über der Antenne
  (X -24…-4, Y 25,5…34) gibt es keine Rippen und keinen Clip/Band; dort nur 1,5 mm Boden.
- Lüftungsschlitze: nicht vorgesehen (optional, nicht umgesetzt).

## Akku (wichtig)

- LiPo **503450** (ca. 50 x 34 x 5 mm inkl. Schutzschaltung), Fach 52 x 36 x 6,0. Akku von hinten einlegen, Kabel
  durch den Bodenschlitz zum **BAT-Stecker (JST 1.25, 2-polig)** führen und stecken.
- **Polarität prüfen!** Die Belegung des BAT-Steckers am Waveshare-Board **vor dem Anstecken** mit Multimeter/Schaltplan
  verifizieren – viele LiPos haben ein vertauschtes JST-Pinout. Verpolung zerstört Board und Akku.
- Deckel: nach unten herausschieben (Fingermulde in der Öffnung), werkzeuglos. Er läuft in einer Nut **hinter einer 1,0 mm
  dicken Rücklippe** (2 mm Überlappung je Seite) und ist so gegen Herausfallen nach hinten gesichert; der Akku liegt auf dem Deckel.
  Rastzungen im Deckel rasten in Mulden der Nut (geschlossen).
- **Opfer-Stege:** Die Decke des Akkufachs (52 x 36 mm) wäre ohne Stützen eine lange Brücke. Im **gedruckten** Körper
  (`body.stl`, `print_plate.3mf`) stehen deshalb zwei 0,8 mm dünne Stege in Y-Richtung im Fach. **Vor dem Einlegen des Akkus mit
  Zange/Messer ausbrechen.** `body.step` (Baugruppe) enthält sie nicht (`BAT_STRUTS = False` schaltet sie ab).
- **Sommer/Auto:** Im Fahrzeug werden leicht >60 °C erreicht. LiPo bei Hitze nicht im Auto lassen:
  **Akku im Sommer entnehmen** bzw. Gerät nicht in der Sonne liegen lassen. Gehäuse aus **PETG oder besser ASA**
  drucken (PLA verformt sich ab ~55 °C).

## Vent-Clip

Separates Teil, per **einseitigem Schwalbenschwanz** (45°, druckbar ohne Stützen) auf die Rückseite geschoben.
Zwei Nuten kreuzen sich im Clip-Mittelpunkt (`CLIP_CX/CLIP_CY` = +6 / 16, rechts neben der Antenne, über dem Akkufach frei):
von oben einschieben = Hochformat, von der **linken Seite** = Querformat (die Nut läuft bis zur Außenkante); am Anschlag sitzt der Clip mittig.
Eine **Rastnase** (Ø1,4) am Clip rastet in einer Mulde der Nut und hält ihn auch gegen das Gerätegewicht (Hochformat) in Position.
Zwei federnde Zinken (15 mm Tiefe) klemmen eine Lamelle; Ruhespalt = `SLAT_T` − 0,4 mm, Standard Lamelle 2,0 mm
(Bereich 1,5–3 mm, `SLAT_T` anpassen). Zähne an den Spitzen verhaken die Lamellenkante.
Druck: Clip auf der Seite liegend, Platte und Zinken haben dieselbe Auflagefläche, Ridge-Spitze verrundet; 100 % Infill.

## Druckeinstellungen

- Material **PETG oder ASA**, Düse 0,4, **Schichthöhe 0,2 mm**, **3–4 Perimeter**, Infill 15–25 %, **keine Stützen**.
- Clip: 100 % Infill empfohlen, 4+ Perimeter; langsam drucken.
- Körper: Hohlraum-Decken (Zellen ≤ 20 mm), Akkufach-Decke (über die Opfer-Stege) und die Taster-Schlitze werden überbrückt –
  Brückeneinstellungen (langsam, viel Lüfter). **Keine Stützen nötig**; die Opfer-Stege vor dem Gebrauch ausbrechen.
- Maßhaltigkeit: Spiel 0,3 (Platine) / 0,2 (Schiebeteile) – nach Test ggf. `CLR`, `DT_CLR` ändern.

## Zusammenbau

Benötigt außerdem: 4x M2x8 Senkkopfschrauben und ca. 250 mm Schaumstoff-Dichtband (0,8–1,0 mm, selbstklebend).

1. Opfer-Stege im Akkufach ausbrechen. Akkukabel von hinten durch den Bodenschlitz (8 x 19 mm) fädeln (Polarität prüfen!) und
   den JST-Stecker **vor dem Einsetzen der Platine** am BAT-Stecker einstecken (er ist nur so gut erreichbar).
2. Platine+LCD von vorn in den Körper legen (USB-C unten, TF oben; Zapfen in die Platinenlöcher, sie liegt auf den 4 Standoffs).
3. Dichtband rundum auf die Glaskante kleben, Frontrahmen aufsetzen, 4x M2x8 Senkschrauben eindrehen (nicht überdrehen).
4. Akku einlegen, Deckel von unten unter die Rücklippe schieben bis er einrastet.
5. Clip bei Bedarf einschieben (hoch/quer).

## Bitte nachmessen / Parameter in `enclosure.py`

| Messung | Parameter |
|---|---|
| LCD-Modul-Dicke, Platinendicke, höchstes Bauteil hinten | `LCD_T` (4,0), `PCB_T` (1,6), `COMP_T` (4,0) |
| USB-C-Höhe/Überstand, Mitte | `USB_H`, `USB_OVERHANG`, `USB_X` |
| Displayfenster-Versatz | `WIN_OFF_X`, `WIN_OFF_Y` |
| Taster-Höhe über Platine | `BTN_Z_OFF` |
| Akkugröße | `BAT_L`, `BAT_W`, `BAT_T` |
| Lamellendicke Lüftung | `SLAT_T` |
| Schraubenart | `USE_INSERTS` |
| Außenmaß / Schraubenlage | `OUTER`, `SCREW_C` |
