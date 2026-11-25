# ESP32-C5-LCD-2.73 – ITS-G5 Empfänger mit Display

Empfängt **ITS-G5 / C-ITS** (IEEE 802.11p, 5,9 GHz) mit dem Waveshare/Spotpear
[ESP32-C5-LCD-2.73](https://spotpear.com/wiki/ESP32-C5-LCD-2.73-inch-LCD-Display-Screen-SD-WIFI6.html)
und zeigt die empfangenen Nachrichten direkt auf dem 320×320-Display an – bei Ampeln
inklusive **Zustand jeder Fahrspur** (Signalfarbe + Countdown).

Basierend auf den Erkenntnissen von [OpenTrafficMap](https://wiki.opentrafficmap.org/start),
[simple-its-g5-receiver-firmware](https://git.devlol.org/jstsmthrgk/simple-its-g5-receiver-firmware)
und [cits-to-go](https://codeberg.org/sascha8a/cits-to-go).

## Funktionen

- **Empfang** auf 5900 MHz (CCH), umschaltbar 5860–5920 MHz
- **Dekodierung**: 802.11 → GeoNetworking (auch signierte Pakete, ETSI TS 103 097 / IEEE 1609.2) → BTP →
  CAM, DENM, MAPEM, SPATEM; alle anderen Typen (IVIM, SREM, SSEM, CPM, VAM …) werden gezählt
- **Display (LVGL, deutsch)**, Bedienung mit den Tasten PREV / NEXT / BOOT(ENTER):
  1. *Status* – Pakete, Rate, Zähler je Nachrichtentyp, RSSI, Kanal, SD/USB
  2. *Stationen* – Fahrzeuge und RSUs mit Typ, RSSI, Alter
  3. *Ampeln* – Kreuzungen → Spuren je Zufahrt mit Manöverpfeilen, Ampelfarbe und Countdown;
     zusätzlich Kartenansicht der Spurgeometrie, eingefärbt nach Signalzustand
  4. *Warnungen* – DENM (Stau, Baustelle, Unfall …)
  5. *Einstellungen* – Kanal, SD-Log, USB-Ausgabe, Helligkeit
- **USB-Raw-Ausgabe** (SLIP-gerahmte 802.11-Frames, kompatibel zu sl2mqtt3 / OpenTrafficMap)
- **SD-Karten-Log** als pcap (in Wireshark mit ITS-Dissektor auswertbar)

## Repository

| Pfad | Inhalt |
|---|---|
| [`firmware/`](firmware/README.md) | ESP-IDF-v5.5.6-Projekt (Build, Flashen, Bedienung, Formate) |
| [`firmware/components/its/`](firmware/components/its/README.md) | Dekoder + State-Store in reinem C, mit Host-Tests |
| [`enclosure/`](enclosure/README.md) | Parametrisches Gehäuse (CadQuery) in zwei Varianten (**LiPo 503450** / **LiFePO4 14500 mit Lademodul**) – STEP, STL, **3MF**, Druck- und Montageanleitung |
| [`web/`](web/index.html), [`docs/WEBFLASHER.md`](docs/WEBFLASHER.md) | Web-Flasher (GitHub Pages, ESP Web Tools) |
| [`docs/PLAN.md`](docs/PLAN.md) | Projektplan, Hardware-Fakten, Arbeitspakete |
| `docs/reference/` | Maßzeichnung und Schaltplan des Boards (Waveshare) |

## Schnellstart

**Flashen im Browser:** GitHub-Pages-Seite des Repos öffnen (Chrome/Edge), Board per USB-C anschließen,
„Installieren“. Details und manuelles Flashen mit esptool: [docs/WEBFLASHER.md](docs/WEBFLASHER.md).

**Selbst bauen:**

```bash
cd firmware
idf.py set-target esp32c5
idf.py build flash
```

**Gehäuse drucken:** `enclosure/export/lipo/print_plate.3mf` (LiPo 503450) oder `enclosure/export/lifepo4/print_plate.3mf`
(LiFePO4 14500, internes Lademodul) in den Slicer laden (PETG/ASA – im Auto wird es heiß).
Vorher die Dicken nachmessen, siehe [enclosure/README.md](enclosure/README.md).

## Wichtige Hinweise

- **Nicht auf echter Hardware getestet.** Firmware, Dekoder und Gehäuse wurden per Build, Host-Tests
  (≈875 000 Prüfungen inkl. Fuzzing, ASan/UBSan) und Abgleich gegen die offiziellen ASN.1-Definitionen
  (Wireshark, ETSI TS 103 301 v2.2.2) geprüft, aber noch nicht mit echten Ampeldaten. Bitte mit SD-Log
  einen Mitschnitt an einer Ampel aufnehmen – damit lassen sich Fehler schnell finden.
- 802.11p-Empfang nutzt die undokumentierten Espressif-Funktionen `phy_11p_set` / `phy_change_channel`
  (vorhanden in ESP-IDF v5.5.6 für den ESP32-C5).
- Der Countdown benötigt `moy` (Minute des Jahres) im SPATEM.
- LiPo im Auto: bei Hitze herausnehmen (Akku ist werkzeuglos entnehmbar).
- **Alternative LiFePO4 (14500, hitzefester):** Gehäusevariante `lifepo4_14500` mit internem Lademodul (3,6 V).
  **⚠ Pflicht:** Dafür muss auf dem Board der Onboard-Lader abgeschaltet werden (Bauteil **L1** am ETA6098 entfernen).
  **Niemals eine LiFePO4-Zelle an ein unverändertes Board mit USB anschließen** – der Onboard-Lader lädt auf 4,2 V
  (Überladung, Brandgefahr). Anleitung, Prüfschritte und offene Annahmen (Lage des 5-V-Abgriffs, Modulmaße, J1-Polarität):
  [enclosure/README.md#lifepo4-variante](enclosure/README.md#lifepo4-variante).
- Nur Empfang – das Gerät sendet nichts.

## Lizenz

GPL-3.0-or-later (siehe [LICENSE](LICENSE)). Enthaltene Fremdkomponenten behalten ihre Lizenz:
ILI9488-Treiber (MIT), Waveshare io_extension (Apache-2.0), Montserrat-Schrift (OFL), ETSI-ASN.1-Module in den Tests
(siehe jeweilige README/NOTICE).
