# efficient lamp – Launcher

`EfficientLamp.exe` installiert, startet und aktualisiert die Programme aus diesem Repository.
Ein einzelnes, statisch gelinktes Win32-Programm (C++17, GDI+), ca. 0,7 MB, für Windows 10 und 11.

## Was beim Start passiert

1. **Einrichten (nur beim ersten Mal):** Kopie nach `%LOCALAPPDATA%\Programs\EfficientLamp`, Startmenü-Eintrag
   „efficient lamp“, Eintrag unter „Installierte Apps“ – alles nur für den aktuellen Benutzer, ohne Adminrechte.
2. **Programmliste laden:** `catalog.json` aus dem neuesten GitHub-Release (Aufbau: [docs/katalog.md](../docs/katalog.md)).
   Bis sie da ist, zeigt der Launcher den zuletzt geladenen Stand; offline bleibt es dabei.
3. **Zustand erkennen:** installiert (Uninstall-Eintrag unter HKCU, Programmdatei, Versionsangabe) und läuft
   (Prozessname aus einem Toolhelp-Schnappschuss – kein anderes Programm wird geöffnet). Solange das Fenster sichtbar ist,
   alle 3 Sekunden; minimiert gar nicht.

## Installieren und Aktualisieren

1. ZIP aus dem Release laden, **Größe und SHA-256** mit dem Katalog vergleichen – stimmt etwas nicht, wird abgebrochen.
2. Mit eigenem Entpacker nach `%LOCALAPPDATA%\EfficientLamp\packages\<id>\<version>` entpacken
   (nur der Programmordner aus dem ZIP; Namen mit `..` oder Laufwerken werden abgelehnt).
3. Das `Setup.cmd` des Programms in einem eigenen Konsolenfenster starten und warten, bis es geschlossen wird.
4. Neu erkennen: Ist die neue Version installiert, erscheint „Installiert“; sonst ein Hinweis auf das Setup-Fenster.

Aufträge laufen nacheinander. „Alle aktualisieren“ reiht alle Updates ein. Beim Selbst-Update wird die neue Version
geprüft, die laufende `.exe` umbenannt (`EfficientLamp.old.exe`), die neue an ihren Platz gelegt und gestartet.

## Sicherheit

- Geladen wird nur über **HTTPS** von GitHub; jede Datei muss zu Größe und SHA-256 aus `catalog.json` passen.
- Der Katalog darf nur Skripte **aus dem jeweiligen Paket** starten und Programme nur **aus dem Installationsordner**
  des Programms (unter `%LOCALAPPDATA%`/`%APPDATA%`); Argumente ohne Zeichen, mit denen sich in `cmd.exe` Befehle
  verketten oder Variablen einsetzen lassen. Fehlerhafte Einträge werden übersprungen.
- Entpackt wird nur der Programmordner aus dem ZIP; Namen mit `..`, Laufwerken oder Windows-Gerätenamen (`CON`, `NUL` …)
  werden abgelehnt.
- Kennt ein älterer Launcher das Format einer neuen Programmliste nicht, lässt er die Programme in Ruhe und bietet
  nur das eigene Update an.

## Befehlszeile

| Aufruf | Wirkung |
|---|---|
| `EfficientLamp.exe` | normal starten (läuft schon einer, wird dessen Fenster nach vorne geholt) |
| `--portable` | nicht einrichten, direkt aus dem aktuellen Ordner laufen |
| `--uninstall` | Launcher entfernen (Startmenü, Eintrag, Zwischenspeicher); die Programme bleiben |
| `--catalog <URL oder Pfad>` | Testbetrieb: andere Programmliste (auch lokale Dateien); der Zwischenspeicher bleibt unberührt |
| `--theme dark` / `--theme light` | Farbschema erzwingen (sonst wie Windows) |
| `--render <datei.png> [--size 760x600] [--scale 200] [--demo]` | Fenster nur als Bild zeichnen |

## Dateien

| Ort | Inhalt |
|---|---|
| `%LOCALAPPDATA%\Programs\EfficientLamp\EfficientLamp.exe` | der Launcher |
| `%LOCALAPPDATA%\EfficientLamp\catalog.json` | zuletzt geladene Programmliste |
| `%LOCALAPPDATA%\EfficientLamp\packages\` | entpackte Pakete (für Setup, Einstellungen, Deinstallation) |
| `%LOCALAPPDATA%\EfficientLamp\launcher.log` | Protokoll (höchstens 256 KB, dann wird rotiert) |

## Quelltext

| Datei | Aufgabe |
|---|---|
| `src/core/json.cpp` | kleiner JSON-Leser |
| `src/core/sha256.cpp` | SHA-256 |
| `src/core/zip.cpp` | ZIP-Verzeichnis lesen, Inflate (RFC 1951), CRC-32 |
| `src/core/catalog.cpp` | Katalog lesen und prüfen, Versionen vergleichen |
| `src/win/net.cpp` | Downloads über WinHTTP (TLS 1.2/1.3, Proxy wie Windows) |
| `src/win/jobs.cpp` | Auftragswarteschlange: laden, prüfen, entpacken, Setup ausführen |
| `src/win/system.cpp` | Erkennung, Prozessliste, Programme/Skripte starten |
| `src/win/selfinstall.cpp` | Einrichten, Selbst-Update, Entfernen |
| `src/win/ui.cpp` | das Fenster (eigene Zeichnung, hell/dunkel, DPI-genau, Tastaturbedienung) |
| `src/win/main.cpp` | Befehlszeile, nur eine Instanz |

Bauen: `sh build.sh` · Tests: `sh tests/run.sh` (Linux, g++)
