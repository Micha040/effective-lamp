# DeskCats =^.^=

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [DeskCats.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/DeskCats.zip) herunterladen.

Fatos und Kosta wohnen auf deinem Desktop: als Pixel-Art-Katzen, die auf der Taskleiste und auf den Oberkanten
deiner Fenster sitzen, herumspazieren, springen, sich putzen und schlafen. Du kannst sie füttern, mit ihnen spielen,
sie streicheln und herumtragen.

## Installieren

1. ZIP komplett entpacken (z. B. nach „Downloads\DeskCats").
2. **`Setup.cmd`** doppelklicken. Kommt „Der Computer wurde durch Windows geschützt“: „Weitere Informationen“ →
   „Trotzdem ausführen“ (DeskCats ist nicht signiert, deshalb kennt Windows es nicht).
3. Windows fragt **einmal nach Administratorrechten**. Die braucht es nur für den Eintrag direkt im
   Rechtsklick-Menü des Desktops (wie bei SnowDesk). Ohne Adminrechte landet der Eintrag unter
   „Weitere Optionen anzeigen“.

Danach sitzen die beiden unten auf der Taskleiste. Noch einmal `Setup.cmd` starten (z. B. mit einer neueren Version)
überschreibt die alte Installation, die Einstellungen bleiben erhalten.

Installiert wird nach `%LOCALAPPDATA%\Programs\DeskCats`, das Protokoll liegt in `%LOCALAPPDATA%\DeskCats`,
die Einstellungen in der Registry unter `HKCU\Software\DeskCats`. DeskCats startet automatisch mit Windows
(abschaltbar).

## Bedienen

**Rechtsklick auf den Desktop → Katzen:**

| Eintrag | Wirkung |
|---|---|
| Füttern | stellt einen Napf unter den Mauszeiger (auf das Fenster bzw. die Taskleiste darunter) – beide kommen zum Fressen |
| Spielen | lässt ein Wollknäuel fallen – sie jagen es, springen darauf und schubsen es herum |
| Herrufen | beide kommen zur Maus (auch auf ein Fenster hinauf) |
| Schlafen schicken / Aufwecken | sie rollen sich zusammen bzw. wachen auf |
| Katzen ausblenden / anzeigen | ausgeblendet läuft nichts mehr (0 % CPU) |
| Einstellungen … | öffnet das Einstellungsfenster |

**Direkt an den Katzen:**

- **Anklicken** = streicheln (sie schließen genüsslich die Augen)
- **Ziehen** = hochheben und tragen; beim Loslassen fallen bzw. fliegen sie mit Schwung und landen auf den Pfoten
- **Rechtsklick** = Menü (streicheln, füttern, spielen, schlafen, Einstellungen, ausblenden, beenden)
- Napf und Knäuel lassen sich ebenfalls ziehen und werfen; Rechtsklick darauf: wegräumen bzw. nachfüllen

**Einstellungen:** welche Katzen, Größe (Klein bis Sehr groß, immer in ganzen Pixel-Schritten, damit die
Pixel-Art scharf bleibt), Lebhaftigkeit (Ruhig / Normal / Verspielt), ob sie auf Fenster klettern, auf der
Taskleiste sitzen und auf allen Monitoren unterwegs sein dürfen, die Spieleliste und der Autostart.

## Was die beiden so machen

- **Fatos** (rote Scottish Fold) ist gemütlich: liegt gern als Brotlaib herum, schläft viel und lange.
  **Kosta** (Smoking-Kater) ist neugierig: springt höher, ist öfter unterwegs, geht gern zu Fatos.
- Sie sitzen auf **Fensteroberkanten** und der **Taskleiste**, springen von Fenster zu Fenster und wieder herunter.
- **Echte Reihenfolge:** Eine Katze auf einem Fenster gehört zu diesem Fenster. Liegt ein anderes Fenster davor,
  ist sie dahinter verdeckt; holst du ihr Fenster nach vorne, kommt sie mit.
- **Fenster verschieben:** Wer darauf sitzt, fährt mit. **Minimieren, maximieren oder schließen:** Sie fallen
  herunter und landen auf den Pfoten.
- Ist die Maus in der Nähe, schauen sie hin. Manchmal gähnen, strecken oder putzen sie sich.

## Mehrere Monitore

- Jeder Monitor hat seinen eigenen Boden (seine Taskleiste bzw. die Unterkante). Die Katzen springen über Fenster
  und Kanten zwischen den Monitoren hin und her. Liegen die Unterkanten zu weit auseinander zum Springen, gehen sie
  am Bildschirmrand hinüber – so bleibt keine Katze auf einem Monitor „gefangen“.
- Die Größe richtet sich nach der Windows-Skalierung des jeweiligen Monitors – auf einem 150-%-Monitor sind sie
  etwa so groß wie auf einem 100-%-Monitor, nur mit feineren Bildschirmpixeln.
- Läuft auf einem Monitor ein **Vollbild-Programm** (Spiel, Video), verschwinden die Katzen dort; auf den anderen
  Monitoren bleiben sie.
- „Auf allen Monitoren“ ausschalten = nur der Hauptmonitor.

## Leistung

- Kein Grafikkarten-Aufwand: Jede Katze ist ein kleines, durchsichtiges Fenster, das Windows selbst darstellt.
- Gezeichnet wird nur, wenn sich am Bild etwas ändert, im Pixel-Art-Takt von 12 Bildern pro Sekunde. Sitzen,
  Liegen und Schlafen brauchen nur ein paar Bilder pro Sekunde (Blinzeln, Atmen, Schwanz), schlafend fast nichts.
  Jede Pose wird einmal berechnet und danach aus dem Speicher geholt.
- **Komplett pausiert**, wenn ein Spiel aus der Liste läuft (auch im Fenstermodus), bei Vollbild, im
  Präsentationsmodus und bei gesperrtem oder ausgeschaltetem Bildschirm.
- Selbst nachmessen: `DeskCats.exe --selftest 60` läuft eine Minute und schreibt CPU-Zeit und Zähler ins Protokoll
  (eine schon laufende Instanz vorher beenden).

## Spiele & Anti-Cheat (CS2, FACEIT …)

DeskCats greift in keine anderen Programme ein: keine DLL in fremden Prozessen, kein Auslesen von Spielspeicher,
keine Eingaben (keine Maus- oder Tastatur-Hooks, keine simulierten Klicks). Fenster beobachtet es nur über die
offiziellen Windows-Ereignisse (wie Barrierefreiheits-Tools) und verschiebt ausschließlich seine eigenen Fenster.
Spiele erkennt es nur am Namen in der Prozessliste (wie der Task-Manager), ohne sie zu öffnen. Läuft ein Spiel aus
der Liste, sind die Katzen weg und DeskCats steht komplett still. Das sollte für Anti-Cheat-Programme unauffällig
sein. Garantieren können das aber nur deren Hersteller.

Die Liste enthält z. B. `cs2.exe`, Valorant, Fortnite, Apex, League of Legends. Eigene Spiele trägst du im
Einstellungsfenster ein (Name der .exe, steht im Task-Manager unter „Details“; mehrere mit `;` trennen).

## Probleme?

- **„Smart App Control hat … blockiert“:** Smart App Control lässt nur signierte oder bei Microsoft bekannte
  Programme zu, DeskCats ist nicht signiert. Ausweg: Windows-Sicherheit → App- & Browsersteuerung →
  Smart App Control → Aus.
- **Eintrag fehlt im Rechtsklick-Menü:** `Setup.cmd` noch einmal starten und die Adminrechte erlauben, oder
  „Weitere Optionen anzeigen“ benutzen.
- **Katzen sind weg:** Rechtsklick auf den Desktop → Katzen → „Katzen anzeigen“. Läuft ein Spiel aus der Liste
  oder ein Vollbild-Programm, kommen sie danach von selbst wieder.
- **Protokoll:** `%LOCALAPPDATA%\DeskCats\DeskCats.log`

## Entfernen

**`Uninstall.cmd`** doppelklicken, oder Einstellungen → Apps → Installierte Apps → DeskCats → Deinstallieren.
Für das Kontextmenü-Paket fragt Windows evtl. nach Adminrechten.

## Befehlszeile

```
DeskCats.exe --cmd feed | play | call | sleep | wake | show | hide | toggle | settings | exit
DeskCats.exe --selftest <Sekunden>
```

## Versionen

- **1.0.1** – Das Setup lässt die Autostart-Einträge anderer Programme in Ruhe (1.0.0 hat beim Installieren alle
  Autostarts des Benutzers gelöscht) und behält die Einstellungen bei einem Update. Katzen können nicht mehr ins
  Bodenlose fallen (über der Taskleiste fallen gelassen, über den Rand geworfen, Monitor abgezogen). Ausgeblendet oder
  pausiert arbeitet DeskCats wirklich gar nicht mehr. Vollbild wird je Monitor erkannt. Kein Gerangel mehr mit
  Programmen, die sich ständig nach vorne drängen. Das Einstellungsfenster passt sich beim Wechsel auf einen Monitor
  mit anderer Skalierung an.
- **1.0.0** – erste Version.
