# Entwicklung

Alles wird unter **Linux mit MinGW-w64** für Windows (x64) gebaut – genau so wie in GitHub Actions.
Getestet wurde unter Ubuntu 24.04.

```sh
sudo apt-get install g++ python3 g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
```

## Bauen

```sh
sh tools/build-all.sh                  # DeskCats, SnowDesk und den Launcher (je build/ im Projektordner)
python3 tools/package.py --tag v1.0.0  # ZIPs, EfficientLamp.exe, catalog.json und SHA256SUMS.txt nach dist/
```

Die ZIPs sind reproduzierbar (feste Zeitstempel und Reihenfolge): gleicher Quellstand ergibt dieselbe Prüfsumme.
`package.py` prüft vorher, dass jede Versionsnummer in `app.json` mit den Quelltexten übereinstimmt, und vereinheitlicht
Zeilenenden und BOM der Windows-Skripte (`.ps1` mit BOM, damit Windows PowerShell 5.1 Umlaute richtig liest).

TriggerSwitch und WaveMuteSync werden nicht vorab kompiliert: Ihr `Setup.ps1` übersetzt den C#-Quelltext auf dem PC
mit dem Compiler aus .NET Framework 4.8.

## Testen

```sh
sh launcher/tests/run.sh             # JSON, SHA-256, ZIP/Inflate, Katalog-Prüfung, Versionsvergleich
sh launcher/tests/run.sh dist/catalog.json   # zusätzlich den erzeugten Katalog prüfen
sh apps/deskcats/tests/run.sh        # Katzen: Dauertest mit drei Monitoren, sechs Startwerte
sh apps/snowdesk/tests/run.sh        # Schnee-Simulation und "Echtes Wetter"
```

Der Launcher lässt sich unter Windows (oder Wine) ohne GitHub ausprobieren:

```
EfficientLamp.exe --portable --catalog C:\pfad\zu\dist\catalog.json
python3 tools/package.py --tag test --base-url "C:\pfad\zu\dist\\"   # Katalog mit lokalen Paketpfaden
EfficientLamp.exe --render bild.png --theme dark --demo --scale 200      # Fenster als Bild (für die README)
```

## Ein Release veröffentlichen

1. Versionsnummern anpassen – je Programm in `app.json` und an den Stellen aus `versionCheck`,
   für den Launcher in `launcher/src/win/common.h`, `launcher/res/launcher.rc` und `launcher/res/app.manifest`.
2. Committen, dann taggen und pushen:
   ```sh
   git tag v1.0.1
   git push origin main v1.0.1
   ```
3. Der Workflow [release.yml](../.github/workflows/release.yml) testet, baut, schnürt die Pakete und veröffentlicht
   das Release mit allen Dateien. Ab dann sehen alle Launcher die neuen Versionen (unveränderte Programme behalten
   ihre Version und werden nicht erneut angeboten).

Jeder Push auf `main` und jeder Pull Request läuft durch [build.yml](../.github/workflows/build.yml); die fertigen
Pakete hängen dort als Artefakt an.

## Logo und Bilder

`brand/` enthält die SVG-Quellen (Logo, App-Kacheln). `python3 brand/make_brand.py` erzeugt daraus
`launcher/res/efficientlamp.ico`, die PNGs für den Launcher und die Bilder in `docs/media/`
(braucht `rsvg-convert`, Pillow und die Schrift Inter).
