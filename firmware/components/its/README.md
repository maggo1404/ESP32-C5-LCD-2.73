# Komponente `its` – ITS-G5-Decoder und Zustandsspeicher

Reines C99 ohne ESP-IDF-Abhängigkeiten (läuft auch auf dem Host). Vertrag mit der Anwendung:
[`include/its.h`](include/its.h).

## Schichten

| Datei | Aufgabe |
|---|---|
| `src/its_frame.c` | `its_parse_frame`: 802.11-Datenframe (24 B, +2 QoS, +6 bei 4 Adressen, +4 HT-Control) → LLC/SNAP `AA AA 03 00 00 00 89 47` → GeoNetworking: Basic Header (NH 1 = Common, NH 2 = Secured), bei Secured die IEEE-1609.2-Hülle (COER, ETSI TS 103 097: `unsecuredData` oder `signedData → tbsData → payload → unsecuredData`, OER-Längenangaben), Common Header (8 B), erweiterter Header je Typ (Beacon 24, SHB/TSB 28, GBC/GAC 44, GUC 48, LS 36/48) → BTP-A/B (4 B) → ItsPduHeader (protocolVersion, messageID, stationID). Beacon/LS liefern `ITS_OK` mit `pdu == NULL`. |
| `src/uper.c` | Bounds-geprüfter UPER-Leser (Bits, eingeschränkte/uneingeschränkte Zahlen, Längen, IA5String, Extension-Zusätze und offene Typen überspringen). Fehler sind „klebrig“ (`u->err`). |
| `src/its_cam.c` | CAM (EN 302 637-2 v1.x und TS 103 900 v2): Station, Position, Geschwindigkeit, Richtung. |
| `src/its_denm.c` | DENM (EN 302 637-3 v1.3, TS 103 831 v2): actionID, Termination, eventPosition, eventType (cause/subCause). |
| `src/its_mapspat.c` | MAPEM/SPATEM (ISO TS 19091 / SAE J2735, ETSI TS 103 301): Fahrspuren, Knoten (absolute cm-Koordinaten, max. 8 Knoten je Spur, gleichmäßig ausgedünnt), Manöver, Verbindungen mit Signalgruppe; SPAT: erster Phasenzustand je Signalgruppe mit startTime/minEndTime/maxEndTime/likelyTime. Extension-Zusätze, CHOICE-/ENUM-Erweiterungen und Regional-Erweiterungen werden über ihre Open-Type-Länge übersprungen. |
| `src/its_names.c` | Deutsche Namen (UTF-8) für Nachrichten, Stationstypen, Ursachencodes, Phasen; `its_phase_light`. |
| `src/its_store.c` | Zustandsspeicher: Zähler, Stationstabelle (LRU), Kreuzungen (MAP+SPAT nach `(region,id)` zusammengeführt), DENM-Tabelle (Schlüssel actionID), Ring der letzten Frames, `its_store_tick` (geglättete Rate, Ablauf nach 60 s / 120 s / 10 min bzw. Termination), Uhr für Countdowns (`its_timemark_remaining_tenths`, Stundenwechsel). Schwache `its_store_lock/unlock` – die Firmware überschreibt sie mit einem Mutex. |

Konventionen des Speichers: Stationsslot belegt ⇔ `packets > 0`; `recent_head` ist der Index des **nächsten** zu
beschreibenden Slots (neuester Eintrag = `(head + 15) % 16`); die dekodierten MAP-Kreuzungen (~5 KB) liegen in statischen
Scratch-Puffern (Zugriff nur unter dem Store-Lock), `its_store_t` (~71 KB) ist statisch zu halten.

Die Dekodierlogik für MAP/SPAT und die GN-Header lehnt sich an
[cits-to-go](https://github.com/OpenTrafficMap/cits-to-go) (Kotlin, GPL-3.0) an, prüft aber gegen das ASN.1 und
überspringt Erweiterungen statt sie abzulehnen.

## Einschränkungen

- Countdowns (`its_timemark_remaining_tenths`) brauchen im SPATEM `moy` **und** `timeStamp`; fehlt `moy` (oder ist es
  527040 = ungültig), gibt es keine Uhr und der Countdown ist unbekannt (-1).
- Das alte Secured-Format nach ETSI TS 103 097 v1.2.1 wird nicht unterstützt (nur IEEE 1609.2 / TS 103 097 ≥ v1.3.1);
  solche Frames liefern `ITS_ERR_SECURITY`.
- Mehr als `ITS_MAX_LANES` (64) Spuren bzw. `ITS_MAX_SIGNAL_STATES` (48) Signalgruppen werden nicht gespeichert; die
  Anzahl steht in `lanes_dropped` / `states_dropped`.
- SPAT-Daten gelten nach 30 s ohne Update als veraltet (`has_spat = false`), MAP-Geometrie bleibt bis 120 s.
- DENM-Ablauf pauschal nach 10 min (validityDuration wird nicht ausgewertet).

## Tests (Host)

```sh
cmake -S firmware/components/its/test -B build-its && cmake --build build-its && ctest --test-dir build-its --output-on-failure
# oder:  make -C firmware/components/its/test test
```

Gebaut wird mit `-Wall -Wextra -Wpedantic -Wshadow -Werror` und AddressSanitizer + UBSan
(`-DITS_SANITIZE=OFF` schaltet das ab). Umgebungsvariable `ITS_FUZZ_ITERS` (Standard 200000) steuert den Fuzz-Lauf.

Inhalt: UPER-Grundfunktionen, Namen, Zeitmarken, ~50 Testvektoren (CAM v1/v2, DENM v1/v2, MAPEM, SPATEM, unsecured
und signedData-gesichert, SHB/TSB/GBC/GUC, BTP-A/B, QoS/4-Adressen, Beacon, Negativfälle), Abschneiden jedes Frames an
jeder Länge, Speicher-Tests (LRU, Ablauf, Zusammenführen, Lock-Balance) und ein Fuzz-Lauf mit zufälligen Mutationen
(Frames und reine UPER-Payloads, exakt dimensionierte Heap-Puffer).

### Testvektoren neu erzeugen

```sh
pip install asn1tools
python3 -I firmware/components/its/test/gen_vectors.py     # schreibt test/vectors.h (reproduzierbar, eingecheckt)
```

`gen_vectors.py` kodiert mit `asn1tools` aus den ETSI-ASN.1-Modulen in `test/asn1/` (CAM, DENM, jeweils Release 1 und 2)
bzw. aus `test/asn1/dsrc-subset.asn` (MAPEM/SPATEM), baut komplette 802.11/LLC/GN/BTP-Frames (auch mit von Hand gebauter
1609.2-signedData-Hülle) und leitet die erwarteten Werte aus den *Eingabewerten* ab. Für „Zukunfts“-Nachrichten mit
Extension-Zusätzen wird das ASN.1 zur Laufzeit textuell erweitert. Quellen und Lizenz: `test/asn1/README.md`.
