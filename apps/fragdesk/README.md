# FragDesk ⌖

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – am bequemsten über den [Launcher](https://github.com/Micha040/effective-lamp/releases/latest/download/EfficientLamp.exe) installieren und aktuell halten,
> oder direkt als [FragDesk.zip](https://github.com/Micha040/effective-lamp/releases/latest/download/FragDesk.zip) herunterladen.

**Deine CS2-Stats und Highlights – komplett auf deinem PC.** FragDesk liest deine Demos (Premier, Wettkampf, Wingman,
FACEIT) und zeigt daraus über 150 Kennzahlen, deinen Premier-Verlauf als Graph, jedes Match bis zur einzelnen Runde,
jede Runde als **2D-Ansicht von oben**, **Heatmaps**, dein **Crosshair-Placement** und deine **Reaktionszeit** – und
die Momente, die sich lohnen: Multikills, gewonnene Clutches, Noscopes, Wallbangs, Kills durch Smoke und mehr. Welche
Demo ausgewertet und welcher Moment als Video aufgenommen wird, entscheidest du. Mit der **[Handy-App](#handy-app)** hast
du Stats und Clips auch auf dem iPhone oder Android-Handy.

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
- **Matches:** alle ausgewerteten Matches als sortierbare Tabelle. Ein Klick öffnet das Match mit acht Bereichen:
  *Übersicht* (Rundenstreifen mit Kills und Clutches) · *Scoreboard* (beide Teams, Rating, ADR, KAST, Crosshair-Placement,
  Reaktionszeit, Eröffnungen, Clutches, Utility, MVPs, Premier vorher/nachher) · *Runden* (Kauf, Schaden, Tod, Eröffnung) ·
  *Waffen* (Kills, Treffer, Kopfquote, Körperzonen, „gestorben durch“) · *Duelle* (gegen jeden Gegner, kompletter
  Killfeed) · *Utility* · *Momente* · *Heatmap*. Die Werte jedes anderen Spielers lassen sich ebenfalls anzeigen.
- **2D-Ansicht:** jede Runde von oben zum Abspielen – wo alle stehen und hinschauen, Smokes, Mollys, Flashes und
  HEs, Kills, Bombe (siehe [unten](#2d-ansicht)).
- **Statistiken:** über 150 Kennzahlen in zwölf Bereichen – Allgemein, Waffen, Maps, Eröffnung & Trades, Clutches
  (1v1 bis 1v5), Utility (Flashes, geblendete Gegner, Blendzeit, Flash-Assists …), Zielen (Crosshair-Placement und
  Reaktionszeit, auch nach Waffenart, Treffsicherheit, erster Schuss, Spray, in Bewegung, Trefferzonen), Besondere
  Kills (Wallbang, Noscope, durch Smoke, geblendet, im Sprung …), Schaden & Zeit, Economy & Bombe, **Verlauf**
  (Premier, Rating, ADR, K/D, KAST, HS-Quote, Crosshair-Placement, Reaktionszeit als Graphen) und **Heatmaps** pro Map
  über alle Matches. Jede Kennzahl hat einen Tooltip mit Erklärung.
- **Momente:** alle erkannten Highlights, nach Wert sortiert, mit **In CS2 ansehen** und **Aufnehmen**;
  dazu die Liste deiner Videos.

<p align="center"><img src="../../docs/media/fragdesk-stats.png" width="760" alt="FragDesk: Statistiken, Verlauf als Graphen"></p>

### Wie Momente erkannt werden

Jede Runde jedes Spielers bekommt Punkte: für die Zahl der Kills (Doppelkill bis Ass), für gewonnene Clutches
(je mehr Gegner, desto mehr) und für Besonderes – Noscope, Wallbang, durch Smoke, geblendet, im Sprung, Messer,
Zeus, weiter Schuss, zwei auf einen Streich, alles Kopfschüsse, schnelle Folge. Ab einer Schwelle ist es ein Moment;
der Ausschnitt beginnt 4 Sekunden vor dem ersten Kill und endet 2 Sekunden nach dem letzten (bei Clutches die ganze
entscheidende Phase).

## 2D-Ansicht

Im Match oben auf **2D-Ansicht** – oder direkt auf eine Runde im Rundenstreifen bzw. im Bereich *Runden* klicken.
Zu sehen ist die Runde auf der Karte von oben, so wie im Radar:

- **Spieler** als Punkte in Teamfarbe mit Blickrichtung (Kegel), HP-Ring, Name; Bombenträger, Entschärfen/Legen,
  geblendet und Schüsse sind markiert. Ein Klick auf einen Spieler (auf der Karte oder in der Liste) hebt ihn hervor.
- **Granaten:** Flugbahn, dann die Wirkung – Smoke (so lange sie steht), Molotov/Brandgranate als tatsächliche
  Feuerfläche, Flash, HE, Decoy.
- **Kills** als Kreuz mit kurzer Linie vom Schützen, rechts die Killliste mit Zeit (Klick springt kurz davor);
  **Bombe** liegend, gelegt (mit Restzeit) und entschärft.
- Unten die **Zeitleiste** mit allen Kills und Bomben-Ereignissen, Tempo ½× bis 4×.
  Tasten: **Leertaste** Abspielen/Anhalten, **Pfeile links/rechts** 5 Sekunden, **Pfeile hoch/runter** Runde,
  **Esc** zurück.

<p align="center"><img src="../../docs/media/fragdesk-2d.png" width="760" alt="FragDesk: 2D-Ansicht einer Runde auf Mirage mit Spielern, Blickrichtung, Smokes, Molotov und Killliste"></p>

Beim ersten Öffnen liest FragDesk die Demo einmal komplett (wieder mit `fdanalyze.exe`, ein paar Sekunden) und legt
die Positionen neben die Auswertung (`matches\<Prüfsumme>.replay`, 0,5–3 MB pro Match); danach geht es sofort und
auch ohne die Demo. Positionen werden 8-mal pro Sekunde gespeichert und dazwischen weich verschoben – für genaue
Pixel-Duelle ist die Demo in CS2 das Original. Radarbilder gibt es für alle aktuellen Karten; bei einer unbekannten
Karte stehen die Punkte auf einem Raster.

## Heatmaps

Pro Match im Tab **Heatmap**, über alle Matches einer Map unter **Statistiken › Heatmaps** (dort gelten die Filter für
Modus und Zeitraum). Auf dem Radarbild lässt sich ein- und ausschalten, was zu sehen ist:

- **Aufenthalt:** wo die gewählten Spieler während der Runden waren (einmal pro Sekunde, ohne Kaufzeit).
- **Kills** (vom Ort des Schützen) und **Tode**.
- **Smokes, Mollys, Flashes, HEs** am Landepunkt – wie viel wo landet. Mit **Wurfpositionen** dazu eine Linie von der
  Stelle, an der geworfen wurde, also die Lineups.
- **Spieler:** Ich, mein Team, Gegner (einzeln oder zusammen), **Seite:** beide, T oder CT.
- **Punkte:** jedes Ereignis einzeln mit Tooltip (Runde, Zeit, wer, Waffe). Ein Klick öffnet die **2D-Ansicht** genau
  dort, 2 Sekunden davor.

Ist eine Ebene allein an, zeigt die Karte den klassischen Verlauf von blau (wenig) bis rot (viel); mehrere Ebenen
bekommen je eine eigene Farbe.

<p align="center"><img src="../../docs/media/fragdesk-heatmap.png" width="760" alt="FragDesk: Heatmap eines Matches auf Mirage – wo sich die Spieler aufgehalten haben"></p>

## Crosshair-Placement und Reaktionszeit

- **Crosshair-Placement:** der Winkel zwischen deinem Fadenkreuz und dem Kopf des Gegners in dem Moment, in dem er für
  dich sichtbar wird – im Schnitt, kleiner ist besser. Dazu der Anteil der Begegnungen mit höchstens 5° („Fadenkreuz am
  Kopf“) und die Aufteilung in waagerecht und senkrecht.
- **Reaktionszeit:** die Zeit vom Sichtbarwerden bis zu deinem ersten Treffer an diesem Gegner (nur Treffer innerhalb
  von 2 Sekunden).

Gezählt wird nur, wenn du eine Schusswaffe in der Hand hast, nicht geblendet bist und der Gegner auf deinem Bildschirm
auftaucht. Taucht er nach mindestens einer Sekunde Pause wieder auf, ist das eine neue Begegnung. „Sichtbar“ meldet
das Spiel selbst (dieselbe Information, mit der es das Radar füllt) – manchmal ein paar Ticks zu spät. Für Vergleiche
und deinen Verlauf taugt das gut, ein Labor-Messwert ist es nicht, und die Zahlen sind nicht dieselben wie bei Leetify.
Zu finden unter **Statistiken › Zielen** (auch nach Gewehr, Scharfschützengewehr, Pistole und MP), im **Verlauf** und
im **Scoreboard** jedes Matches für alle Spieler.

**Ältere Matches:** Heatmap-Daten, Crosshair-Placement und Reaktionszeit entstehen beim Auswerten (ab FragDesk 1.2).
Für Matches, die vorher ausgewertet wurden, zeigt FragDesk einen Hinweis mit **Neu auswerten** – dafür muss die Demo
noch da sein.

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

## Handy-App

Deine Stats und Clips auf dem Handy – als Web-App, die FragDesk selbst ausliefert: kein App Store, kein Server, keine
Kosten. Zu sehen ist dasselbe wie in FragDesk, fürs Handy umgebaut: **Übersicht**, **Matches** mit allen Bereichen,
**Statistiken** samt Verlauf und deine **Momente und Videos**. Heatmaps und die 2D-Ansicht kommen noch. Ansehen und
Aufnehmen in CS2 bleibt am PC.

Zwei Wege: **Direkt vom PC** (so wie hier beschrieben – der PC muss an sein) oder über einen **[eigenen
Server](#eigener-server)** (ein paar Euro im Monat; der PC muss nicht an sein, auch mit Freunden teilbar). Umschalten
in FragDesk unter Einstellungen › Handy-App › Verbindung.

**Einrichten (einmalig):**

1. In FragDesk unter **Einstellungen › Handy-App** „Handy-App erlauben“ einschalten. Beim ersten Mal fragt Windows, ob
   FragDesk Verbindungen annehmen darf – „Zugriff zulassen“.
2. **Handy verbinden** und den QR-Code mit der iPhone-Kamera scannen. Safari öffnet FragDesk und verbindet sich.
3. In Safari **Teilen › Zum Home-Bildschirm**. Ab dann startet FragDesk wie eine App im Vollbild. Fragt es dort nach dem
   Code, gib ihn noch einmal ein (er gilt 10 Minuten).

**Zu Hause und unterwegs:** Im Heimnetz geht es sofort. Für unterwegs [Tailscale](https://tailscale.com/download)
(für private Nutzung kostenlos) auf PC und Handy installieren und mit demselben Konto anmelden. FragDesk erkennt das und
nimmt die Tailscale-Adresse für den QR-Code – die gilt zu Hause und unterwegs. Dein PC muss dafür an sein und FragDesk
offen (minimiert reicht).

**Videos aufs Handy:** Im Video auf „Aufs Handy speichern“. Es landet in der Dateien-App unter „Downloads“; dort
Teilen › „Video sichern“, dann ist es in Fotos. Mit einer HTTPS-Adresse (siehe unten) geht es direkt über „Teilen ›
Video sichern“, und die App startet auch ohne Verbindung mit den zuletzt geladenen Daten.

**Sicherheit:** Aus, bis du es einschaltest. Nur gekoppelte Handys kommen an Daten und Videos: Gekoppelt wird mit einem
6-stelligen Code, der nur 10 Minuten gilt und nach 5 Fehlversuchen verfällt; danach meldet sich das Handy mit einem
langen Zufallsschlüssel an. Gekoppelte Handys siehst und entfernst du unter Einstellungen › Handy-App. FragDesk spricht
dabei nur mit deinem Handy im Heimnetz bzw. über Tailscale, nichts geht ins Internet – und mit CS2 oder dem
Anti-Cheat hat das nichts zu tun.

**Rücksicht aufs Spiel:** Solange CS2 läuft, schickt FragDesk keine Videos (die Stats schon – das sind nur ein paar
KB), damit dein Ping nicht leidet. Im Leerlauf kostet die Handy-App nichts; Vorschaubilder der Videos entstehen einmal
und liegen dann in `%LOCALAPPDATA%\FragDesk\thumbs`.

**HTTPS über Tailscale (für Profis):** `tailscale serve --bg 7420` in einer Eingabeaufforderung auf dem PC liefert eine
Adresse wie `https://dein-pc.dein-tailnet.ts.net`. Adresse kopieren und in FragDesk bei „Adresse fürs Handy“ auf
**Eigene einfügen** – dann steht sie im QR-Code. „Automatisch“ nimmt wieder die erkannte Adresse.

**Ohne laufenden PC:** Mit einem [eigenen Server](#eigener-server) liest die Handy-App vom Server – dein PC darf aus
sein, und Freunde können mitmachen.

## Eigener Server

Ein kleiner gemieteter Linux-Server speichert deine Stats und Clips – und auf Wunsch die deiner Freunde. FragDesk lädt
im Hintergrund hoch, die Handy-App liest vom Server: dieselbe App mit denselben Ansichten wie oben, aber dein PC muss
nicht an sein und Tailscale brauchst du nicht. Hochgeladen wird **nie, während CS2 läuft oder aufgenommen wird**.

**Was du brauchst:** einen VPS mit Debian oder Ubuntu (x86_64), einer eigenen IPv4-Adresse und SSH-Zugang als root.
Klein reicht völlig, z. B. der kleinste netcup-VPS („VPS nano“) für rund 3–4 € im Monat samt IPv4 – Preise ändern
sich, beim Anbieter nachsehen. Platz: ein Match belegt 100–400 KB (Auswertung und Heatmap), ein Clip 2–10 MB; 60 GB
reichen für Tausende Matches und mehrere Tausend Clips.

**Installieren – ein Befehl** (am PC im Terminal `ssh root@<IP-des-Servers>`, dann auf dem Server):

```sh
curl -fsSL https://raw.githubusercontent.com/Micha040/effective-lamp/main/apps/fragdesk/server/install.sh | bash
```

Als normaler Benutzer statt root: `… | sudo bash`. Fehlt `curl`: erst `apt update && apt install -y curl`.

Das Skript installiert [Caddy](https://caddyserver.com) aus den Paketquellen (HTTPS mit kostenlosem Zertifikat von
Let's Encrypt, erneuert sich selbst), legt den Benutzer `fragdesk` an, holt `fragdesk-server` aus dem neuesten
[Release](https://github.com/Micha040/effective-lamp/releases/latest) (geprüft mit `SHA256SUMS.txt`), richtet den
systemd-Dienst ein, gibt in ufw (falls aktiv) die Ports 80 und 443 frei, startet alles, prüft es – und zeigt zum
Schluss **deine Einladung als Admin**:

```
Deine Einladung als Admin – gilt 7 Tage und nur einmal:

  https://203-0-113-5.sslip.io/#invite=…
```

Ohne eigene Domain heißt dein Server `<IP-mit-Bindestrichen>.sslip.io` ([sslip.io](https://sslip.io) zeigt den Namen
kostenlos auf deine IP – das reicht für das Zertifikat). Mit eigener Domain (DNS-A-Eintrag auf die IP des Servers):
`… | bash -s -- --domain stats.example.org`. Erst schauen, was passieren würde: `--dry-run`. Hat dein Anbieter
eine eigene Firewall im Kundenmenü, müssen dort die Ports 80 und 443 offen sein.

**Verbinden:** Den Einladungslink auf den PC bringen (z. B. per Messenger an dich selbst) und kopieren. In FragDesk
unter **Einstellungen › Handy-App** den Modus **„Eigener Server“** wählen und **„Einladung einfügen“**. Ab dann lädt
FragDesk neue Matches und Videos hoch (beim Start, nach jeder Auswertung und Aufnahme, sonst alle 15 Minuten; Videos in
Stücken, ein Abbruch schadet nicht) und zeigt dort Server, Konto, Speicher und Stand. **„Handy verbinden“** zeigt wie
gewohnt einen QR-Code – jetzt für den Server. Mit dem Schalter „Videos hochladen“ bleiben Videos auf Wunsch am PC;
**„Trennen“** meldet FragDesk vom Server ab.

**Löschen:** Löschst du in FragDesk ein Match oder Video, verschwindet es beim nächsten Abgleich auch vom Server. Fehlt
etwas nur in FragDesk – neuer PC, Video im Explorer gelöscht, um am PC Platz zu sparen –, bleibt es auf dem Server.

**Neuer PC oder „Trennen“ gedrückt:** In der Handy-App unter **Einstellungen › PC neu verbinden** einen Link holen und
in FragDesk wie eine Einladung einfügen – das verbindet FragDesk wieder mit *deinem* Konto, alle Daten bleiben (der
bisher verbundene PC wird abgemeldet; der Link gilt einen Tag und einmal). Für Freunde macht das der Admin unter
**Server verwalten** › Konto › „PC neu verbinden“, auf dem Server geht es mit `fragdesk-server invite --user <ID>`
(IDs zeigt `fragdesk-server users`).

**Freunde einladen:** als Admin in FragDesk mit **„Freund einladen“** (der Link landet in der Zwischenablage), in der
Handy-App unter **Einstellungen › Server verwalten** (Link erstellen und direkt teilen) oder auf dem Server mit
`fragdesk-server invite` (`--admin` für einen weiteren Admin, `--days` für die Gültigkeit). Jeder Link gilt
einmal und 7 Tage. Öffnet ihn jemand auf dem Handy, erklärt die Seite, dass er in FragDesk am PC gehört.

**Konten und Speicher:** Jeder hat sein eigenes Konto und sieht nur seine eigenen Stats und Clips. Eingeladene Konten
bekommen 15 GB, Admins unbegrenzt. Unter **Server verwalten** in der Handy-App siehst du alle Konten mit belegtem
Speicher, änderst das Limit (5, 10, 15, 25, 50 GB oder unbegrenzt) und entfernst Konten samt Daten; die Stats der
anderen sieht auch der Admin nicht. Ist ein Speicher voll, lädt FragDesk nichts Neues mehr hoch und sagt das – alles,
was schon oben ist, bleibt zu sehen. Auf dem Server listet `fragdesk-server users` alle Konten.

**Aktualisieren:** Denselben Befehl noch einmal ausführen. Er holt das neueste Release; Daten, Domain und Zeitzone
bleiben (eine neue Admin-Einladung gibt es nur, solange noch niemand Admin ist).

**Deinstallieren:** `curl -fsSL …/install.sh | bash -s -- --uninstall` entfernt Dienst, Programm und den
Caddy-Eintrag. Die Daten bleiben in `/var/lib/fragdesk` – ganz weg damit: `rm -rf /var/lib/fragdesk && userdel
fragdesk`. Caddy bleibt installiert.

**Sicherheit:**

- `fragdesk-server` lauscht nur auf `127.0.0.1:7420`; von außen geht es nur über Caddy und HTTPS.
- FragDesk meldet sich mit einem langen Zufallsschlüssel an, Handys wie gewohnt mit dem 6-stelligen Code (gilt
  10 Minuten, nach 5 Fehlversuchen verfallen) und danach einem eigenen Schlüssel. Auf der Platte liegen Schlüssel und
  Einladungscodes nur als SHA-256 – wer die Dateien kopiert, kann sich damit nicht anmelden.
- Gegen Raten: höchstens 5 falsche Einladungen bzw. Kopplungscodes pro 15 Minuten und IP.
- Der Dienst läuft als eigener Benutzer ohne Rechte, darf nur in `/var/lib/fragdesk` schreiben (0700), keine
  Verbindungen nach außen aufbauen und nur die nötigen Systemaufrufe benutzen (systemd-Härtung).
- `fragdesk-server invite/users` sprechen über 127.0.0.1 mit dem Dienst und brauchen den Schlüssel aus
  `/var/lib/fragdesk/local.key`; von außen sperrt Caddy diese Befehle.
- Wer root auf dem Server hat, kann alle Daten lesen – miete den Server selbst und gib den Zugang nicht weiter.
  Sichern: den Ordner `/var/lib/fragdesk` (oder Snapshots beim Anbieter).

**Ressourcen:** Im Leerlauf schläft der Server (einmal pro Minute räumt er kurz auf) und braucht wenige MB
Arbeitsspeicher. Die Matches eines Kontos lädt er erst, wenn ein Handy fragt (rund 150 KB pro Match), und gibt sie
nach 15 Minuten ohne Handy wieder frei. Caddy komprimiert JSON und Text, Videos nicht. Uhrzeiten in der Handy-App
rechnet der Server in seine Zeitzone um – steht der Server auf UTC, nimmt `install.sh` Europe/Berlin
(`--timezone <Zone>` ändert das).

## Anti-Cheat und Sicherheit

- FragDesk **liest nur Demo-Dateien**. Es greift nie in ein laufendes Spiel ein: keine Injektion, keine Hooks,
  keine simulierten Eingaben, kein Lesen fremden Speichers.
- CS2 startet es **nur zum Ansehen oder Aufnehmen** – und nur, wenn CS2 gerade nicht läuft. Dabei nutzt es
  ausschließlich offizielle Startoptionen von Valve (`-tools` für den Workshop-Tools-Modus, `+playdemo` und die
  Konsole über `-netconport`), **kein Plugin, keine geänderten Spieldateien**. CS2 spielt dabei nur die Demo auf
  deinem PC ab und verbindet sich mit keinem Server. Für Matches startest du CS2 danach wie gewohnt neu.
- Alles bleibt auf deinem PC: Es wird nichts hochgeladen, FragDesk braucht kein Internet. Die [Handy-App](#handy-app)
  (aus, bis du sie einschaltest) verbindet nur Handys, die du selbst gekoppelt hast – im Heimnetz oder über Tailscale.
  Nur wenn du einen [eigenen Server](#eigener-server) einrichtest, lädt FragDesk Auswertungen und Videos dorthin – und
  nirgendwo sonst hin.

## Ressourcen

- Im Leerlauf tut FragDesk nichts außer auf neue Dateien in den Demo-Ordnern zu warten (Windows meldet sich dann) und –
  falls die Handy-App an ist – auf Anfragen vom Handy.
- Ausgewertet wird in einem eigenen Prozess (`fdanalyze.exe`) mit niedriger Priorität: ein paar Sekunden lang
  ca. 150–250 MB Arbeitsspeicher, danach ist er wieder weg. Er liest die Demo dafür zweimal gleichzeitig (Statistiken
  und Heatmap/Zielen) – auf einem Mehrkern-Prozessor kaum langsamer als einmal.
- Ausgewertete Matches liegen als kleine JSON-Dateien (50–200 KB pro Match) in `%LOCALAPPDATA%\FragDesk\matches`,
  daneben die Heatmap-Daten (30–200 KB) und die Daten der 2D-Ansicht für die Matches, die du dir so angesehen hast. Die Demo selbst brauchst du danach nur
  noch zum Ansehen und Aufnehmen in CS2.
- Die 2D-Ansicht zeichnet nur, solange sie abspielt und das Fenster zu sehen ist (rund 30 Bilder pro Sekunde),
  angehalten oder minimiert kostet sie nichts. Heatmaps rechnet FragDesk nur, wenn sich die Auswahl ändert; die
  Daten vieler Matches liest es im Hintergrund.

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

Die Radarbilder der 2D-Ansicht stammen aus Counter-Strike 2 und gehören Valve (© Valve Corporation); FragDesk
liefert sie mit wie [CS Demo Manager](https://github.com/akiver/cs-demo-manager), von dem auch die Kartenwerte
stammen (MIT, © AkiVer). FragDesk ist kein offizielles Produkt von Valve. Einzelheiten stehen ebenfalls in
`THIRD-PARTY-NOTICES.txt` (im Quelltext: [`res/radars/NOTICE.txt`](res/radars/NOTICE.txt)).
