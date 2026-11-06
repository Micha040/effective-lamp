# catalog.json – die Programmliste des Launchers

Der Launcher lädt bei jedem Start
`https://github.com/Micha040/effective-lamp/releases/latest/download/catalog.json`.
Die Datei wird **nicht von Hand geschrieben**: `tools/package.py` erzeugt sie beim Release aus den
`apps/*/app.json` und ergänzt Download-Adresse, Größe und SHA-256 jedes Pakets.

## Aufbau

```jsonc
{
  "schema": 1,                       // Format-Version; ein Launcher lehnt unbekannte Versionen ab
  "name": "efficient lamp",
  "release": "v1.0.0",               // Tag des Releases
  "published": "2026-11-06",
  "repo": "Micha040/effective-lamp",
  "launcher": {                      // neueste Launcher-Version (für das Selbst-Update) – Aufbau bleibt in allen Formaten gleich
    "version": "1.0.0", "file": "EfficientLamp.exe",
    "url": "https://github.com/…/releases/download/v1.0.0/EfficientLamp.exe",
    "size": 707584, "sha256": "…"
  },
  "apps": [ { … }, … ]               // Reihenfolge = Reihenfolge im Launcher ("order" in app.json)
}
```

### Ein Programm

| Feld | Bedeutung |
|---|---|
| `id` | eindeutig, nur `a-z`, `0-9`, `-` (für Ordner und Bildkachel) |
| `name`, `version` | Anzeige; `version` wird mit der installierten Version verglichen (`1.10.0` > `1.9.2`) |
| `tagline`, `description` | Kurzbeschreibung (die Karte zeigt `description`, höchstens zwei Zeilen) |
| `requires` | optionaler Hinweis in der Karte, z. B. „braucht Wave Link 3“ |
| `color` | Ersatzfarbe der Kachel, falls der Launcher kein eigenes Bild für die `id` hat |
| `page` | Anleitung (README auf GitHub) |
| `download.file`, `url`, `size`, `sha256` | das ZIP-Paket; vor dem Entpacken werden Größe und SHA-256 geprüft |
| `download.folder` | oberster Ordner im ZIP – nur dessen Inhalt wird entpackt |
| `process` | Name der .exe, an dem der Launcher erkennt, dass das Programm läuft |
| `installDir` | für „Installationsordner öffnen“ |
| `detect.uninstallKey` | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\<Schlüssel>`; Version aus `DisplayVersion` |
| `detect.exe` | gilt nur als installiert, wenn diese Datei existiert |
| `detect.versionFile` | Version aus dieser Datei lesen (`Version = "1.2.3"`), für Programme ohne Registry-Eintrag |
| `install`, `uninstall`, `open` | was ausgeführt wird (siehe unten) |

Pfade dürfen nur mit `%LOCALAPPDATA%\` oder `%APPDATA%\` beginnen und kein `..` enthalten. Versionen bestehen aus
Ziffern, Buchstaben, `.` und `-` und beginnen mit einer Ziffer. Ein Eintrag, der eine Regel verletzt, wird übersprungen
(und im Protokoll des Launchers genannt) – die übrigen Programme erscheinen trotzdem.

Ändert sich das Format (`schema`), lesen ältere Launcher nur noch den Abschnitt `launcher` und bieten ihr eigenes Update
an. Deshalb ändert sich dessen Aufbau nie.

### Befehle

```jsonc
"install":   { "script": "Setup.cmd", "args": "-Action Install" },   // Skript aus dem entpackten Paket
"uninstall": { "script": "Uninstall.cmd" },
"open":      { "exe": "%LOCALAPPDATA%\\Programs\\DeskCats\\DeskCats.exe", "label": "Öffnen", "wait": false }
```

- `script`: nur ein Dateiname im entpackten Paket. Der Launcher startet es in einem eigenen Konsolenfenster
  (`cmd.exe /d /c ""<Paket>\<script>" <args>"`) und wartet auf das Ende (`"wait": false`: nicht warten).
- `exe`: ein installiertes Programm – nur innerhalb von `installDir` des Programms.
- `args` darf keine Zeichen enthalten, mit denen `cmd.exe` Befehle verkettet oder Variablen einsetzt
  (`& | < > ^ % ! "`, Steuerzeichen).
- `label`: Beschriftung des Knopfs (Standard „Öffnen“).

Pakete landen in `%LOCALAPPDATA%\EfficientLamp\packages\<id>\<version>`; ältere Versionen räumt der Launcher nach
einer erfolgreichen Installation weg, nach einer Deinstallation den ganzen Ordner.

## Ein neues Programm aufnehmen

1. Ordner `apps/<id>/` mit Quelltext, `README.md` und einem Setup (`Setup.cmd`), das ohne Administratorrechte
   installiert und – für die Erkennung – einen Uninstall-Eintrag unter HKCU schreibt oder eine Datei mit Version ablegt.
2. `apps/<id>/app.json` nach dem Vorbild der vorhandenen anlegen. `versionCheck` listet die Stellen im Quelltext,
   an denen die Versionsnummer steht – `tools/package.py` bricht ab, wenn sie nicht zur `version` passen.
3. Optional eine Kachel: `brand/tiles/<id>.svg`, in `brand/make_brand.py` eintragen und im Launcher
   (`launcher/res/launcher.rc`, `ui.cpp`) einbinden. Ohne Kachel zeigt der Launcher den Anfangsbuchstaben in `color`.
4. Muss etwas kompiliert werden, gehört `apps/<id>/build.sh` in `tools/build-all.sh`.
5. Neuen Tag setzen (siehe [entwicklung.md](entwicklung.md)) – der Launcher zeigt das Programm danach automatisch an.
