# FragDesk ⌖

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [FragDesk.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/FragDesk.zip) herunterladen.

**Deine CS2-Stats und Highlights – komplett auf deinem PC.** FragDesk liest deine Demos (Premier, Wettkampf, Wingman,
FACEIT) und zeigt daraus über 150 Kennzahlen, deinen Premier-Verlauf als Graph, jedes Match bis zur einzelnen Runde –
und die Momente, die sich lohnen: Multikills, gewonnene Clutches, Noscopes, Wallbangs, Kills durch Smoke und mehr.
Welche Demo ausgewertet und welcher Moment als Video aufgenommen wird, entscheidest du.

<p align="center"><img src="../../docs/media/fragdesk-dashboard.png" width="760" alt="FragDesk: Übersicht mit Premier-Verlauf, Form und Kennzahlen"></p>

## Installieren

1. ZIP komplett entpacken (z. B. nach „Downloads\FragDesk“).
2. **`Setup.cmd`** doppelklicken. Kommt „Der Computer wurde durch Windows geschützt“: „Weitere Informationen“ →
   „Trotzdem ausführen“ (FragDesk ist nicht signiert, deshalb kennt Windows es nicht).
3. Fertig – das Setup braucht **keine Administratorrechte**, legt einen Startmenü-Eintrag an und bietet FragDesk bei
   `.dem`-Dateien unter „Öffnen mit“ an (deine Standard-App für Demos bleibt, wie sie ist).

Installiert wird nach `%LOCALAPPDATA%\Programs\FragDesk`. **Kein Autostart:** FragDesk läuft nur, solange sein Fenster
offen ist. Noch einmal `Setup.cmd` starten (z. B. mit einer neueren Version) aktualisiert die Installation, deine
Auswertungen und Einstellungen bleiben erhalten.

Voraussetzungen: Windows 10 oder 11 (64 Bit). Für Videos zusätzlich Steam, CS2 und die kostenlosen
**CS2 Workshop Tools** (siehe [Videos aufnehmen](#videos-aufnehmen)); den Spielton nimmt FragDesk ab Windows 10
Version 2004 auf.

## Woher kommen die Demos?

FragDesk schaut in diese Ordner und listet neue Demos unter **Matches › Neue Demos** auf (es öffnet dabei nur Dateien,
nie andere Programme):

| Quelle | So kommst du an die Demo |
|---|---|
| **Premier, Wettkampf, Wingman** | in CS2 unter **Deine Matches** beim Match auf „Herunterladen“ – CS2 legt die Demo in `game\csgo\replays` ab, FragDesk findet sie dort |
| **FACEIT** | auf der Match-Seite „Demo ansehen/herunterladen“ – die Datei (`.dem.zst` oder `.dem.gz`) landet im Downloads-Ordner, FragDesk entpackt sie selbst |
| **alles andere** | weitere Ordner in den Einstellungen hinzufügen, Demos ins Fenster ziehen oder „Demos hinzufügen“ |

**Auswerten** startest du pro Demo selbst (oder „Alle auswerten“). Eine Demo braucht dafür meist 2–10 Sekunden.
Wer es automatisch mag: Einstellungen › „Neue Demos sofort auswerten“.

## Was du siehst

- **Übersicht:** Premier-Rating mit Verlauf (Farben wie im Spiel), Form der letzten 10 Matches, die wichtigsten
  Kennzahlen mit T- und CT-Seite, letzte Matches, beste Waffen und Maps, deine besten Momente.
  Filter für Modus (Premier, FACEIT, Wettkampf …), Zeitraum (letzte 10 / 30 / alle) und Map gelten für alles.
- **Matches:** alle ausgewerteten Matches als sortierbare Tabelle. Ein Klick öffnet das Match mit sieben Bereichen:
  *Übersicht* (Rundenstreifen mit Kills und Clutches) · *Scoreboard* (beide Teams, Rating, ADR, KAST, Eröffnungen,
  Clutches, Utility, MVPs, Premier vorher/nachher) · *Runden* (Kauf, Schaden, Tod, Eröffnung) · *Waffen* (Kills,
  Treffer, Kopfquote, Körperzonen, „gestorben durch“) · *Duelle* (gegen jeden Gegner, kompletter Killfeed) ·
  *Utility* · *Momente*. Die Werte jedes anderen Spielers lassen sich ebenfalls anzeigen.
- **Statistiken:** über 150 Kennzahlen in elf Bereichen – Allgemein, Waffen, Maps, Eröffnung & Trades, Clutches
  (1v1 bis 1v5), Utility (Flashes, geblendete Gegner, Blendzeit, Flash-Assists …), Zielen (Treffsicherheit, erster
  Schuss, Spray, in Bewegung, Trefferzonen), Besondere Kills (Wallbang, Noscope, durch Smoke, geblendet, im Sprung …),
  Schaden & Zeit, Economy & Bombe und **Verlauf** (Premier, Rating, ADR, K/D, KAST, HS-Quote als Graphen).
  Jede Kennzahl hat einen Tooltip mit Erklärung.
- **Momente:** alle erkannten Highlights, nach Wert sortiert, mit **In CS2 ansehen** und **Aufnehmen**;
  dazu die Liste deiner Videos.

<p align="center"><img src="../../docs/media/fragdesk-stats.png" width="760" alt="FragDesk: Statistiken, Verlauf als Graphen"></p>

### Wie Momente erkannt werden

Jede Runde jedes Spielers bekommt Punkte: für die Zahl der Kills (Doppelkill bis Ass), für gewonnene Clutches
(je mehr Gegner, desto mehr) und für Besonderes – Noscope, Wallbang, durch Smoke, geblendet, im Sprung, Messer,
Zeus, weiter Schuss, zwei auf einen Streich, alles Kopfschüsse, schnelle Folge. Ab einer Schwelle ist es ein Moment;
der Ausschnitt beginnt 4 Sekunden vor dem ersten Kill und endet 2 Sekunden nach dem letzten (bei Clutches die ganze
entscheidende Phase).

## Videos aufnehmen

**Einmalig vorher:** In Steam Rechtsklick auf **Counter-Strike 2 › Eigenschaften › DLC** und bei
**„Counter-Strike 2 Workshop Tools“** „Installieren“ anhaken (kostenlos, von Valve). Warum: FragDesk steuert CS2 über
dessen Konsolen-Schnittstelle (`-netconport`), und die öffnet CS2 unter Windows nur im Workshop-Tools-Modus – ein
seit Jahren offener Fehler im Spiel ([ValveSoftware/csgo-osx-linux#3603](https://github.com/ValveSoftware/csgo-osx-linux/issues/3603)).
Fehlen die Tools, zeigt FragDesk unter **Momente** und in den Einstellungen einen Hinweis.

Bei einem Moment auf **Aufnehmen** (oder mehrere ankreuzen und „Ausgewählte aufnehmen“):

1. FragDesk startet CS2 mit der Demo, springt zum Moment und schaltet auf den Spieler – so, als würdest du selbst die
   Demo ansehen.
2. Es nimmt das CS2-Fenster **von außen** auf, wie OBS: Bild per Bildschirmaufnahme, Ton nur von CS2 (kein Discord,
   keine Musik), gespeichert als MP4 (H.264/AAC, mit dem Hardware-Encoder deiner Grafikkarte, wenn vorhanden).
3. Danach beendet es CS2 wieder. **Während der Aufnahme den PC bitte nicht benutzen** – aufgenommen wird, was auf
   dem Bildschirm zu sehen ist.

Die Videos liegen in **`Videos\FragDesk\<Datum> <Map> <Ergebnis>\`**, z. B. `2026-11-06 Mirage 13-11\R14 3 Kills.mp4`.
Einstellbar sind Ordner, Auflösung (720p / 1080p / 1440p), 30 oder 60 fps, Qualität, „nur Killfeed“ (übriges HUD
aus), Spielton und Sprachchat der Demo.

Für Aufnahmen startet CS2 mit **eigenem Einstellungsordner** (`%LOCALAPPDATA%\FragDesk\cs2`): Fenstergröße,
ausgeblendetes HUD und Co. landen nie in deinen echten CS2-Einstellungen. Zur Sicherheit merkt sich FragDesk vorher
deine CS2-Einstellungsdateien und stellt sie danach wieder her, falls sich doch etwas geändert hat. Passt die
gewählte Auflösung nicht als Fenster auf den Monitor, nimmt FragDesk ein randloses Fenster in Monitorgröße.
Wird CS2 während der Aufnahme geschlossen, minimiert oder verschoben, bricht FragDesk ab, statt den Desktop
aufzunehmen.

**Manche Demos kann CS2 selbst nicht abspielen** – vor allem von Community-Servern wie FACEIT: CS2 stürzt dann
direkt nach dem Laden mit `FATAL ERROR: CopyNewEntity: invalid class index … out of range 0` ab
([ValveSoftware/csgo-osx-linux#4543](https://github.com/ValveSoftware/csgo-osx-linux/issues/4543)). FragDesk erkennt
die Fehlermeldung, verwirft den angefangenen Clip, beendet CS2 und macht mit der nächsten Demo weiter; unter
**Momente** steht dann, was passiert ist. Die Statistiken dieser Demo sind davon nicht betroffen.

**In CS2 ansehen** macht dasselbe ohne Aufnahme und mit deinen normalen Einstellungen; CS2 bleibt danach offen.
Bevor du damit online spielst, starte CS2 einmal normal neu – dieses CS2 läuft noch im Workshop-Tools-Modus mit der
Konsolen-Schnittstelle, über die FragDesk es gesteuert hat.

> **Hinweis:** Läuft etwas nicht rund, steht im Protokoll `%LOCALAPPDATA%\FragDesk\fragdesk.log`
> (Einstellungen › „Datenordner öffnen“), was FragDesk an CS2 geschickt hat und was CS2 zuletzt ausgegeben hat.

## Anti-Cheat und Sicherheit

- FragDesk **liest nur Demo-Dateien**. Es greift nie in ein laufendes Spiel ein: keine Injektion, keine Hooks,
  keine simulierten Eingaben, kein Lesen fremden Speichers.
- CS2 startet es **nur zum Ansehen oder Aufnehmen** – und nur, wenn CS2 gerade nicht läuft. Dabei nutzt es
  ausschließlich offizielle Startoptionen von Valve (`-tools` für den Workshop-Tools-Modus, `+playdemo` und die
  Konsole über `-netconport`), **kein Plugin, keine geänderten Spieldateien**. CS2 spielt dabei nur die Demo auf
  deinem PC ab und verbindet sich mit keinem Server. Für Matches startest du CS2 danach wie gewohnt neu.
- Alles bleibt auf deinem PC: Es wird nichts hochgeladen, FragDesk braucht kein Internet.

## Ressourcen

- Im Leerlauf tut FragDesk nichts außer auf neue Dateien in den Demo-Ordnern zu warten (Windows meldet sich dann).
- Ausgewertet wird in einem eigenen Prozess (`fdanalyze.exe`) mit niedriger Priorität: ein paar Sekunden lang
  ca. 100–150 MB Arbeitsspeicher, danach ist er wieder weg.
- Ausgewertete Matches liegen als kleine JSON-Dateien (50–200 KB pro Match) in `%LOCALAPPDATA%\FragDesk\matches`.
  Die Demo selbst brauchst du danach nur noch zum Ansehen und Aufnehmen.

## Genauigkeit

Die Zahlen kommen aus [cs-demo-analyzer](https://github.com/akiver/cs-demo-analyzer) (die Auswertung von
CS Demo Manager) plus eigenen Berechnungen. Mit öffentlichen Test-Demos (Valve, FACEIT, ESEA, HLTV) stimmen Kills,
Kopfschüsse, KAST und Multikills mit den Angaben der Anbieter überein; ADR liegt meist unter einem Punkt daneben
(die Anbieter zählen Schaden rund ums Rundenende unterschiedlich).
**Rating 2.0** ist eine Näherung an die HLTV-Formel (HLTV veröffentlicht sie nicht) – gut zum Vergleichen deiner
eigenen Matches, aber nicht identisch mit HLTV oder Leetify.

Das **Premier-Rating** steht in der Demo (vorher und nachher) – deshalb gibt es den Verlauf nur für Premier-Demos
von Valve. Demos aus CS:GO und POV-Demos kann FragDesk nicht lesen.

## Daten, Protokoll, Deinstallieren

| Was | Wo |
|---|---|
| Programm | `%LOCALAPPDATA%\Programs\FragDesk` |
| Auswertungen, Einstellungen, Protokoll | `%LOCALAPPDATA%\FragDesk` |
| Videos | `Videos\FragDesk` (änderbar) |

Entfernen: `Uninstall.cmd` aus dem ZIP oder **Einstellungen › Apps › Installierte Apps › FragDesk**. Auf Wunsch
löscht es auch die Auswertungen; deine Demos und Videos bleiben immer erhalten.

Befehlszeile: `FragDesk.exe <Demo> …` wertet Demos aus (auch für „Öffnen mit“), `--theme dark|light` erzwingt
das Farbschema.

## Lizenzen

FragDesk steht unter der MIT-Lizenz (wie ganz efficient lamp). `fdanalyze.exe` enthält
[cs-demo-analyzer](https://github.com/akiver/cs-demo-analyzer) von AkiVer und
[demoinfocs-golang](https://github.com/markus-wa/demoinfocs-golang) von markus-wa (beide MIT) sowie weitere
Open-Source-Bibliotheken – alle Lizenztexte stehen in `THIRD-PARTY-NOTICES.txt` im Programmordner.
