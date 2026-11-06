# TriggerSwitch – Trigger-Belegung per Hotkey (Razer Basilisk V3 Pro)

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [TriggerSwitch.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/TriggerSwitch.zip) herunterladen.

Ein Druck auf **Strg+Alt+F11** schaltet den Multifunktions-Trigger deiner Basilisk V3 Pro um:
**Leertaste-Turbo (20/s) ⇄ Rechtsklick-Turbo (20/s)**. In der Bildschirmecke erscheint für ca. 3 Sekunden ein kleines Overlay mit dem Modus und dem Akkustand der Maus.
TriggerSwitch läuft unsichtbar im Hintergrund und startet bei jeder Windows-Anmeldung von selbst.

```
Hotkey ──► TriggerSwitch ──► Maus: Normalmodus + Trigger-Belegung ──► Overlay (unten rechts)
                  ▲
                  └── Wächter: prüft alle 2,5 s und stellt wieder her, falls etwas zurückgesetzt wurde
```

## Wie es funktioniert

- Synapse betreibt die Maus normalerweise im **Treibermodus**: Die Maus meldet nur „Trigger gedrückt“, den Turbo erzeugt Synapse per Software. TriggerSwitch schaltet die Maus deshalb in den **Normalmodus**. Dann führt sie ihre Belegung selbst aus, genau wie im Modustest.
- Danach schreibt es die Trigger-Belegung ins **Arbeitsprofil** der Maus, mit demselben Befehl, den Synapse benutzt. Dein gespeichertes Synapse-Profil wird nie verändert (das ist im Code gesperrt).
- Ein **Wächter** prüft alle 2,5 Sekunden, ob noch alles stimmt, und stellt den gewählten Modus sofort wieder her. Das deckt ab, wenn Synapse die Maus übernimmt, sie aus dem Standby kommt, aus- und eingeschaltet wird oder der Dongle umgesteckt wird.
- **Anti-Cheat:** kein Tastatur-Hook (der Hotkey läuft über die Windows-Funktion `RegisterHotKey`, wie bei Discord oder OBS), keine simulierten Eingaben, nichts wird in ein Spiel injiziert. Den Turbo erzeugt die Maus selbst. Ob Turbo-Belegungen in einem Spiel oder einer Liga erlaubt sind, legen deren Regeln fest. Valve und FACEIT sehen Makros und Turbo kritisch, daran ändert das Tool nichts.

## Installation

1. ZIP-Datei **entpacken** (Rechtsklick → „Alle extrahieren“).
2. Im entpackten Ordner **`Setup.cmd` doppelklicken** → **[1] Installieren**.
   Warnt Windows vor einem „unbekannten Herausgeber“, auf „Ausführen“ klicken. Der komplette Quellcode liegt lesbar im Ordner und wird erst auf deinem PC zur .exe kompiliert.
3. Hotkey bestätigen oder direkt die gewünschte Kombination drücken.
4. **Testen:** Hotkey drücken. Unten rechts erscheint das Overlay und der Trigger macht den anderen Turbo.

Keine Administratorrechte nötig. Voraussetzung: Windows 10 oder 11.

## Benutzung

- Jeder Druck auf den Hotkey wählt die nächste Belegung. Der zuletzt gewählte Modus gilt auch nach einem Neustart.
- **Gelber Hinweis im Overlay** („Maus schläft – wird beim Aufwachen gesetzt“): Der Wechsel ist gespeichert und wird ausgeführt, sobald die Maus wieder antwortet. Du musst nichts tun.
- **Akku im Overlay:** Rechts im Overlay steht der Ladestand der Maus mit Batteriesymbol, z. B. „82 %“. Bei 20 % und weniger wird er gelb, ab 10 % rot, beim Laden grün mit Blitz. TriggerSwitch fragt ihn etwa einmal pro Minute bei der Maus ab, nur lesend. Schläft die Maus oder liefert sie keinen Wert, fehlt die Anzeige einfach.
- **Overlay und Vollbild:** Das Overlay bekommt nie den Fokus, Klicks gehen durch, du fliegst nicht aus dem Spiel. Sichtbar ist es in randlosem Vollbild („Vollbild im Fenster“) und im normalen Windows-10/11-Vollbild. Im echten exklusiven Vollbild zeigt Windows keine fremden Fenster über dem Spiel. Dann siehst du das Overlay nicht, umgeschaltet wird trotzdem.

## Gut zu wissen

- **Solange TriggerSwitch läuft, führt die Maus alle ihre Belegungen selbst aus.** Deine aktuellen Belegungen sind alle in der Maus gespeichert, deshalb merkst du davon nichts. Funktionen, die es nur in Synapse gibt (z. B. Makros oder Programmstarts), laufen in dieser Zeit nicht. Brauchst du die einmal: **[6] Pausieren**.
- Stellst du in Synapse den Trigger um, setzt TriggerSwitch ihn wieder auf den gewählten Modus. Alle anderen Tasten bleiben so, wie sie in Synapse eingestellt sind.
- Hotkey ungünstig? Im Setup **[2] Hotkey ändern**. Gut geeignet sind Strg+Alt+F-Tasten, **Pause** oder **Rollen**. Strg+Alt+Buchstabe ist auf deutschen Tastaturen gleich AltGr (z. B. @ oder €). Das Setup warnt dich davor.

## Setup-Menü (`Setup.cmd`)

| | |
|---|---|
| **[1] Installieren / Aktualisieren** | kompiliert, behält deine Einstellungen, richtet den Autostart ein |
| **[2] Hotkey ändern** | neue Kombination einfach drücken (oder eintippen, z. B. `Rollen`) |
| **[3] Overlay ansehen / einstellen** | Vorschau (Akku mit Beispielwerten), an/aus, Ecke, Anzeigedauer |
| **[4] Log anzeigen** | Protokoll live in einem eigenen Fenster |
| **[5] Neu starten** | z. B. nach Änderungen an der `config.json` |
| **[6] Pausieren** | gibt die Maus an Synapse zurück und beendet TriggerSwitch bis zur nächsten Anmeldung |
| **[7] Diagnose-Log an/aus** | protokolliert jede Prüfung (für die Fehlersuche) |
| **[8] Deinstallieren** | gibt die Maus an Synapse zurück und entfernt alles |

## Einstellungen (`config.json`)

Liegt unter `%LOCALAPPDATA%\TriggerSwitch\`. Nach Änderungen: **[5] Neu starten**.

```json
{
  "hotkey": "Ctrl+Alt+F11",
  "overlay": { "enabled": true, "seconds": 3, "position": "bottomRight" },
  "modes": [
    { "name": "Leertaste",   "fn": 13, "size": 4, "data": "00 2C 00 32 00", "color": "#4ADE80" },
    { "name": "Rechtsklick", "fn": 14, "size": 3, "data": "02 00 32 00 00", "color": "#60A5FA" }
  ],
  "watchdogMs": 2500,
  "verboseLog": false
}
```

| Feld | Bedeutung |
|---|---|
| `hotkey` | z. B. `Ctrl+Alt+F11`, `Strg+Umschalt+F9`, `Pause`, `ScrollLock` |
| `overlay.position` | `bottomRight`, `bottomLeft`, `topRight`, `topLeft`, `bottomCenter`, `topCenter` |
| `overlay.seconds` | Anzeigedauer, 1 bis 10 |
| `modes` | Belegungen, zwischen denen der Hotkey der Reihe nach umschaltet (auch mehr als zwei) |
| `watchdogMs` | Prüftakt des Wächters in Millisekunden (mindestens 1000) |

**Eigene Belegungen** (`fn`/`size`/`data` sind die Rohwerte, die auch Synapse in die Maus schreibt; Werte in Hex):

| Belegung | `fn` | `size` | `data` |
|---|---|---|---|
| Turbo-Taste | 13 | 4 | `MM KK II II 00`: Zusatztasten, Tastencode, Intervall in ms (`00 32` = 50 ms = 20/s, `00 19` = 25 ms = 40/s) |
| Turbo-Maustaste | 14 | 3 | `BB II II 00 00`: Taste (`01` links, `02` rechts, `03` Mitte), Intervall wie oben |
| Taste | 2 | 2 | `MM KK 00 00 00`, z. B. `00 08 00 00 00` = E |
| Maustaste | 1 | 1 | `BB 00 00 00 00` |

Tastencodes (USB-HID): A = `04` … Z = `1D`, 1 = `1E` … 0 = `27`, Leertaste = `2C`, F1 = `3A`. Zusatztasten: Strg `01`, Umschalt `02`, Alt `04`.
Ein ungültiger Eintrag wird übersprungen und im Log gemeldet.

## Wo liegt was?

Alles unter `%LOCALAPPDATA%\TriggerSwitch\` (in die Explorer-Adresszeile einfügen):

- `TriggerSwitch.exe`: das Programm (auf deinem PC aus `TriggerSwitch.cs` kompiliert)
- `config.json`: Einstellungen
- `state.json`: zuletzt gewählter Modus
- `TriggerSwitch.log`: Protokoll (rotiert bei 1 MB)

Der Autostart ist eine Aufgabe namens **TriggerSwitch** in der Windows-Aufgabenplanung.

## Fehlerbehebung

Erst ins Log schauen: **[4] Log anzeigen**.

| Meldung | Lösung |
|---|---|
| „Hotkey … ist schon von einem anderen Programm belegt“ (auch als Overlay) | **[2] Hotkey ändern** |
| „Keine Basilisk V3 Pro gefunden“ | Dongle einstecken. TriggerSwitch wartet und verbindet sich von selbst. |
| „Maus antwortet nicht (schläft oder ist aus)“ | Maus bewegen bzw. einschalten. Der Modus wird automatisch nachgeholt. |
| „… in 10 Minuten …× zurückgestellt“ | Irgendetwas (vermutlich Synapse) stellt die Maus ständig um. TriggerSwitch stellt jedes Mal wieder her. Wenn es stört: Synapse neu starten oder Log-Ausschnitt schicken. |
| Overlay im Spiel unsichtbar | Spiel läuft in exklusivem Vollbild. Auf „Vollbild im Fenster“ bzw. randlos stellen. |
| Setup meldet „Smart App Control ist eingeschaltet“ | Smart App Control blockiert selbst kompilierte Programme. Ohne Ausschalten (Windows-Sicherheit → App- und Browsersteuerung) startet TriggerSwitch nicht. |

## Pausieren & Deinstallieren

- **[6] Pausieren:** Trigger wieder wie in Synapse, Synapse übernimmt (Treibermodus), TriggerSwitch ist bis zur nächsten Anmeldung aus.
- **[8] Deinstallieren:** gibt die Maus ebenso zurück und entfernt Programm, Autostart und Einstellungen.
- Antwortet die Maus dabei nicht (schläft oder ist aus): bewegen bzw. einschalten und nochmal versuchen.
