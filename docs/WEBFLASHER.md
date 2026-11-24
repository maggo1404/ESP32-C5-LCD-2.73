# Web-Flasher und CI

## Überblick

| Workflow | Auslöser | Aufgabe |
|---|---|---|
| `.github/workflows/build.yml` | Push (alle Branches), Pull Request, Tags `v*` | Decoder-Hosttests, Firmware-Build, bei Tags GitHub-Release |
| `.github/workflows/firmware.yml` | wiederverwendbar (`workflow_call`) | baut die Firmware in `espressif/idf` (ESP-IDF v5.5.1, Ziel `esp32c5`) und erzeugt `firmware-factory.bin` |
| `.github/workflows/pages.yml` | Push auf `main`, manuell | baut die Firmware, erzeugt die Seite samt Manifest und deployt sie auf GitHub Pages |

Das Factory-Image entsteht mit `python -m esptool --chip esp32c5 merge_bin ... @flash_args` im Build-Ordner.
`merge_bin` ist der Name in esptool 4.x (ESP-IDF v5.5); esptool 5.x akzeptiert ihn weiterhin als Alias.
Das Image enthält Bootloader (Offset 0x2000), Partitionstabelle und Anwendung und wird ab Adresse `0x0` geschrieben.

## GitHub Pages aktivieren

1. Repository öffnen: **Settings → Pages**.
2. Unter **Build and deployment → Source** die Option **GitHub Actions** wählen.
3. Auf `main` pushen (oder den Workflow *Pages* unter *Actions* manuell starten).
4. Die Seite erscheint unter `https://maggo1404.github.io/ESP32-C5-LCD-2.73/`.

Der Flasher nutzt **ESP Web Tools 10.5.2** (enthält esptool-js 0.7.x mit ESP32-C5-Unterstützung) und braucht
Chrome oder Edge am Desktop (Web Serial, HTTPS).

## Releases

Einen Tag `vX.Y.Z` pushen:

```
git tag v1.0.0 && git push origin v1.0.0
```

`build.yml` testet, baut und legt ein GitHub-Release an mit `firmware-factory.bin` und allen Dateien aus
`enclosure/export/` (STEP, STL, 3MF). Die Release-Notizen werden automatisch erzeugt.

## Manuell flashen

Mit esptool (`pip install esptool`), Gerät im Download-Modus (bei Bedarf BOOT halten und USB einstecken):

```
esptool.py --chip esp32c5 write_flash 0x0 firmware-factory.bin
```

Alternativ (esptool 5.x): `esptool --chip esp32c5 write-flash 0x0 firmware-factory.bin`.
Vollständig löschen vorab: `esptool.py --chip esp32c5 erase_flash`.
