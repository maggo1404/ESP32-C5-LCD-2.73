# Firmware: ITS-G5-Empfänger mit Display (ESP32-C5-LCD-2.73)

ESP-IDF-Projekt für das Waveshare/Spotpear **ESP32-C5-LCD-2.73** (ILI9488 320×320,
32 MB Flash, 8 MB PSRAM). Die Firmware empfängt ITS-G5 (IEEE 802.11p, 5,9 GHz),
dekodiert CAM/DENM/MAPEM/SPATEM (Komponente `components/its`) und zeigt Stationen,
Ampeln (Spurzustände mit Countdown und Karte) und Warnungen auf dem Display.
Optional werden die Rohframes über USB (SLIP) ausgegeben und als pcap auf der
SD-Karte mitgeschrieben.

## Aufbau

| Pfad | Inhalt |
|---|---|
| `main/` | Anwendung: `rx_11p.c` (Empfang, Worker), `usb_out.c` (SLIP), `sd_log.c` (pcap), `board.c` (I²C, IO-Expander, SPI, LCD), `lvgl_port.c`, `ui*.c`, `buttons.c`, `settings.c` (NVS) |
| `components/its/` | Decoder + State-Store (reines C, getrennt host-testbar) |
| `components/esp_lcd_ili9488/` | ILI9488-Treiber (MIT, aus dem Waveshare-Demo, lokal angepasst, siehe dortige README) |
| `components/io_extension/` | Treiber für den CH32V003-IO-Expander (Reset, Backlight-PWM, Akku-ADC), aus dem Waveshare-Demo (Apache-2.0-Paket, Lizenztext liegt bei) |
| `partitions.csv` | 32 MB Flash: NVS, PHY-Init, Factory-App 6 MB (ab 0x10000) |
| `sdkconfig.defaults` | Ziel `esp32c5`, PSRAM, Konsole auf UART0, LVGL-Optionen |

## Bauen

Benötigt wird **ESP-IDF v5.5.x** (gebaut und geprüft mit **v5.5.6**,
Commit `880a203ae603d08a94d06aa01e0d6761958e5e9e`). Die ROM/PHY-Funktionen
`phy_11p_set` und `phy_change_channel` stecken in `libphy.a` dieser Version
(mit `riscv32-esp-elf-nm` geprüft); der von OpenTrafficMap gepinnte Commit
`4dc1c655…` ist nicht nötig.

```sh
. $IDF_PATH/export.sh
cd firmware
idf.py set-target esp32c5
idf.py build
```

LVGL kommt als Registry-Komponente `lvgl/lvgl ~8.4` (`main/idf_component.yml`) und
wird beim ersten Konfigurieren geladen (Internetzugang zu `components.espressif.com`
nötig). Der Bootloader nutzt Log-Level WARN, damit er in die 0x6000 Bytes vor der Partitionstabelle passt. Hinweis: `LV_COLOR_16_SWAP=y` ist erforderlich, der ILI9488-Treiber
erwartet byte-vertauschtes RGB565.

Ergebnis (v5.5.6): App ca. 1,46 MB (`0x1766e0` Bytes) in der 6-MB-Partition, 76 % frei;
Bootloader 0x4c90 Bytes (20 % frei); interner RAM nach dem Linken rund 190 kB frei (der Decoder-Store
(~93 kB `.bss`) liegt im PSRAM, siehe `main/linker.lf`; IDF verlagert dabei auch die Wi-Fi-`.bss`).

## Flashen

Das Board hängt per USB-C an der USB-Serial-JTAG-Schnittstelle (GPIO13/14).
Zum Flashen BOOT gedrückt halten, kurz RESET/PWR betätigen, loslassen.

Einzelne Dateien:

```sh
idf.py -p /dev/ttyACM0 flash
```

oder (Offsets für den ESP32-C5: Bootloader bei **0x2000**):

```sh
cd build
python -m esptool --chip esp32c5 -p /dev/ttyACM0 -b 460800 write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 32MB \
  0x2000 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 its-g5-display.bin
```

### Zusammengeführtes Image (`firmware-factory.bin`)

```sh
cd build
python -m esptool --chip esp32c5 merge_bin -o firmware-factory.bin @flash_args
# entspricht:
# esptool.py --chip esp32c5 merge_bin -o firmware-factory.bin --flash_mode dio \
#   --flash_freq 80m --flash_size 32MB 0x2000 bootloader/bootloader.bin \
#   0x8000 partition_table/partition-table.bin 0x10000 its-g5-display.bin
```

Das Image beginnt bei Flash-Offset 0 (Bereich vor 0x2000 ist mit 0xFF gefüllt) und wird
daher an **0x0** geschrieben:

```sh
python -m esptool --chip esp32c5 -p /dev/ttyACM0 write_flash 0x0 firmware-factory.bin
```

(Für Web-Flasher/ESP Web Tools: ein Teil bei Offset 0.) Die NVS-Partition wird beim
ersten Start automatisch angelegt.

## Ausgaben

### Logs (UART0)

Konsole und `ESP_LOG` laufen **nicht** über USB, sondern über UART0:
**TX = GPIO11, RX = GPIO12, 115200 Baud, 8N1** (Stiftleiste „UART“ unten links, 3,3 V-Pegel).
Grund: Über USB-Serial-JTAG läuft der binäre SLIP-Datenstrom. Die frühen
ROM-Bootmeldungen können zusätzlich auf USB erscheinen; SLIP-Parser verwerfen
solche Bytes.

### USB-Rohausgabe (SLIP)

Einstellungen → „USB-Ausgabe“ = an. Jeder fehlerfreie **Broadcast-Frame**
(802.11-Header + LLC/SNAP + GeoNetworking …, **ohne FCS**) wird einzeln
SLIP-kodiert und auf den USB-CDC-Port geschrieben:

```
0xC0  <Frame, 0xC0 -> 0xDB 0xDC, 0xDB -> 0xDB 0xDD>  0xC0
```

Das entspricht dem „raw“-Modus der simple-its-g5-receiver-Firmware und ist
kompatibel zu `sl2mqtt3`. Es gibt keine Zeitstempel/RSSI im Strom. Wenn der Host
nicht liest (USB-Puffer voll), werden Frames verworfen und der Port 1 s lang nicht mehr beschrieben; der Decoder wird nie durch USB verzögert (Schreiben ohne Wartezeit).
Das Port-Symbol in der Statuszeile ist grau (aus), gelb (an, kein Host) oder grün
(Host verbunden).

### pcap auf SD-Karte

Einstellungen → „SD-Log“ = an. Nach dem Einlegen einer FAT-formatierten TF-Karte (CS
GPIO9, gleicher SPI-Bus wie das Display) wird pro Start eine neue Datei
`/sdcard/its_0001.pcap`, `its_0002.pcap` … angelegt (nächste freie Nummer).
Format: pcap, Magic `a1b2c3d4`, Version 2.4, snaplen 65535, **LINKTYPE 105
(IEEE 802.11, ohne Radiotap)**. Die Zeitstempel sind Zeit seit dem Start (die
Platine hat keine gesetzte Uhrzeit), Wireshark zeigt daher Daten im Jahr 1970 – mit
„Zeitreferenz“ bzw. relativer Zeit arbeiten. Es wird ca. alle 2 s geschrieben und
synchronisiert. Karte entfernt oder Schreibfehler: Datei wird geschlossen, das Symbol
wird rot/gelb, alle 4 s folgt ein neuer Versuch (neue Datei). Zum sicheren Entnehmen
SD-Log erst ausschalten.

In Wireshark: Datei öffnen, Anzeigefilter z. B. `llc.type == 0x8947` (GeoNetworking)
oder `wlan.da == ff:ff:ff:ff:ff:ff`. Für die Dekodierung von CAM/DENM/… ist
das Wireshark-Protokoll *GeoNetworking* (GN) + *BTP* zuständig; falls es nicht
automatisch greift: Einstellungen → Protokolle → LLC → „ethertype 0x8947 → GN“
(`Analyze → Decode As…`). Gesicherte Pakete (ETSI TS 103 097) werden von Wireshark
nur mit installiertem Zertifikatsprofil weiter zerlegt.

## Bedienung (3 Tasten)

| Taste | Pin | Funktion |
|---|---|---|
| PREV | GPIO2 | zurück / hoch (hält man sie, wiederholt sie) |
| NEXT | GPIO3 | weiter / runter |
| ENTER (BOOT) | GPIO28 | kurz: öffnen / ändern, lang (> 0,7 s): zurück |

Ebenen: **Seitenleiste** (unten markiert) → *Liste* → *Detail*.

* Seitenleiste: PREV/NEXT wechseln die Seite (Status, Stationen, Ampeln, Warnungen,
  Einstellungen), ENTER öffnet die Liste der Seite.
* Liste: PREV/NEXT wählen einen Eintrag (umlaufend), ENTER öffnet das Detail,
  langes ENTER geht zurück zur Seitenleiste.
* Detail Station/Warnung: PREV/NEXT blättern durch die Einträge, langes ENTER zurück.
* Detail Ampel: PREV/NEXT scrollen die Spurliste, ENTER wechselt zwischen **Spuren**
  und **Karte** (in der Karte wechseln PREV/NEXT die Kreuzung), langes ENTER zurück.
* Einstellungen: ENTER bei *Kanal*/*Helligkeit* startet das Ändern (PREV/NEXT ändern den Wert,
  ENTER oder lang beendet), *SD-Log*/*USB-Ausgabe* schalten direkt um,
  *Zähler löschen* leert Stationen/Ampeln/Warnungen und setzt die Verlustzähler zurück. Alles wird in NVS gespeichert.

### Seiten

* **Status**: Pakete gesamt und pro Sekunde, Zähler je Nachrichtentyp, letzter RSSI, Kanal,
  SD-/USB-Status, Akkuspannung, Laufzeit, verworfene Frames, freier Heap.
* **Stationen**: ID, Typ, RSSI, Alter, `Typen` (C = CAM, D = DENM, M = MAPEM, S = SPATEM,
  + = andere). Detail mit Position/Geschwindigkeit/Richtung (aus CAM), MAC.
* **Ampeln**: Liste der Kreuzungen (MAP/SPAT vorhanden, Alter); Detail pro Zufahrt mit Spuren,
  Manöverpfeilen, Ampelfarbe der Signalgruppe und Countdown (likelyTime, sonst minEndTime),
  Ansicht **Karte**: Spuren aus den MAP-Knoten maßstäblich, Zufahrten dick und nach Signalzustand
  eingefärbt, weißer Punkt = Haltelinie, Zahl = Spur-ID. Ohne aktuelle SPAT-Daten (> 6 s) sind
  die Punkte grau.
* **Warnungen**: DENM mit Ursache, Station, Alter, RSSI; Detail mit Positionsangabe.
* **Einstellungen**: siehe oben.

Statuszeile oben: Kanal, Pakete/s, USB-Symbol, SD-Symbol (grün = schreibt, gelb = keine Karte,
rot = Fehler), Akkuspannung.

## Kanäle

Standard ist **5900 MHz** (G5-CCH). Wählbar (Einstellungen) sind 5860, 5870, 5880, 5890, 5900, 5910
und 5920 MHz; die Umschaltung erfolgt nach dem Bestätigen (ENTER) im Worker-Task über `phy_change_channel()`, danach werden noch wartende Frames des alten Kanals verworfen:
5860 = SCH4, 5870 = SCH3, 5880 = SCH1, 5890 = SCH2, 5900 = CCH. Es wird immer 10-MHz-Kanalbreite
(802.11p) empfangen. Übliche Quellen: Ampeln (MAPEM/SPATEM) und Fahrzeuge (CAM) auf 5900 MHz.

## Funktionsweise (Kurzfassung)

* `rx_11p.c`: Wi-Fi ohne Verbindung, Promiscuous-Modus (alle Typen außer FCS-Fehler),
  `phy_11p_set(1,0)`, `esp_wifi_set_channel(140)`, dann `phy_change_channel(freq,1,0,0)`. Der Callback
  verwirft MISC-Pakete, `rx_state != 0`, Frames ohne Broadcast-Zieladresse (addr1 = ff:…) und zu große
  Frames, kopiert den Rest ohne `malloc` in einen Pool aus 24 Puffern und reiht ihn zum Worker ein.
* Worker: `its_store_ingest()`, dann USB (SLIP) und SD (über Message-Buffer in PSRAM an den SD-Task),
  1×/s `its_store_tick()`. `its_store_lock/unlock` sind per FreeRTOS-Mutex überschrieben (`main.c`).
* LCD und SD-Karte teilen SPI2 (SCLK 6, MOSI 7, MISO 5; CS 8 bzw. 9). Der SPI-Master-Treiber
  serialisiert die Zugriffe beider Geräte; der SD-Treiber hält den Bus pro Transaktion exklusiv
  (`spi_device_acquire_bus`), LVGL-Flushes warten dann kurz. Der SD-Task schreibt gesammelt
  (8-kB-Blöcke im internen RAM) und blockiert den Decoder nie.
* LVGL 8.4 läuft in einem einzigen Task, nur dieser ruft LVGL auf. Die UI kopiert die benötigten
  Teile des Stores unter dem Store-Lock in statische Snapshots (kurze Sperrzeit) und aktualisiert
  alle 250 ms.
* Schrift: Montserrat (Latin-1, Umlaute/ß) per TinyTTF aus dem Image (`main/fonts`, OFL).

## Nicht auf echter Hardware geprüft

Gebaut und statisch geprüft wurde nur mit ESP-IDF v5.5.6; ein Lauf auf dem Board stand nicht zur
Verfügung. Unbestätigt sind insbesondere: Empfang auf dem C5 (insb. die Frame-Länge:
verwendet wird `sig_len - 4`, ersatzweise begrenzt durch `dump_len`, OpenTrafficMap nutzt `dump_len`),
Farbwiedergabe/Byte-Reihenfolge des Displays, SPI-Takt 40 MHz, Akku-ADC-Skalierung (Faktor 3 laut
Waveshare-Demo), IO-Expander-Pin 5 (USB/CAN-Auswahl, wird beim Start auf 0 = USB gesetzt), SD-Hotplug, USB-SLIP-Durchsatz
und der RAM-Bedarf zur Laufzeit.
