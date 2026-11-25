# Gehäuse für Waveshare ESP32-C5-LCD-2.73 (parametrisch, CadQuery)

Handgerät mit **herausnehmbarem Akku** und **Lüftungsgitter-Clip** fürs Auto, in **zwei Varianten**:

| Variante | Akku | Laden | Dicke | Ausgabe |
|---|---|---|---|---|
| **LiPo** (`lipo_503450`, Standard) | LiPo 503450, flach | Onboard-Lader des Boards (ETA6098) | 22,3 mm | `export/lipo/` |
| **LiFePO4** (`lifepo4_14500`) | LiFePO4 14500 (AA-Format), hitzefest | **internes Lademodul (3,6 V)**, Onboard-Lader per Rework abgeschaltet | 31,7 mm | `export/lifepo4/` |

> **Achtung LiFePO4-Variante:** Sie funktioniert nur mit einem Eingriff am Board (Bauteil **L1** entfernen). **Niemals** eine
> LiFePO4-Zelle an ein unverändertes Board mit USB-Kabel anschließen – der Onboard-Lader würde sie auf 4,2 V laden
> (Überladung, Brandgefahr). Details und weitere Warnungen: [LiFePO4-Variante](#lifepo4-variante).

Alles wird aus `enclosure.py` erzeugt:

```bash
python -m venv venv && venv/bin/pip install cadquery trimesh numpy matplotlib
venv/bin/python enclosure.py                          # LiPo        -> enclosure/export/lipo/
venv/bin/python enclosure.py --battery lifepo4_14500  # LiFePO4     -> enclosure/export/lifepo4/
venv/bin/python enclosure.py --all                    # beide nacheinander (je ein Prozess, ca. 6 min)
# alternativ: ENC_BATTERY=lifepo4_14500 venv/bin/python enclosure.py
```

Das Skript prüft auch: jedes Teil ist ein gültiger Einzelkörper, keine Durchdringung in der Baugruppe
(inkl. `board_dummy`, bei LiFePO4 auch Zelle und Lademodul), Teile liegen ohne Überlappung auf dem 220x220-Bett,
Brückenspannweiten ≤ 20 mm, geschlossene 3MF-Netze und – je Variante – Funktions-Probes (`export/<variante>/check_report.txt`).
Alle Pfade unten beziehen sich auf `export/<variante>/` (früher flach in `export/`; die LiPo-Geometrie ist unverändert).

## 3D-Viewer (Web-Seite)

Die Seite (`web/`) zeigt das Gehäuse per Button **„3D ansehen“** in einer Lightbox (three.js r170, lokal unter
`web/vendor/three/`, wird erst beim Klick geladen) mit **Schalter LiPo / LiFePO4**. Die Modelle liegen als GLB in
`export/<variante>/glb/` (`assembly`, `body`, `front_bezel`, `battery_door`, `vent_clip`, `assembly_exploded`, LiFePO4 zusätzlich
`charger_lid`) plus Vorschaubilder in `export/<variante>/glb/posters/`; `pages.yml` kopiert beides nach `gehaeuse/3d/<variante>/`
und die Druckdateien (`3mf/step/stl`) mit Präfix (`lipo_body.stl`, `lifepo4_print_plate.3mf` …) nach `gehaeuse/` (Liste `files.json`).
`build.yml` hängt dieselben Dateien (ohne `*_dummy`) mit Präfix an das Release.

```bash
venv/bin/pip install cascadio==0.0.17 trimesh numpy
venv/bin/python step_to_glb.py [lipo|lifepo4|all]   # STEP -> GLB (Toleranz 0,05 mm / 0,3 rad); läuft auch am Ende von enclosure.py, wenn cascadio installiert ist
node cad_posters.mjs [lipo|lifepo4|all]             # Poster-PNGs (npm i playwright; npx playwright install chromium)
```

Farben setzt `step_to_glb.py` je Teil (Tabelle `COLORS`; `board_dummy` = dunkelgrüne Platine/Display, `cell_dummy` = Zelle, `charger_dummy` = Lademodul, sichtbar nur in der Baugruppe).
Die Explosionsansicht entsteht dort durch Z-Versatz der Baugruppen-Knoten (`EXPLODE`).
Hinweis: Die Vorlage-Pipeline nutzt puppeteer-core für die Poster; hier wird stattdessen Playwright verwendet (vorinstalliert).
Nach Änderungen am Modell: `python enclosure.py --all`, dann ggf. `node cad_posters.mjs`, und die GLBs/Poster mit einchecken.

## Teile

| Teil | Datei | Druckausrichtung |
|---|---|---|
| Frontrahmen | `front_bezel` | Vorderseite nach unten (Fensterfase überhängt nur 45°) |
| Gehäusekörper | `body` | Rückseite nach unten, Bauteilraum offen nach oben |
| Akkudeckel (Schieber) | `battery_door` | Außenseite (mit Fingermulde) nach oben |
| Vent-Clip | `vent_clip` | **auf der Seite** (Platte steht hochkant), Schichten laufen entlang der Zinken-Biegerichtung |
| Modul-/Kabeldeckel *(nur LiFePO4)* | `charger_lid` | Außenseite (bündig mit der Rückseite) nach unten, Führungsrippen zeigen nach oben |
| `board_dummy` | nur `.step`/`.stl` | Referenzkörper (Display+Platine+Bauteile), nicht drucken |
| `cell_dummy`, `charger_dummy` *(nur LiFePO4)* | nur `.step` (in `assembly.step`) | Referenzkörper Zelle 14500 und Lademodul, nicht drucken |

`*.stl` sind in **Druckausrichtung**, `*.step` in **Einbaulage** (`assembly.step` = Baugruppe).
`print_plate.3mf` enthält alle druckbaren Teile (LiPo 4, LiFePO4 5) fertig angeordnet. `preview_*.png` sind Vorschauen.

**Koordinatenkonvention** (siehe Kopf von `enclosure.py`): Ursprung Platinenmitte, +X rechts, +Y oben
**bei Blick auf die Vorderseite**, +Z zum Betrachter. Die Maßzeichnung zeigt die *Rückseite* (links/rechts
gespiegelt): `X = 34 - bx`, `Y = 34 - by`. Daher liegt der TF-Slot (Rückansicht oben links, bx 10–20)
vorne gesehen **oben rechts** (Käfigmitte bx 18,7 → X = +15,3), der BAT-Stecker (gemessen) bei X = -22,6, Y = -17 (Steckrichtung +Y).

## Maße (Standardparameter)

- Außenmaß Körper/Rahmen **78,0 x 78,0 mm** (Platinentasche 68,6), Gesamtdicke **LiPo 22,3 mm / LiFePO4 31,7 mm** (ohne Clip;
  Clip +18,2 mm bzw. +18,7 mm nach hinten)
- Stapel von hinten (LiPo; LiFePO4 hat statt „Akku 6,0“ **15,4** = Ø14,5 + 0,9 Schaumstoff/Spiel, also Fachhöhe 18,0): Lippe 1,0 + Deckelnut 1,6 + Akku 6,0 (=8,6), Trennboden 1,5, Bauteilraum 4,3 (Bauteile 4,0 + 0,3 Luft),
  Platine 1,6, LCD 4,0, 0,5 Luft (Dichtband), Frontrahmen 1,8
- Fenster 50,5 x 50,5 mm mit 1 mm 45°-Fase außen (aktive Fläche 48,96), Offset `WIN_OFF_X/Y` (Standard 0 = mittig; aus der
  Zeichnung ca. mittig gemessen)
- Akkufach LiPo 52 x 36 x 6,0 mm (LiFePO4: 54 x 16,4 x 18,0 mm, siehe unten) (unten, **bewusst nicht über der Antenne**), Schiebedeckel 1,6 mm in Führungsnut
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

## Akku (wichtig) – LiPo-Variante

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

## LiFePO4-Variante

LiFePO4 (3,2 V nominal, max. 3,65 V) verträgt Hitze im Auto deutlich besser als LiPo. Das Board kann so eine Zelle im
Akkubetrieb direkt versorgen (Q1/Q2 schalten bei 3,0 V durch, der 3,3-V-Buck läuft dann im Dropout), **der Onboard-Lader
(U2 ETA6098) darf sie aber nicht laden**. Darum gilt für diese Variante:

> ## ⚠ PFLICHT: Onboard-Lader abschalten (L1 entfernen) – sonst Brandgefahr
>
> * **Niemals** eine LiFePO4-Zelle an ein **unverändertes** Board anschließen, solange USB steckt oder irgendwann gesteckt werden kann.
>   Der ETA6098 lädt eine 1S-Zelle auf (laut Annahme, Datenblatt nicht geprüft) **4,2 V**. Das ist für LiFePO4 (max. 3,65 V) eine
>   **Überladung**: Zersetzung, Aufblähen, Brand. Auf dem Board gibt es keinen Zellenschutz.
> * Vor dem ersten Einsetzen der Zelle: **L1** (2,2 µH, Bauform 2520, Speicherdrossel am SW-Pin von U2 im Schaltplanblock
>   *CHARGE*; nicht mit L2 am MP1605 verwechseln!) **auslöten**. Ohne L1 kann der Buck-Lader keinen Ladestrom liefern.
> * **Prüfen vor dem ersten Anschluss** (USB gesteckt, **keine** Zelle): Multimeter zwischen **J1 Pin 1 (BAT+)** und **echter Board-Masse**
>   (USB-C-Schirm oder GND-Pad – **nicht** J1 Pin 2: der Minuspol hängt hinter Q1 und „schwebt“ ohne Zelle). Soll: ≈ 0 V; danach noch
>   einmal mit ca. **1 kΩ** Last zwischen J1 Pin 1 und GND: weiterhin < 0,5 V. Zeigt das Meter nennenswerte Spannung (> 1 V),
>   ist das Board **nicht** sicher – Zelle nicht anschließen.
> * **Erster Anschluss mit mA-Meter in Reihe** mit der Zelle (Modul noch abgeklemmt, USB gesteckt): es müssen **≈ 0 mA** vom Board
>   in die Zelle fließen. Jeder messbare Ladestrom heißt: L1 nicht wirksam entfernt oder anderer Pfad – sofort trennen.
> * Das Lademodul im Gehäuse lädt die Zelle mit **3,6 V** (Konstantspannung) – siehe unten, **vor dem Anschließen nachmessen**.
> * **Ladestrom des Moduls auf ≤ 300 mA (≈ 0,5 C bei 600 mAh) begrenzen** – Pflicht, nicht optional (siehe Prüfschritte unten).
> * **Das Modul lädt immer, sobald USB (5 V) anliegt – es gibt keinen NTC/Temperaturschutz.** LiFePO4 darf **nicht unter 0 °C und nicht über
>   45 °C** geladen werden. Im Auto im Sommer (Sonne, > 45 °C) oder bei Frost daher **den Ladeschalter ausschalten bzw. kein USB anstecken**.
>   Dafür ist ein **Schiebeschalter in der IN+-Leitung des Moduls** vorgesehen (`CHARGE_SWITCH = True`, Fenster 8,5 × 3,5 mm in der
>   linken Seitenwand, z. B. SS12D00); Alternative (b): Modul mit **NTC-/TS-Pin** verwenden und einen NTC an der Zelle anbringen.
> * Nicht ohne Aufsicht laden.
> * Nur Zellen **mit Schutzschaltung (PCM)** für LiFePO4 verwenden (Überladeschutz ca. 3,8–4,0 V, Unterspannung ca. 2,0–2,5 V).
>   Eine LiPo-PCM (4,25 V) schützt eine LiFePO4-Zelle **nicht**.

### Teileliste (zusätzlich zu M2x8-Schrauben und Dichtband)

| Teil | Hinweis |
|---|---|
| **LiFePO4-Zelle 14500 (AA-Format) mit PCM**, ca. 600 mAh | Ø14,0–14,2 × 49,7–50,5 mm blank, mit PCM/Knopfpol bis ca. 53 mm. Das Fach nimmt bis **Ø14,5 × 53,5 mm** auf. |
| **AA-Haltekontakte** (Plus: Blech, Minus: Blech + Feder) oder Einzelkontakte | Blech ca. 10 × 10 × 0,5 mm in den Taschen, Feder ca. 6 mm frei (Block 2 mm), Ø ≤ 9 mm. Aus einem ausgeschlachteten AA-Halter. |
| **Lademodul** TP5000 *(auf LiFePO4 / 3,6 V gestellt)* oder CN3058E-Modul | Gehäuse ist für ca. **26 × 17 × 4 mm** ausgelegt (**Annahme, nachmessen**, Parameter `CHARGER_L/W/H`). Eingang 5 V (Klemme/Pads), Ausgang BAT+/BAT−, Lade-LED. |
| **Mini-Schiebeschalter** (z. B. SS12D00 mit **langem Hebel ≥ 6 mm**, Gehäuse ca. 8,5 × 3,5 mm – die Wand ist 4,2 mm dick, ein kurzer Hebel erreicht die Außenseite nicht) | in der IN+-Leitung des Moduls = **Lade-Aus-Schalter**; Fenster in der linken Seitenwand (`CHARGE_SWITCH`, `SW_*`). Optional, aber empfohlen. |
| 2× M2x6 Senkkopfschrauben | Deckel des Modul-/Kabelraums (selbstschneidend, Pilotloch Ø1,8) |
| Litze 0,25–0,35 mm², Schrumpfschlauch, **JST-1.25-Stecker** mit 2 Adern | Kabel vom Kabelraum zum BAT-Stecker **J1** am Board (Stecker passend zu J1 besorgen) |
| Schaumstoff 1 mm | zwischen Zelle und Akkudeckel (zusätzlich zum Dichtband) |

### Verkabelung (Schaltbild)

Die meisten TP5000-/TP4056-artigen Module haben **IN− und BAT− intern verbunden** (gemeinsame Masse, **nachmessen**: Durchgang zwischen
IN− und BAT− im stromlosen Zustand). Daraus folgt:

```
  Board                                   Lademodul (gemeinsame Masse IN−/BAT−, auf 3,6 V!)
                                          ┌───────────────┐
  USB_5V vor D1 ──[Schalter]──────────────┤ IN+      BAT+ ├──┬──────────────► Zelle (+)   (Blech bei +X)
  (Abgriff, Schaltplan geprueft)          │               │  └──────────────► J1 Pin 1 = BAT+
                                          │ IN−  ═  BAT− ├──┬──────────────► Zelle (−)   (Feder bei −X)
                                          └───────────────┘  └──────────────► J1 Pin 2 = Minus (Drain Q1)
  Board-GND: KEINE eigene Leitung zum Modul – der Rückweg läuft über Q1 (Pin 2 → Q1 → GND).
```

* **Empfohlen (Modul mit gemeinsamer Masse):** IN+ von **USB_5V vor D1** (über den Ladeschalter), **IN−/BAT− = Zellen-Minus = J1 Pin 2**.
  Der Eingangsstrom des Moduls (= Ladestrom) fließt über J1 Pin 2 durch **Q1** (AO3400, Rds-on ≈ 30–50 mΩ) nach Board-GND zurück.
  Dadurch bleibt Q1 im Pfad: bei **verpolter Zelle** sperrt Q1 (Gate an BAT+), das Modul hat keinen Rückweg vom USB → kein
  Ladestrom, und **das Board** ist geschützt. **Die Zelle selbst ist dann aber nicht geschützt:** sie liegt verpolt direkt am
  BAT+/BAT−-Ausgang des Moduls und wird über dessen interne Dioden kurzgeschlossen – nur die Schutzschaltung (PCM) der Zelle
  begrenzt das (siehe unten „Was nicht geschützt ist“). Deshalb nur Zellen **mit PCM** und auf die Polaritätsmarken achten. Der Spannungsabfall an Q1 (≈ 15 mV bei 300 mA) ist vernachlässigbar. **Nicht**
  zusätzlich IN− an Board-GND anlöten: das würde Q1 überbrücken (Verpolschutz weg, Rückstrom des Boards über die Modulmasse).
* Nur bei einem Modul mit **galvanisch getrenntem** IN−/BAT− (selten) gilt: IN− an Board-GND, BAT− an Zellen-Minus; dann ist Q1 für den Ladepfad
  ohne Wirkung. Im Zweifel messen (Durchgang) und die Variante oben wählen.
* **Was nicht geschützt ist:** Das Modul selbst hat keinen Verpolschutz am BAT-Ausgang (Zelle falsch herum an den Blechen → Modul/Zelle
  gefährdet; Polarität der Marken beachten). Eine PCM-ausgelöste (tiefentladene) Zelle liegt ohne Spannung an BAT+ → Q1 (Gate über R54) bleibt aus und
  das Modul hat dann **keinen** Rückweg – die Zelle lässt sich dann nicht über dieses Modul „aufwecken“ (ggf. kurz mit Labornetzteil auf > 2,5 V bringen). **Nicht verifiziert – nachmessen.**
* **Kein Rückspeisen:** Ohne USB darf der 5-V-Knoten nicht über das Modul (BAT+ → IN+, z. B. Body-Diode/Booster) aus der Zelle gespeist werden – sonst bleibt Q2
  (Gate am USB_5V-Netz) gesperrt und das Board bleibt aus. Test: siehe unten („USB abziehen“). Der Ladeschalter in IN+ trennt diesen Pfad zusätzlich.
* **IN+** an **USB_5V vor D1** (VBUS-Seite der Schottky D1; laut Schaltplan geprüft, die **Lötstelle am Board** (z. B. D1-Anode) mit Multimeter bestimmen),
  **BAT+/BAT−** parallel zur Zelle (Kontaktbleche) **und** zum J1-Kabel. Die Drähte laufen durch den Bodenschlitz.
* Sicherungsvorschlag: 0,5–1 A Polyfuse in die BAT+-Leitung der Zelle (nicht im Gehäuse vorgesehen).
* **Polarität von J1:** Laut Schaltplan Pin 1 = BAT+, Pin 2 = Minus (über Q1). Die **physische** Pinreihenfolge am Board und am
  Stecker unbedingt mit Multimeter prüfen – **nicht verifiziert**. Verpolung zerstört Board und Zelle.

### Lademodul vor dem Anschluss prüfen (3,6 V und ≤ 300 mA!)

1. Modul **ohne Zelle** mit 5 V versorgen (z. B. USB-Netzteil) und die Spannung zwischen **BAT+ und BAT−** messen.
2. Soll: **3,60 … 3,65 V**. Zeigt das Meter **4,2 V** (Werkseinstellung vieler TP5000-Module), ist das Modul auf Li-Ion gestellt:
   per Lötbrücke/Schalter/Widerstand laut Modul auf **3,6 V (LiFePO4)** umstellen und erneut messen. **Erst bei 3,6 V die Zelle anschließen.**
3. **Ladestrom auf ≤ 300 mA begrenzen (Pflicht):** Bei TP5000-/CN3058-Modulen wird der Strom über den Programmierwiderstand (Rprog/Rcs, oft als
   Lötbrücken/DIP-Pads für 0,5/1/2 A oder als Widerstand beschriftet) eingestellt – Wert **laut Datenblatt/Beschriftung des konkreten Moduls**
   wählen, nicht raten. Messen: **mA-Meter in Reihe** mit BAT+ zu einer entladenen, aber nicht tiefentladenen Zelle (oder einem 3,0–3,3-V-Netzteil
   mit Strombegrenzung als Ersatzlast); der gemessene Konstantstrom muss ≤ 300 mA sein. Sonst Programmierung ändern, erneut messen.
4. Erst jetzt Zelle an das Modul anschließen, die LED beobachten (Laden/Fertig), nach dem ersten Zyklus Zellspannung messen (≤ 3,65 V).
5. **USB-Abzieh-Test:** Zelle eingesetzt und Board läuft, USB abziehen: **USB_5V < 0,3 V** (Multimeter am USB_5V-Netz/IN+ des Moduls) und das Board
   bleibt an. Bleibt USB_5V höher (Rückspeisung durch das Modul), Q2 bleibt gesperrt → Modul nicht verwenden bzw. Ladeschalter/Diode in IN+ ergänzen.
6. Gleichzeitig prüfen: Board bei gestecktem USB und **abgeklemmtem Modul** (Test oben) lädt die Zelle nicht (≈ 0 mA).

### Konstruktion (was der Code erzeugt)

* **Zellfach:** 54 × 16,4 mm, Höhe 18,0 mm, Zelle **längs in X** am unteren Rand (Y −37 … −20,6), von hinten (Akkudeckel
  abgeschoben) einlegbar. Obere Fachkanten mit **45°-Fasen = Zellenwiege** (zentriert, 0,35 mm Spiel), Decke ohne Stützen druckbar
  (Brücke 16,4 mm; keine Opfer-Stege). Zelle ruht auf **1 mm Schaumstoff** auf dem Akkudeckel (Fuge 0,6 mm bei Ø14,5).
* **Kontakte:** **Plus (+X)** flache Blechtasche 10,4 × 10,4 × 0,7 mm bündig in der Stirnwand; **Minus (−X)** Federtasche Ø9,2 × 1,5 mm
  mit Blechtasche dahinter. Federlänge eingebaut 2,0 mm (Zelle 53,5) … 5,8 mm (Zelle 49,7) bei 6 mm freier Länge, Block 2 mm
  (Zellen am unteren Längenrand haben kaum Vorspannung; lieber PCM-Zellen ≥ 50 mm nehmen). **Polarität:** Plus liegt bei **+X** (rechts bei Blick auf die Vorderseite, also **links bei Blick auf die Rückseite**
  beim Einlegen), der Minuspol mit Feder bei **−X**. Das **„+“ und „−“ ist vertieft in die Fachdecke eingeprägt** (sichtbar beim
  Einlegen von hinten, je 7 mm von der Stirnwand). Mechanisch ließe sich die Zelle (wie in jedem AA-Halter) auch verkehrt einlegen – deshalb **immer** nach den Marken einlegen (Pluspol = Knopf zum flachen Blech bei +X, flacher Minuspol an die Feder bei −X).
* **Kabelrinne** (2 mm tief, Z 6…9) in der oberen Fachwand über die ganze Länge, mit Verbindung zu beiden Blechtaschen; von dort
  der **Durchlass** (X −26,6…−18,6, ab Z 4, Schwelle zur Rückseite) in den **Kabel-/Modulraum** und weiter durch den bekannten
  Bodenschlitz (8 × 19 mm) zum BAT-Stecker **J1** (X −22,6, Y −17) und zu den 5-V-Abgriffen auf dem Board.
* **Modul-/Kabelraum** (X −34,4…−4,2, Y −15,6…−0,7, von hinten offen, mit **Deckel** `charger_lid` geschlossen): massiver Rahmen
  statt Rippenraster. Das Lademodul **steht hochkant** (Platine in der XZ-Ebene) in einem 27,4 × 4,6 mm Schlitz, unten in einer
  Rille des Deckels, oben in einem Schlitz in der Fachdecke (Rest Boden 1,2 mm), an den Enden von U-förmigen Rippen am Deckel gehalten (Längenspiel 1,9 mm, ±0,95 je Ende). Warum
  hochkant: flach liegend hätte das Modul (26 × 17) nirgends neben dem BAT-Stecker-Schlitz, den Clip-Bändern und der Antennenzone Platz.
  Der Raum liegt ≥ 20 mm von der Antennenzone (X −24…−4, Y 25,5…34) entfernt; Rippen/Bänder um den Clip bleiben unverändert.
* **Ladeschalter-Fenster** (`CHARGE_SWITCH`): 8,5 × 3,5 mm durch die linke Seitenwand (Y −11,2, Z 9) in den Kabelraum; der Schalter (z. B. SS12D00) wird innen eingeklebt, sein Hebel ragt durch das Fenster. Er liegt weit (> 20 mm) von der Antennenzone.
* **Deckel** `charger_lid`: 1,8 mm Platte bündig im Falz, 2× M2x6 Senkkopf; **Lichtfenster** 8 × 2 mm in der Mitte unter dem Modul
  (Lade-LED sichtbar, wenn die LED an der Unterkante in Modulmitte sitzt – **Annahme**, `LED_X`/`LED_W` anpassen).
* **Dicke 31,7 mm** (statt 22,3): Fachhöhe 18,0 = Lippe 1,0 + Deckelnut 1,6 + Ø14,5-Zelle mit 0,9 mm Schaumstoff/Spiel + Boden. Sie ist die Untergrenze
  für Ø14,5; dünner geht nur mit kleinerer Zelle (z. B. Ø13,x).
* **Vent-Clip verstärkt:** Platte 3,5 mm (statt 3,0) und Zinken 2,8 mm (statt 2,4) → ca. 1,6-fache Klemmkraft; Überstand nach hinten
  18,7 mm. Rechnerisch (Annahmen: PETG E = 1200 MPa, μ = 0,4, 130 g) Sicherheit ca. 4,5 gegen Rutschen und ca. 2,7 gegen Kippen – ein
  Papierwert, im Auto testen. Wegen der größeren Hebelwirkung (Dicke 31,7 mm) nicht über schwere Lüftungslamellen hängen.

### Zusammenbau LiFePO4

1. **Board-Umbau:** L1 auslöten, Test ohne Zelle (siehe oben). Lademodul auf 3,6 V prüfen (siehe oben).
2. Kontakte in die Taschen (Blech Plus; Feder-Blech Minus) einlegen/verkleben, Drähte in die Kabelrinne legen und durch den Durchlass
   führen (Kabel von den Blechen zum Kabelraum).
3. Modul mit Drähten versehen (IN+ zum Board über den Schalter, BAT+ / BAT− zu Zelle und J1), **Verkabelung vor dem Einbau mit Multimeter prüfen**;
   Modul in die Rippen des Deckels setzen (**vor dem Drucken ein echtes Modul anpassen/Probekörper drucken – Maße und Lage von LED/Anschlüssen variieren**), Deckel mit dem Modul von hinten einschieben (Modul-Oberkante in den Deckenschlitz).
   Der Deckel noch **nicht** festschrauben, solange die Drähte noch nicht zum Board geführt sind.
4. Drähte durch den **Bodenschlitz** führen, **nur IN+** (über den Ladeschalter) an USB_5V vor D1 löten (IN− bleibt an der gemeinsamen Modulmasse = Zellen-Minus = J1 Pin 2, **nicht** an Board-GND, siehe Verkabelung), J1-Stecker am BAT-Stecker einstecken
   (Polarität!), dann Platine+LCD einsetzen (wie LiPo) und den Rahmen verschrauben.
5. Deckel mit 2× M2x6 festschrauben. Zelle (Plus rechts/**+X**, Minuspol an die Feder) einlegen, Schaumstoff auflegen, Akkudeckel schließen.

### Druck (LiFePO4)

Wie die LiPo-Variante (PETG/ASA, 0,2 mm, 3–4 Perimeter, keine Stützen): Körper Rückseite unten, Akkudeckel Außenseite nach oben,
**Modul-Deckel** mit den Rippen nach oben, Clip auf der Seite. `export/lifepo4/print_plate.3mf` enthält alle fünf Teile.
Opfer-Stege gibt es hier nicht (Fachdecke 16,4 mm Brücke mit 45°-Fasen). Fasen/Taschen vorher mit einer Zelle und einem Probekörper testen.

### Nicht verifizierte Annahmen (bitte prüfen!)

| Annahme | Wo geprüft werden muss |
|---|---|
| **Lötstelle des 5-V-Abgriffs (USB_5V vor D1; Netz laut Schaltplan geprüft)** | Multimeter am Board; der Gehäuse-Bodenschlitz (X −26,6…−18,6, Y −26,5…−7,5) ist nur der Kabelweg |
| **Ladeschlussspannung des ETA6098 = 4,2 V** (nicht per Widerstand ladbar auf 3,6 V) | ETA6098-Datenblatt; deshalb L1 entfernen |
| **Maße des Lademoduls** 26 × 17 × 4 mm, **Lage der Lade-LED** | echtes Modul messen; `CHARGER_L/W/H`, `LED_X` |
| **Pinbelegung J1** (Pin 1 BAT+) und Steckerpolarität | Multimeter / Schaltplan, bevor eine Zelle angeschlossen wird |
| **Federkontakt-Maße** (Blech 10 × 10 × 0,5, Feder ≈ 6 mm) | passenden AA-Halter nachmessen; `PLATE_*`, `SPRING_*` |
| **Zellmaße** Ø ≤ 14,5, L ≤ 53,5 (mit PCM) | Datenblatt/Messschieber der gekauften Zelle |
| Gemeinsame Masse IN−/BAT− des Moduls; Rückweg über Q1; Aufwecken tiefentladener Zellen | Durchgangsmessung am Modul, Tests oben |
| Kontaktblech-Halt (Lippe/Schlitz) nicht umgesetzt | Bleche mit Klebstoff/Heißkleber oder Zelle selbst gehalten |
| Entladung/Firmware (Spannungsanzeige, Abschaltschwelle ≈ 3,1 V) | siehe `firmware/` (separat) |

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
| Akkugröße (LiPo) | `BATTERIES["lipo_503450"]` (`BAT_L`, `BAT_W`, `BAT_T`) |
| Zelle 14500 / Kontaktbleche / Feder | `CELL_D_MAX`, `CELL_L_MAX`, `PLATE_W`, `PLATE_T`, `SPRING_FREE` (LiFePO4) |
| Lademodul-Maße, Lage der LED | `CHARGER_L`, `CHARGER_W`, `CHARGER_H`, `LED_X` (LiFePO4) |
| Lamellendicke Lüftung | `SLAT_T` |
| Schraubenart | `USE_INSERTS` |
| Außenmaß / Schraubenlage | `OUTER`, `SCREW_C` |
