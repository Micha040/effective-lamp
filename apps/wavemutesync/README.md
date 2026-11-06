# WaveMuteSync – Wave:3-Mute → Discord & Teams

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [WaveMuteSync.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/WaveMuteSync.zip) herunterladen.

Tippst du oben aufs **Elgato Wave:3**, zeigt dich **Discord** automatisch als stumm an – und in einem laufenden **Microsoft-Teams**-Anruf auch Teams. Tippst du erneut, wirst du überall wieder aktiv.
Das Programm läuft unsichtbar im Hintergrund und startet bei jeder Windows-Anmeldung von selbst. Du musst nie etwas starten.

```
Wave:3 (Antippen) ──► Wave Link 3 ──► WaveMuteSync ──┬──► Discord  (lokale RPC-Schnittstelle)
                                                     └──► Teams    (Mikrofon-Button im Anruf)
```

## Wie es funktioniert (kurz)

- **Woher kommt der Mute-Status?** Das Wave:3 schaltet beim Antippen intern stumm – Windows selbst bekommt davon nichts mit. **Wave Link 3** kennt den Zustand aber und stellt ihn lokal bereit (dieselbe Schnittstelle nutzt das offizielle Stream-Deck-Plugin). WaveMuteSync hört dort mit.
- **Discord:** über Discords lokale RPC-Schnittstelle. Das Tool setzt direkt „stumm“ bzw. „nicht stumm“ – kein simulierter Tastendruck, der aus dem Takt geraten kann.
- **Teams:** Microsoft hat die lokale Teams-Schnittstelle für Zusatzgeräte zum 30.06.2026 abgeschaltet. WaveMuteSync drückt deshalb den Mikrofon-Button in Teams über die Windows-Bedienungshilfen (UI Automation, wie ein Screenreader) – nur während eines Anrufs.

## Regeln – damit nichts Überraschendes passiert

| Situation | Was WaveMuteSync macht |
|---|---|
| Du tippst aufs Mikro → stumm | Discord und Teams (im Anruf) werden stumm geschaltet |
| Du tippst erneut → aktiv | Discord und Teams werden wieder aktiv |
| PC-Start, Discord-Neustart oder du trittst einem Teams-Anruf bei | Nur wenn das Mikro gerade stumm ist, wird Discord/Teams stumm geschaltet. **Es wird nie automatisch entstummt.** |
| Discord ist taub geschaltet (Kopfhörer-Symbol) | Beim Entstummen fasst WaveMuteSync Discord nicht an |
| Du schaltest in Discord/Teams selbst stumm oder laut | Wird respektiert, kein Gegensteuern |

## Voraussetzungen

- Windows 10 oder 11 (PowerShell 5.1 und .NET Framework 4.8 sind dort schon dabei)
- **Elgato Wave Link 3** – muss laufen (tut es normalerweise automatisch im Hintergrund)
- **Discord-Desktop-App** (Discord im Browser geht nicht)
- **Microsoft Teams (neu)** – optional
- Keine Administratorrechte nötig

## Installation

1. ZIP-Datei **entpacken** (Rechtsklick → „Alle extrahieren“).
2. Im entpackten Ordner **`Setup.cmd` doppelklicken** → **[1] Installieren**.
   Warnt Windows vor einem „unbekannten Herausgeber“: auf „Ausführen“ klicken – der komplette Quellcode liegt lesbar im Ordner und wird erst auf deinem PC zur .exe kompiliert.
3. Das Setup fragt, ob du **Discord einrichten** willst, und führt dich durch (einmalig, ca. 3 Minuten):
   1. <https://discord.com/developers/applications> öffnen, mit deinem Discord-Konto anmelden.
   2. **New Application** → Name z. B. „Wave Mute Sync“ → Haken setzen → **Create**.
   3. Links auf **OAuth2**:
      - **Client ID** kopieren.
      - Bei **Client Secret** auf **Reset Secret** → Wert kopieren (wird nur einmal angezeigt).
      - Unter **Redirects** → **Add Redirect** → genau `http://localhost` eintragen → unten **Save Changes**.
   4. Client ID und Secret ins Setup-Fenster einfügen (Rechtsklick oder Strg+V). Das Setup prüft direkt, ob beides zusammenpasst.
4. **In Discord** erscheint einmalig ein Fenster „Wave Mute Sync möchte sich verbinden“ → **Autorisieren**.
   (Nicht zu sehen? Discord in den Vordergrund holen.)
5. **Testen:** oben aufs Wave:3 tippen → in Discord erscheint das durchgestrichene Mikro und du hörst Discords Stumm-Ton.

Die App im Developer Portal gehört nur dir und wird nirgends veröffentlicht. Discord erlaubt diese lokale Schnittstelle genau für den Besitzer der App – also dich.

## Setup-Menü (`Setup.cmd`)

| | |
|---|---|
| **[1] Installieren / Aktualisieren** | kompiliert neu, behält deine Einstellungen, richtet den Autostart ein |
| **[2] Discord einrichten / ändern** | Client ID/Secret neu eingeben |
| **[3] Log live anzeigen** | zeigt, was gerade passiert (beenden mit Strg+C) |
| **[4] Neu starten** | nach Änderungen an der `config.json` |
| **[5] Diagnose-Log an/aus** | ausführlichere Einträge zur Fehlersuche |
| **[6] Deinstallieren** | entfernt Programm, Autostart, Einstellungen und Discord-Anmeldung |

## Wo liegt was?

Alles unter `%LOCALAPPDATA%\WaveMuteSync\` (in den Explorer-Adressbalken einfügen):

- `WaveMuteSync.exe` – das Programm (auf deinem PC aus `WaveMuteSync.cs` kompiliert)
- `config.json` – Einstellungen
- `WaveMuteSync.log` – Protokoll (rotiert bei 1 MB)
- `discord-token.dat` – Discord-Anmeldung, mit Windows (DPAPI) verschlüsselt; nur dein Benutzerkonto kann sie lesen

Der Autostart ist eine Aufgabe namens **WaveMuteSync** in der Windows-Aufgabenplanung (Start bei Anmeldung, automatischer Neustart falls das Programm abstürzt).

## Fehlerbehebung

Erst ins Log schauen: `Setup.cmd` → **[3] Log live anzeigen**.

| Meldung im Log | Lösung |
|---|---|
| „Wave Link 3 ist nicht erreichbar“ | Wave Link starten. Es muss **Wave Link 3** sein – die alte Version hat eine andere Schnittstelle. |
| „Kein Wave-Mikrofon mit Mute-Status gefunden“ | Wave:3 anstecken. Bei mehreren Mikros in der `config.json` `waveLink.deviceNameFilter` setzen (z. B. `"Wave:3"`). |
| „Discord läuft nicht“ | Discord-Desktop-App starten – WaveMuteSync verbindet sich dann automatisch. |
| „Discord lehnt Client-ID/Client-Secret ab“ | Im Portal ein neues Secret erzeugen → Setup → **[2]**. |
| „… Redirects genau http://localhost eingetragen?“ | Im Portal unter OAuth2 → Redirects `http://localhost` eintragen, **Save Changes**, dann **[4] Neu starten**. |
| „Discord-Autorisierung abgelehnt / nicht bestätigt“ | **[4] Neu starten** und in Discord auf „Autorisieren“ klicken. |
| „Teams: Beschriftung des Mikrofon-Buttons nicht erkannt: "…"“ | Teams nutzt eine unbekannte Beschriftung (z. B. andere Sprache). Muster in der `config.json` ergänzen (siehe unten) und **[4] Neu starten**. |
| Teams reagiert nicht, keine Meldung | **[5] Diagnose-Log an**, in einem Anruf aufs Mikro tippen, Log ansehen. |

Gut zu wissen: Discords Stumm-Ton beim Umschalten kannst du in Discord unter *Einstellungen → Benachrichtigungen → Sounds* abschalten.

## Einstellungen (`config.json`)

Nach Änderungen: `Setup.cmd` → **[4] Neu starten**.

```json
{
  "discord":  { "enabled": true, "clientId": "123…", "clientSecret": "…" },
  "teams":    { "enabled": true },
  "waveLink": { "deviceNameFilter": "" },
  "verboseLog": false
}
```

| Feld | Bedeutung |
|---|---|
| `discord.enabled` / `teams.enabled` | Ziel an- oder ausschalten |
| `waveLink.deviceNameFilter` | leer = erstes Wave-Gerät; sonst Teil des Gerätenamens aus Wave Link |
| `verboseLog` | ausführliches Diagnose-Log |
| `teams.mutedNamePattern` *(optional)* | Regex: Beschriftung des Teams-Mikro-Buttons, wenn du **stumm** bist. Standard: `(?i)unmute\|aufheben\|einschalten\|turn on` |
| `teams.liveNamePattern` *(optional)* | Regex: Beschriftung, wenn du **aktiv** bist. Standard: `(?i)^\s*mute\|stummschalten\|stumm schalten\|ausschalten\|turn off` |

Zu den Teams-Mustern: Der Mikro-Button heißt z. B. „Unmute mic“ / „Mute mic“ (Englisch) bzw. „Stummschaltung aufheben“ / „Stummschalten“ (Deutsch). Erkennt WaveMuteSync die Beschriftung nicht, klickt es sicherheitshalber gar nicht und schreibt den gefundenen Text ins Log.

## Sicherheit & Datenschutz

- Läuft komplett lokal. Die einzige Internetverbindung geht an Discords Anmeldeserver (`discord.com`), beim ersten Verbinden und wenn die Anmeldung erneuert wird (etwa wöchentlich).
- Das Client-Secret steht in der `config.json` in deinem Benutzerprofil – nicht weitergeben. Die Discord-Anmeldung selbst ist verschlüsselt gespeichert.
- Teams wird über die offiziellen Windows-Bedienungshilfen gesteuert – nichts wird in Teams „gehackt“ oder verändert.

## Grenzen

- Ohne laufendes **Wave Link 3** gibt es keinen Mute-Status (Windows erfährt vom Antippen nichts).
- **Teams:** nur das neue Teams (Desktop) und nur während eines Anrufs; die Erkennung hängt an der Button-Beschriftung (Deutsch/Englisch sind vorbereitet).
- **Discord:** nur die Desktop-App. Discord lässt immer nur ein Programm gleichzeitig die Voice-Einstellungen ändern – ein paralleles Stream-Deck-Discord-Plugin kann sich damit beißen.
- Die Richtung ist bewusst **Mikro → Apps**. Stummschalten in Discord/Teams schaltet das Wave:3 nicht stumm.

## Deinstallieren

`Setup.cmd` → **[6] Deinstallieren**. Die Discord-App kannst du danach optional im Developer Portal löschen.
