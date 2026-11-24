# Gehäuse für Waveshare ESP32-C5-LCD-2.73 (parametrisch, CadQuery)

Handgerät mit **herausnehmbarem LiPo 503450** und **Lüftungsgitter-Clip** fürs Auto.
Alles wird aus `enclosure.py` erzeugt:

```bash
python -m venv venv && venv/bin/pip install cadquery trimesh numpy matplotlib
venv/bin/python enclosure.py        # schreibt nach enclosure/export/
```

Das Skript prüft auch: jedes Teil ist ein gültiger Einzelkörper, keine Durchdringung in der Baugruppe
(inkl. `board_dummy`), Teile liegen ohne Überlappung auf dem 220x220-Bett (`export/check_report.txt`).

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
vorne gesehen **oben rechts** (X ≈ +19), der BAT-Stecker (bx 57, by 53) bei X = -23, Y = -19.

## Maße (Standardparameter)

- Außenmaß Körper/Rahmen **78,0 x 78,0 mm** (Platinentasche 68,6), Gesamtdicke **21,2 mm** (ohne Clip; Clip +18,2 mm nach hinten)
- Stapel von hinten: Akkufach/Deckel 0–7,8, Trennboden 1,5, Bauteilraum 4,4 (Bauteile 4,0 + 0,4 Luft),
  Platine 1,6, LCD 4,0, 0,3 komprimierbare Luft, Frontrahmen 1,6
- Fenster 50,5 x 50,5 mm mit 1 mm 45°-Fase außen (aktive Fläche 48,96), Offset `WIN_OFF_X/Y` (Standard 0 = mittig; aus der
  Zeichnung ca. mittig gemessen)
- Akkufach 52 x 36 x 6,0 mm (unten, **bewusst nicht über der Antenne**), Schiebedeckel 1,6 mm in Führungsnut
- Rückseite über der PCB-Antenne (X -24…-9,5, Y 20,5…34) ausgespart: dort nur 1,5 mm Boden, keine Rippen, kein Metall.
  Abschalten mit `ANT_RECESS = False`.

## Befestigung

Das LCD-Glas ist durchgehend, die M2-Löcher gibt es nur in der Platine dahinter. Deshalb:

1. **Platine** liegt von hinten auf 4 Standoffs (Ø5) und wird durch 4 kurze **Zapfen** (Ø2,0, Höhe = Platinendicke − 0,2 mm)
   in den Platinenlöchern zentriert; die Zapfen erreichen die LCD-Rückseite nie.
2. Der **Frontrahmen** liegt mit seinem Rand (8,75 mm breit) auf dem Glas und klemmt Display+Platine auf die Standoffs.
   Dazwischen `LCD_FRONT_GAP` = 0,3 mm komprimierbares Spiel (bei Wackeln dünnes Schaumstoff-/Klebeband auf die Glaskante).
3. Rahmen und Körper werden mit **4x M2-Senkkopfschrauben in den Ecken außerhalb der 68x68-Platine** verschraubt
   (Schraubenmitte ±35,8 mm, ≥2,5 mm Wand zur Platinentasche). Daher wuchs das Außenmaß auf 78 mm.
   Länge: Rahmen 1,6 + Bohrung 8,0 − 0,5 = 9,1 → **M2 x 8** (selbstschneidend in Ø1,8-Bohrung, PETG/ASA).
   Mit `USE_INSERTS = True`: M2-Gewindeeinsätze (Bohrung Ø3,2 x 5), Schraube dann **M2 x 6**.
   Das Skript gibt die Länge aus. Seitliche Ausrichtung des Rahmens erfolgt über die Schrauben (Loch Ø2,4).

## Öffnungen und Bedienelemente

- **USB-C** an der Unterkante, Mitte (`USB_X`), Fenster 12 x 7 mm für Stecker-Overmold.
- **TF-Karte** an der Oberkante (X ≈ +19, 12 mm breit, 3,4 mm hoch).
- **Seitentaster** (beide Seiten, Y = -18,7 und -27,0 = 52,7/61 mm von oben in der Rückansicht): in die Wand sind
  gedruckte **Biegezungen** (1,2 mm dick, 9 mm lang) mit Stößel eingeschnitten; Zungen laufen auseinander,
  damit beide Taster nebeneinander passen. Stößel sitzt `BTN_NUB` = 0,05 mm vor der Platinenkante – Tastenhöhe
  `BTN_Z_OFF` und Position bitte nachmessen. Zu weiche/harte Zunge: `BTN_TAB_T`, `BTN_TAB_L`.
- **Kabeldurchlass** im Trennboden (8 x 9 mm) direkt über dem BAT-Stecker (X -23, Y -19).
- Gewicht/Druckzeit: Rückseite hinter dem Display ist **hohl** (nur 1,5 mm Boden; stehen bleiben 3 mm Rand, Wand um das Akkufach,
  9-mm-Bänder um die Schwalbenschwanz-Nuten und eine Rippe bei X=+22). Über der Antenne gibt es keine Rippen.
- Lüftungsschlitze: nicht vorgesehen (optional, nicht umgesetzt).

## Akku (wichtig)

- LiPo **503450** (ca. 50 x 34 x 5 mm inkl. Schutzschaltung), Fach 52 x 36 x 6,0. Akku von hinten einlegen, Kabel
  durch den Bodenschlitz zum **BAT-Stecker (JST 1.25, 2-polig)** führen und stecken.
- **Polarität prüfen!** Die Belegung des BAT-Steckers am Waveshare-Board **vor dem Anstecken** mit Multimeter/Schaltplan
  verifizieren – viele LiPos haben ein vertauschtes JST-Pinout. Verpolung zerstört Board und Akku.
- Deckel: nach unten herausschieben (Fingermulde), werkzeuglos. Die Rastzungen im Deckel rasten in Mulden der
  Führung (geschlossen).
- **Sommer/Auto:** Im Fahrzeug werden leicht >60 °C erreicht. LiPo bei Hitze nicht im Auto lassen:
  **Akku im Sommer entnehmen** bzw. Gerät nicht in der Sonne liegen lassen. Gehäuse aus **PETG oder besser ASA**
  drucken (PLA verformt sich ab ~55 °C).

## Vent-Clip

Separates Teil, per **einseitigem Schwalbenschwanz** (45°, druckbar ohne Stützen) auf die Rückseite geschoben.
Zwei Nuten kreuzen sich im Clip-Mittelpunkt (`CLIP_CX/CLIP_CY` = 0 / 18, auf der freien Fläche über dem Akkufach):
von oben einschieben = Hochformat, von der linken Seite = Querformat; der Clip bleibt dabei mittig (Anschlag).
Zwei federnde Zinken (15 mm Tiefe) klemmen eine Lamelle; Ruhespalt = `SLAT_T` − 0,4 mm, Standard Lamelle 2,0 mm
(Bereich 1,5–3 mm, `SLAT_T` anpassen). Zähne an den Spitzen verhaken die Lamellenkante.
Der Clip sitzt nur durch Reibung (`DT_CLR` 0,2); bei Spiel Clip mit etwas Klebeband/`DT_CLR` verkleinern.

## Druckeinstellungen

- Material **PETG oder ASA**, Düse 0,4, **Schichthöhe 0,2 mm**, **3–4 Perimeter**, Infill 15–25 %, **keine Stützen**.
- Clip: 100 % Infill empfohlen, 4+ Perimeter; langsam drucken.
- Körper: Akkufach-Decke und Hohlraum-Decken (ca. 52 x 36 mm) und Taster-Schlitze werden **überbrückt** – Brückeneinstellungen
  (langsam, viel Kühlung) bzw. Lüfter an; bei Problemen Brücke mit Stützen nur im Fach.
- Maßhaltigkeit: Spiel 0,3 (Platine) / 0,2 (Schiebeteile) – nach Test ggf. `CLR`, `DT_CLR` ändern.

## Zusammenbau

1. Akku an BAT-Stecker anschließen (Polarität!), Kabel durch den Bodenschlitz fädeln – oder erst Platine einsetzen (Stecker
   ist hinter dem Boden erreichbar, Akku von hinten).
2. Platine+LCD von vorn in den Körper legen (USB-C unten, TF oben; Zapfen in die Platinenlöcher, sie liegt auf den 4 Standoffs).
3. Frontrahmen aufsetzen, 4x M2x8 Senkschrauben eindrehen (nicht überdrehen).
4. Akku einlegen, Deckel von unten einschieben bis er einrastet.
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
