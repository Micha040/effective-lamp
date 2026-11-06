# SnowDesk ❄

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [SnowDesk.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/SnowDesk.zip) herunterladen.

Schnee fällt auf deinem Desktop-Hintergrund, bleibt oben auf deinen Fenstern liegen und fällt herunter,
sobald du ein Fenster bewegst, minimierst oder schließt.

**Neu in 1.1: Echtes Wetter.** Auf Wunsch zeigt SnowDesk das Wetter, das gerade draußen ist (Standard: Hamburg) –
Regen mit Tropfen auf den Fensterkanten, Schnee, Schneeregen, Nebel, Gewitter, Wind aus der echten Richtung, und
liegender Schnee taut bei Plusgraden.

## Installieren

1. ZIP komplett entpacken (z. B. nach „Downloads\SnowDesk").
2. **`Setup.cmd`** doppelklicken. Kommt „Der Computer wurde durch Windows geschützt“: „Weitere Informationen“ →
   „Trotzdem ausführen“ (SnowDesk ist nicht signiert, deshalb kennt Windows es nicht).
3. Windows fragt **einmal nach Administratorrechten**. Die braucht es nur für den Eintrag direkt im
   Rechtsklick-Menü des Desktops (Windows 11 lässt solche Einträge ohne Signatur nur so zu).
   Ohne Adminrechte landet der Eintrag unter „Weitere Optionen anzeigen“.
4. **Sichttest:** Das Setup zeigt 6 Sekunden lang den Desktop und fragt, ob Schnee zu sehen war.
   Wenn nicht, probiert es automatisch die anderen Varianten. War der Hintergrund schwarz, antworte mit **S**:
   Dann nimmt es direkt die sichere Variante „Vor Symbolen“ (siehe „Probleme?“).

Noch einmal `Setup.cmd` starten (z. B. mit einer neueren Version) überschreibt die alte Installation, die Einstellungen
bleiben erhalten.

Installiert wird nach `%LOCALAPPDATA%\Programs\SnowDesk`, Einstellungen und Protokoll liegen in
`%LOCALAPPDATA%\SnowDesk`. SnowDesk startet automatisch mit Windows (abschaltbar).

## Bedienen

Rechtsklick auf den Desktop → **Schnee**:

| Eintrag | Wirkung |
|---|---|
| Schnee einschalten / ausschalten | beim Ausschalten taut alles in ca. 2 Sekunden weg |
| Leicht / Mittel / Schneesturm | feste Stärke (das Häkchen zeigt die aktive) |
| Echtes Wetter (Ort) | Wetter wie draußen (siehe unten) |
| Alles abschütteln | aller Schnee fällt von den Fenstern |
| Einstellungen … | öffnet das Einstellungsfenster |

Im **Einstellungsfenster** wirkt jede Änderung sofort:

- **Schnee:** an/aus, Stärke, Menge, Wind (mit Böen)
- **Flocken:** Größe, Fallgeschwindigkeit, Anteil der Schneekristalle
- **Liegen bleiben:** maximale Höhe (Standard 32 px ≈ eine Titelleiste, 0 = nichts bleibt liegen),
  Zeit bis voll (Standard 6 Minuten bei „Mittel“), wie der Schnee herunterfällt (Mischung, Klumpen, Brösel, Sanft)
- **Darstellung:** Bilder pro Sekunde (30, 60 oder wie der Monitor), Schnee-Ebene, „Maus pustet Flocken weg“ (aus)
- **Pausieren:** Spieleliste (eine .exe pro Zeile)
- **System:** mit Windows starten

## Echtes Wetter

Rechtsklick auf den Desktop → **Schnee → Echtes Wetter**, oder im Einstellungsfenster unter „Echtes Wetter“.
Dort stellst du auch den **Ort** ein: Name eintippen, „Ort suchen“ (bei mehreren Treffern erscheint eine Auswahl).

- Die Daten kommen von **Open-Meteo** (kostenlos, ohne Konto). SnowDesk fragt **alle 15 Minuten** einmal nach – nur
  solange „Echtes Wetter“ an ist und SnowDesk nicht pausiert. Gesendet werden nur die Koordinaten des Orts.
- **Regen:** schräge Regenspuren (je nach Wind), kleine Spritzer auf den Kanten. Auf den Fensteroberkanten sammeln
  sich **Tropfen**, große rollen zur nächsten Ecke, hängen dort, werden schwerer und **tropfen ab** – auf das Fenster
  darunter oder auf die Taskleiste. Nach dem Regen trocknen sie langsam. Fenster bewegen: die Tropfen fallen herunter.
- **Nieselregen:** feine, langsame Tröpfchen.
- **Schnee:** wie gewohnt, die Menge richtet sich nach dem Schneefall. Bei Plusgraden ist es **nasser Schnee**, der
  nicht liegen bleibt. Um 0 °C gibt es **Schneeregen** (Regen und nasse Flocken).
- **Tauwetter:** Bei Plusgraden schmilzt liegender Schnee langsam (Regen und Sonne beschleunigen das), das
  Schmelzwasser läuft zu den Ecken und tropft.
- **Nebel:** ein heller, ziehender Dunst über dem Hintergrund, unten dichter.
- **Gewitter:** ab und zu erhellt ein Blitz den Hintergrund.
- **Wind:** Stärke und Richtung wie draußen (Wind aus Westen weht nach rechts).
- Ist es trocken, fällt nichts – das ist dann eben das echte Wetter. Was gerade los ist, steht im Einstellungsfenster
  unter „Echtes Wetter“ (z. B. „Hamburg, Deutschland: 6 °C · Regen · Wind 20 km/h aus West · Stand 17:45“).
- Ohne Internet versucht SnowDesk es nach 1, 2, 5, 10 und dann alle 15 Minuten erneut.
- Flockengröße, Fallgeschwindigkeit, Liegenbleiben usw. gelten weiter. Eine feste Stufe (Leicht/Mittel/Schneesturm)
  oder die Regler „Menge“/„Wind“ schalten zurück auf festen Schnee.

## Wie sich der Schnee verhält

- Er fällt **hinter den Desktop-Symbolen** und ist nur auf dem freien Hintergrund zu sehen.
- Er bleibt auf **allen normalen Fenstern und Dialogen** liegen, nicht auf Overlays, Menüs oder
  „Immer im Vordergrund“-Fenstern. Liegen Fenster übereinander, fängt das höhere den Schnee
  (inklusive Windschatten).
- Die Schneedecke wächst gleichmäßig, mit leichten Wellen und runden Enden. Landende Flocken bleiben kurz liegen
  und schmelzen dann ein. An vollen Kanten rieselt ab und zu ein Bröckchen herunter.
- **Fenster verschieben, minimieren, maximieren oder schließen:** Der Schnee fällt herunter.
  **Größe ändern:** Nur der Teil ohne Halt fällt herunter. Ein Klick ins Fenster ändert nichts.
- Auf der **Taskleiste** sammelt sich der Schnee bis zur maximalen Höhe und bleibt dort liegen.
- Alle Monitore schneien. Auflösung, Skalierung und Bildwiederholrate werden automatisch übernommen.

## Leistung

- Es wird nur so oft gezeichnet, wie es nötig ist: volle Bildrate, solange etwas fällt; bei Nebel, rollenden
  Tropfen oder Tauwetter nur wenige Bilder pro Sekunde; steht alles still, gar keine.
- Gezeichnet wird mit der Grafikkarte (Direct3D 11 + DirectComposition). Die Rechenzeit pro Bild steht oben im
  Einstellungsfenster und liegt typischerweise deutlich unter 1 ms.
- **Komplett pausiert (0 % CPU/GPU)**, wenn ein Spiel aus der Liste läuft (auch im Fenstermodus), bei exklusivem
  Vollbild, im Präsentationsmodus, bei gesperrtem oder ausgeschaltetem Bildschirm.
- Ein **komplett verdeckter Monitor** (z. B. maximiertes Fenster oder Vollbild-Video) pausiert einzeln.
- Mit 30 Bildern/s halbiert sich der Aufwand noch einmal.

## Spiele & Anti-Cheat (CS2, FACEIT …)

SnowDesk greift in keine anderen Programme ein: keine DLL in fremden Prozessen, kein Auslesen von Spielspeicher,
keine Eingaben. Fenster beobachtet es nur über die offiziellen Windows-Ereignisse (wie Barrierefreiheits-Tools).
Spiele erkennt es nur am Namen in der Prozessliste (wie der Task-Manager), ohne sie zu öffnen. Läuft ein Spiel
aus der Liste, steht SnowDesk komplett still. Das sollte für Anti-Cheat-Programme unauffällig sein. Garantieren
können das aber nur deren Hersteller.

Die Liste enthält z. B. `cs2.exe`, Valorant, Fortnite, Apex, League of Legends. Eigene Spiele trägst du im
Einstellungsfenster unter „Pausieren“ ein (Name der .exe, steht im Task-Manager unter „Details“).

## Wallpaper Engine

SnowDesk läuft zusammen mit Wallpaper Engine. Der Schnee liegt über dem animierten Hintergrund und unter den Symbolen.

## Probleme?

- **Hintergrund wird schwarz:** Einstellungen → Darstellung → Schnee-Ebene → **„Vor Symbolen“**. Das ist ein
  eigenes, durchklickbares Fenster über dem Desktop und funktioniert immer; der Schnee liegt dann vor den Symbolen.
- **Kein Schnee zu sehen:** Schnee-Ebene „Alternative“ probieren, sonst „Vor Symbolen“.
- **„Smart App Control hat … blockiert“:** Smart App Control lässt nur signierte oder bei Microsoft bekannte Programme
  zu, SnowDesk ist nicht signiert. Ausweg: Windows-Sicherheit → App- & Browsersteuerung → Smart App Control → Aus.
- **Eintrag fehlt im Rechtsklick-Menü:** `Setup.cmd` noch einmal starten und die Adminrechte erlauben, oder
  „Weitere Optionen anzeigen“ benutzen.
- **Protokoll:** `%LOCALAPPDATA%\SnowDesk\SnowDesk.log`
- **Diagnose:** `SnowDesk.exe --diag` schreibt `%LOCALAPPDATA%\SnowDesk\diagnose.txt`
  (Monitore, Desktop-Ebenen, erkannte Fenster ohne Titel).

## Entfernen

**`Uninstall.cmd`** doppelklicken, oder Einstellungen → Apps → Installierte Apps → SnowDesk → Deinstallieren.
Für das Kontextmenü-Paket fragt Windows evtl. nach Adminrechten.

## Befehlszeile

```
SnowDesk.exe --cmd on | off | toggle | light | medium | storm | weather | shake | settings | exit
SnowDesk.exe --diag
```

## Versionen

- **1.1.1** – Das Setup lässt die Autostart-Einträge anderer Programme in Ruhe (bis 1.1.0 hat es beim Installieren
  alle Autostarts des Benutzers gelöscht) und behält gespeicherte Einstellungen.
- **1.1.0** – Echtes Wetter: Regen, Schneeregen, Nebel, Gewitter, Wind und Tauwetter passend zum Ort.
- **1.0.0** – erste Version.
