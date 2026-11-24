# Projektplan: ITS-G5 Empfänger mit Display (ESP32-C5-LCD-2.73)

Ziel: Der Waveshare/Spotpear **ESP32-C5-LCD-2.73** empfängt ITS-G5 (802.11p, 5,9 GHz),
dekodiert C-ITS-Nachrichten und zeigt sie auf dem 320×320-Display an. Bei Ampeln
(MAPEM + SPATEM) wird der Zustand jeder Fahrspur dargestellt. Dazu ein 3D-druckbares
Gehäuse (Handgerät mit LiPo, Lüftungsgitter-Clip fürs Auto) und ein Web-Flasher über
GitHub Pages.

## Entscheidungen (mit dem Nutzer abgestimmt)

| Thema | Entscheidung |
|---|---|
| Framework | ESP-IDF **v5.5.x** + **LVGL 8.4** (wie die Waveshare-Beispiele, LVGL 9 ist mit deren Treiber inkompatibel) |
| Extras | USB-Raw-Ausgabe (SLIP, kompatibel zu sl2mqtt3) und SD-Karten-Log (pcap, LINKTYPE 105) |
| Gehäuse | Handgerät mit herausnehmbarem LiPo **503450 (1000 mAh, ca. 50×34×5 mm)** und integriertem **Lüftungsgitter-Clip** |
| Branch | `main` (Default, Pages-Deploy) und `claude/admiring-bohr-ybe4c1` |
| Lizenz | GPL-3.0-or-later (Decoder-Logik ist angelehnt an cits-to-go, GPL-3.0) |

## Hardware-Fakten (Quelle: Spotpear/Waveshare-Wiki, Schaltplan in docs/reference)

- ESP32-C5-WROOM, 32 MB Flash, 8 MB PSRAM, USB-Serial-JTAG an USB-C (GPIO13/14)
- LCD ILI9488, 320×320, RGB666, SPI: SCLK GPIO6, MOSI GPIO7, MISO GPIO5, DC GPIO4, CS GPIO8
- LCD-Reset und Backlight-PWM über **CH32V003 IO-Expander** (I²C 0x24, PWM-Register 0x05)
- I²C-Bus: SDA GPIO27, SCL GPIO26 (CH32V003 0x24, QMI8658 0x6B, PCF85063 0x51, SHTC3 0x70)
- TF-Karte SDSPI am selben SPI-Bus: CS GPIO9
- Tasten: PREV GPIO2, NEXT GPIO3, BOOT/ENTER GPIO28 (alle aktiv-low)
- UART0: TX GPIO11, RX GPIO12
- Akku-Spannung: ADC des CH32V003

### Mechanik (Maßzeichnung docs/reference/board-dimensions.jpg, Einheit mm)

- Platine/LCD-Modul: **68,00 × 68,00**; aktive Displayfläche 48,96 × 48,96, ungefähr mittig
- 4 Befestigungslöcher **Ø 2,2 (M2)**, Raster **62,00 × 62,00**, 3,00 vom Rand
- Rückseite (Blick auf die Rückseite, USB-C unten):
  - USB-C mittig an der Unterkante (Mitte ca. x = 34)
  - Seitliche Taster an **beiden** Seitenkanten unten: ca. 52,7 und 61 mm von oben
    (links „KEY_R“ und „PWR“, rechts „KEY_L“ und „BOOT“; von vorne gesehen gespiegelt)
  - TF-Kartenslot oben links (x ≈ 10–20), Karte wird über die Oberkante eingeschoben
  - ESP32-C5-WROOM mit **PCB-Antenne oben rechts** (x ≈ 38–58, y ≈ 0–28): dort kein Metall, möglichst dünne Wand
  - Stecker: UART unten links (x ≈ 13), I2C unten rechts (x ≈ 54), RTC-Batterie (x ≈ 10, y ≈ 52),
    **BAT (LiPo, JST 1.25)** rechts unten (x ≈ 57, y ≈ 53)
- Dicken sind auf der Seite nicht angegeben, Schätzwerte (parametrisch, vom Nutzer nachmessen):
  LCD-Modul vor der Platine 4,0, Platine 1,6, Bauteile hinten max. 4,0

## Arbeitspakete

Ausführung durch Sonnet-Subagenten; jedes Paket wird danach von einem **unabhängigen**
Sonnet-Agenten geprüft (frischer Kontext, nur Spezifikation + Code).

### WP1 – Decoder-Komponente `firmware/components/its` (host-testbar, reines C99)
Implementiert `include/its.h` (Vertrag, nicht ändern ohne Absprache):
802.11 → LLC/SNAP (0x8947) → GeoNetworking (Basic, Secured-Hülle nach ETSI TS 103 097 /
IEEE 1609.2 COER: unsecuredData bzw. signedData → tbsData → payload), Common Header,
SHB/TSB/GBC/GUC/Beacon) → BTP-A/B → ItsPduHeader; UPER-Decoder für CAM, DENM, MAPEM, SPATEM
(ISO TS 19091 / ETSI TS 103 301); State-Store; deutsche Namen.
Host-Tests in `firmware/components/its/test/` (CMake/Makefile, gcc), Testvektoren mit
Python `asn1tools` aus den offiziellen ASN.1-Modulen erzeugt.

### WP2 – Firmware `firmware/` (ESP-IDF-Projekt, ohne Decoder)
- 802.11p-Empfang: promiscuous, `phy_11p_set(1,0)`, `esp_wifi_set_channel(140)`,
  `phy_change_channel(freq_mhz,1,0,0)`; Standard **5900 MHz** (CCH), Kanalwahl im Menü
  (5860/5870/5880/5890/5900/5910/5920)
- Nur fehlerfreie Broadcast-Frames, FCS (4 Byte) abschneiden, Queue → Worker-Task
- Worker: `its_store_ingest`, SLIP-Ausgabe über USB-Serial-JTAG (Konsole/Logs auf UART0
  umleiten, damit der SLIP-Strom sauber bleibt), pcap-Log auf SD
- Display: ILI9488 per Waveshare-Komponenten (IO-Expander für Reset/Backlight), LVGL 8.4
- UI (deutsch, 3 Tasten: PREV/NEXT = blättern/auswählen, ENTER = öffnen/zurück durch langes Drücken):
  1. **Status**: Pakete gesamt, Rate/s, Zähler je Nachrichtentyp, letzter RSSI, Kanal, SD/USB-Status
  2. **Stationen**: Liste (ID, Typ, RSSI, Alter, Nachrichtentypen)
  3. **Ampeln**: Liste der Kreuzungen → Detail: pro Zufahrt die Fahrspuren mit Manöver-Pfeilen,
     Ampelfarbe der Signalgruppe und Countdown (likelyTime/minEndTime); zweite Ansicht
     „Karte“: Spur-Polylinien aus MAP-Knoten, eingefärbt nach Signalzustand
  4. **Warnungen** (DENM): Ursache, Alter, Station
  5. **Einstellungen**: Kanal, SD-Log an/aus, USB-Ausgabe an/aus, Helligkeit (NVS)
- `sdkconfig.defaults`, Partitionstabelle (32 MB Flash), `idf_component.yml`

### WP3 – Gehäuse `enclosure/` (CadQuery, parametrisch)
Teile: Frontrahmen (Displayfenster), Gehäusekörper (Platine auf M2-Domen, Aussparungen USB-C,
TF-Slot, Tasten-Stößel für die Seitentaster), Akkufach mit Schiebedeckel (Akku herausnehmbar,
Kabel zum BAT-Stecker), **Lüftungsgitter-Clip** als separates Teil per Schwalbenschwanz
(hoch/quer einschiebbar), keine Metallteile über der Antenne.
Exporte: `*.step` je Teil + Baugruppe, `*.stl`, eine **`.3mf`** mit allen Teilen druckfertig
angeordnet. Plus `enclosure/README.md` mit Druckeinstellungen (PETG/ASA fürs Auto!).

### WP4 – CI + Web-Flasher (`.github/workflows`, `web/`)
- Workflow `build.yml`: Host-Tests des Decoders, Firmware-Build im `espressif/idf:v5.5.x`-Container,
  `esptool merge_bin` → `firmware-factory.bin`, Artefakte; bei Tags GitHub-Release
- Workflow `pages.yml` (nur `main`): baut Firmware, legt Bins + ESP-Web-Tools-Manifest
  in `web/` und deployt über `actions/deploy-pages`
- `web/index.html` (deutsch): Erklärung, Flash-Button (ESP Web Tools bzw. esptool-js, ESP32-C5-Support
  prüfen), Hinweise BOOT-Taste, Links zu Gehäuse-Dateien

### WP5 – Integration & Doku (Orchestrator)
README, Zusammenführen, Commits, Push auf `main` und Feature-Branch.

## Prüfungen (unabhängige Sonnet-Agenten)
- R1: Decoder – eigene Testvektoren mit asn1tools erzeugen, Grenzfälle (Extensions, Regional,
  fehlende Optionals, Secured-Pakete), Speichersicherheit (ASan/UBSan, Fuzz-Kurzlauf)
- R2: Firmware – Build mit ESP-IDF, Pins gegen Wiki, Thread-Safety, SLIP/pcap-Format
- R3: Gehäuse – Exporte neu erzeugen, Maße/Toleranzen/Kollisionen, Druckbarkeit
- R4: CI/Web – Workflow-Syntax (actionlint), Manifest, Seite im Browser (Playwright)
