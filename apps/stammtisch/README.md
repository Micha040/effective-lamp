# STAMMTISCH

> Teil von **[efficient lamp](https://github.com/Micha040/effective-lamp)** – anders als die übrigen Programme kein
> Windows-Tool, sondern eine **Web-App fürs Handy**: **[STAMMTISCH öffnen](https://micha040.github.io/effective-lamp/stammtisch/)**
> (zum Mitmachen braucht man einen Einladungscode).

**Die App für die Runde, die sich jeden Donnerstag trifft.** Wo ist der nächste Stammtisch, wer kommt, was wird im Chat
besprochen – und danach landen die Bilder des Abends im Album. Läuft auf iPhone und Android ohne App Store: Link
öffnen, zum Home-Bildschirm hinzufügen, fertig.

<p align="center"><img src="../../docs/media/stammtisch.png" width="760" alt="STAMMTISCH: Übersicht mit dem nächsten Treffen, Chat und Bilder-Alben"></p>

Version 1.0.0 · [Was sie kann](#was-sie-kann) · [Mitmachen](#mitmachen) · [Sicherheit](#sicherheit-und-datenschutz) ·
[Kosten und Grenzen](#kosten-und-grenzen) · [Technik](#technik) · [Eigener Server](#eigener-server-später) ·
[Entwickeln](#entwickeln)

## Was sie kann

| | |
|---|---|
| **Treffen** | Das nächste Treffen steht groß oben: Tag, Uhrzeit, Ort mit Karte und Route. Darunter die kommenden, im Archiv die vergangenen. Ein Ort kann eine Bar sein, bei jemandem zuhause oder ganz woanders – oder erst einmal offen bleiben. |
| **Zusagen** | Dabei / Vielleicht / Raus mit einem Tipp. Alle sehen, wer kommt. |
| **Chat** | Ein Chat für die ganze Gruppe: Text, Bilder, Videos, Sprachnachrichten, Antworten auf einzelne Nachrichten, Reaktionen. Neue Nachrichten erscheinen sofort. |
| **Bilder** | Ein Album je Treffen. Fotos werden vor dem Hochladen verkleinert (spart Speicher und Datenvolumen), Videos gehen bis 50 MB. Ansehen, speichern, in ein anderes Album verschieben. |
| **Jeder darf alles** | Treffen, Orte und Alben kann jedes Mitglied ändern. Damit das gut geht, gibt es den **Verlauf** (wer hat wann was geändert, vorher → nachher, mit „Rückgängig“) und den **Papierkorb** (Gelöschtes bleibt 30 Tage). |
| **Gruppen** | Jede Gruppe hat eigene Treffen, Chat, Bilder, Mitglieder und einen eigenen Einladungscode. Man kann in mehreren Gruppen sein; sehen kann man nur die eigenen. |
| **Ohne Netz** | Im Keller der Bar zeigt die App den zuletzt geladenen Stand der Treffen. |

Treffen legt immer jemand aus der Gruppe an – die App schlägt dabei den nächsten Donnerstag zur üblichen Uhrzeit vor.
Gespeicherte Orte („Stammbars“, „Bei Tom“) lassen sich mit einem Tipp auswählen.

## Mitmachen

1. Jemand aus der Gruppe schickt dir den **Einladungslink** (Einstellungen › Leute einladen).
2. **iPhone:** Link in Safari öffnen, unten auf **Teilen › „Zum Home-Bildschirm“**, dann STAMMTISCH vom Home-Bildschirm
   öffnen und dort den Code eingeben. (Die installierte App hat ihren eigenen Speicher – wer schon im Browser beitritt,
   muss sich in der App noch einmal verbinden.)
   **Android:** Link in Chrome öffnen, Name eingeben, beitreten. Danach im Menü **„App installieren“**.
3. Namen eingeben – fertig. Kein Konto, keine E-Mail, kein Passwort: Das Gerät merkt sich die Anmeldung.

**Neues Handy oder zweites Gerät (z. B. der PC):** In den Einstellungen unter „Weiteres Gerät verbinden“ – oder jemand
anderes aus der Gruppe tippt in der Mitgliederliste auf deinen Namen und dann auf „Gerätecode erzeugen“. Den Code
(8 Zeichen, 15 Minuten gültig, gilt einmal) gibst du auf dem neuen Gerät ein und bist wieder als du selbst dabei.

**Admin:** Wer die Gruppe gegründet hat. Admins können Mitglieder entfernen und wieder aufnehmen, andere zu Admins
machen und fremde Chat-Nachrichten löschen. Alles andere darf jeder.

## Sicherheit und Datenschutz

- **Der Quelltext ist öffentlich, die Inhalte nicht.** Treffen, Chat, Bilder und Mitglieder sieht nur, wer in der
  Gruppe ist. Das prüft nicht die App, sondern die **Datenbank selbst** bei jeder einzelnen Anfrage
  (Row Level Security, [supabase/migrations](supabase/migrations)) – auch wer die App umgeht und direkt mit dem Server
  spricht, bekommt nichts zu sehen. Genau das spielen die Tests in [tests/db/test.sql](tests/db/test.sql) mit zwei
  Gruppen durch.
- **Bilder liegen privat.** Jede Bildadresse ist nur für Mitglieder abrufbar und gilt einen Tag.
- **Beitreten nur mit Code.** Ohne gültigen Einladungs- oder Gerätecode wird nicht einmal eine Anmeldung angelegt.
  Ein neuer Einladungscode (Einstellungen › Leute einladen › „Neuen Code erzeugen“) macht den alten ungültig.
- **Schlüssel:** In [web/config.js](web/config.js) steht nur der *öffentliche* Schlüssel von Supabase. Er ist dafür
  gemacht, in jeder ausgelieferten App zu stehen, und darf nur, was die Zugriffsregeln erlauben. Geheime Schlüssel
  stehen nirgends im Repo; die Tests brechen ab, falls doch einer hineingerät.
- **Was Dritte sehen:** Kartenbilder kommen von OpenStreetMap, die Ortssuche läuft über deren Dienst Nominatim (nur
  beim Tipp auf „Suchen“). Sonst spricht die App nur mit GitHub Pages (die App selbst) und Supabase (die Daten,
  Serverstandort Frankfurt).

## Kosten und Grenzen

Alles läuft in kostenlosen Tarifen:

| Was | Wo | Grenze im kostenlosen Tarif |
|---|---|---|
| Die App (Dateien) | GitHub Pages | praktisch keine |
| Daten, Anmeldung, Live-Chat | Supabase | 500 MB Datenbank – reicht für Jahre an Text |
| Bilder, Videos, Sprachnachrichten | Supabase | **1 GB für alle Gruppen zusammen**, 50 MB je Datei, 5 GB Abruf im Monat |

- **Speicher:** Ein verkleinertes Foto braucht rund 0,3 MB – 1 GB reicht für grob 2.500 Fotos. Videos fressen den Platz
  schnell (ein Clip von 30 Sekunden hat oft 30–60 MB). Der Stand steht unter Einstellungen › Speicher. Wird es eng: Videos
  sparsam, alte Alben ausmisten – oder der [eigene Server](#eigener-server-später).
- **Einschlafen:** Supabase legt kostenlose Projekte nach einer Woche ohne Zugriffe schlafen. Damit das auch in den
  Ferien nicht passiert, schickt GitHub zweimal pro Woche ein Lebenszeichen
  ([stammtisch-wecker.yml](../../.github/workflows/stammtisch-wecker.yml)). Schläft das Projekt doch einmal: im
  Supabase-Dashboard auf „Restore project“ klicken, die Daten sind noch da.
- **Videos vom iPhone** nimmt die App so, wie das Handy sie liefert. Sehr neue Formate spielt nicht jedes ältere
  Android-Handy ab – dann lässt sich das Video über „Speichern oder teilen“ herunterladen.

## Technik

```
apps/stammtisch/
  web/                    die App: reines HTML, CSS und JavaScript, kein Build-Schritt
    index.html · app.css · sw.js (Service Worker) · manifest.webmanifest · config.js (Adresse + öffentlicher Schlüssel)
    js/app.js             Start, Navigation, Live-Verbindung
    js/pages/             je Seite eine Datei: welcome, events, event, event-edit, chat, photos, settings, changelog
    js/backend/           die Anbindung: supabase.js (echt) · demo.js (Beispieldaten) · index.js (wählt aus)
    js/util.js            Datum, Codes, Texte – ohne Browser testbar
    js/media.js           Bilder verkleinern, Sprachnachrichten aufnehmen, hochladen
    js/map.js             Karte (Leaflet + OpenStreetMap) und Ortssuche
    vendor/               mitgeliefert: supabase-js 2.119.0 (MIT), Leaflet 1.9.4 (BSD-2-Clause)
  supabase/
    migrations/           die Datenbank: Tabellen, Zugriffsregeln, Funktionen, Speicher – in dieser Reihenfolge einspielen
    functions/konto/      legt für ein neues Gerät eine Anmeldung an (nur mit gültigem Code)
  tests/                  run.sh · util.test.js · demo.test.js · check.mjs · db.sh + db/ (Datenbank-Tests)
  tools/                  render.sh (Oberfläche als Bilder) · make_icons.py · ping.mjs (Lebenszeichen)
```

- **Gebaut wie die Handy-App von FragDesk:** ohne Framework, Seiten als ES-Module, die erst geladen werden, wenn man
  sie öffnet. Die Karte (Leaflet) kommt erst, wenn eine Seite wirklich eine zeigt.
- **Sparsam:** kein Abfragen im Takt. Änderungen der anderen kommen über *eine* offene Verbindung (Supabase Realtime) –
  und nur, solange die App sichtbar ist. Liegt sie länger als eine halbe Minute im Hintergrund, wird getrennt.
  Einmal geladene Bilder kommen aus dem Speicher des Handys statt erneut vom Server.
- **Anmeldung ohne Passwort:** Die Edge Function [konto](supabase/functions/konto/index.ts) prüft den Code und legt eine
  Anmeldung mit erfundener Adresse und Zufallspasswort an; beides merkt sich nur das Gerät. Der Beitritt selbst
  (`join_group`, `link_device`) passiert in der Datenbank.
- **Verlauf:** Trigger in der Datenbank schreiben jede Änderung an Treffen, Orten, Alben, Mitgliedern und Gruppe mit
  vorher/nachher mit – die App kann das weder vergessen noch fälschen. „Rückgängig“ (`undo_change`) setzt nur die Felder
  zurück, die der Eintrag geändert hat.
- **Papierkorb:** Gelöscht wird nie sofort, sondern mit Zeitstempel markiert. Was länger als 30 Tage liegt, räumt die
  App höchstens einmal am Tag beim Start weg – erst dann lässt die Datenbank das Löschen der Dateien überhaupt zu.

## Eigener Server (später)

Geplant ist ein eigener Server mit mehr Speicher. Die App ist darauf vorbereitet: **Die Seiten sprechen nie direkt mit
Supabase**, sondern nur mit dem Objekt aus [js/backend/](web/js/backend). Zwei Wege:

1. **Nur die Dateien umziehen** (am wenigsten Arbeit, löst das Speicherproblem): Anmeldung, Datenbank und Live-Chat
   bleiben bei Supabase, nur `upload`, `url` und `removeFiles` in [supabase.js](web/js/backend/supabase.js) zeigen auf
   den eigenen Server. Der prüft die Anmeldung (das Token von Supabase) und speichert unter demselben Pfad
   `<gruppe>/…`.
2. **Alles umziehen:** eine dritte Datei `js/backend/server.js`, die dieselben Funktionen anbietet wie `supabase.js`
   (Anmeldung, Gruppen, Treffen, Chat, Bilder, Verlauf, Live). [demo.js](web/js/backend/demo.js) zeigt in klein, wie so
   eine Anbindung aussieht; ein Test stellt sicher, dass beide dieselben Funktionen haben. In `config.js` wird dann
   `backend` umgestellt. Die Daten lassen sich aus Supabase als SQL exportieren, die Dateien über die Speicher-API.

## Entwickeln

```sh
# ausprobieren ohne Anmeldung: Beispieldaten, nichts wird gespeichert
python3 -m http.server 8080 --directory apps/stammtisch/web      # dann http://localhost:8080/?demo

sh apps/stammtisch/tests/run.sh                 # alle Tests (Node.js ≥ 18, PostgreSQL ≥ 15)
sh apps/stammtisch/tools/render.sh /tmp/bilder  # jede Seite als PNG, dunkel und hell (Chrome ohne Fenster)
python3 apps/stammtisch/tools/make_icons.py     # App-Symbole aus brand/tiles/stammtisch.svg
```

- **Tests:** `tests/run.sh` prüft die Hilfsfunktionen und die Demo-Anbindung, ob die Dateien zusammenpassen (Service
  Worker, Versionsnummer, keine geheimen Schlüssel) und – mit einem frischen PostgreSQL nur für diesen Lauf – die
  Datenbank: 119 Prüfungen, wer was sehen und ändern darf. Läuft bei jedem Push in GitHub Actions mit.
- **Veröffentlichen:** Jeder Push auf `main`, der `apps/stammtisch/web/` ändert, stellt die App neu bereit
  ([pages.yml](../../.github/workflows/pages.yml)) – erst die Tests, dann GitHub Pages. Mit dem Launcher und den
  Releases von efficient lamp hat das nichts zu tun.
- **Version:** steht in `web/config.js`, `web/sw.js` und oben in dieser Datei – `tests/check.mjs` bricht ab, wenn sie
  nicht übereinstimmen. Nach einer Änderung an der App die Nummer erhöhen, dann holen sich alle Handys die neue Fassung.
- **Datenbank ändern:** neue Datei in `supabase/migrations/` (fortlaufend nummeriert), Tests in `tests/db/test.sql`
  ergänzen, `sh tests/db.sh`, dann im Supabase-Dashboard (SQL Editor) oder mit der Supabase-CLI einspielen.
  Vorhandene Migrationen nicht nachträglich ändern.
- **Hinweise des Supabase-Beraters:** „RLS enabled, no policy“ bei `member_users` und `device_codes` ist Absicht (nur
  über Funktionen erreichbar), ebenso „security definer function executable“ bei den Funktionen der App – jede prüft
  selbst, ob der Aufrufer das darf.

## Geplant

- Mitteilungen aufs Handy (neue Nachricht, Ort steht fest, Erinnerung am Donnerstag)
- Kachel im Launcher, die die App am PC öffnet
- eigener Server mit mehr Speicher

## Lizenzen

STAMMTISCH steht wie das ganze Repo unter der [MIT-Lizenz](../../LICENSE). Mitgeliefert werden
[supabase-js](https://github.com/supabase/supabase-js) (MIT, [Lizenztext](web/vendor/LICENSE-supabase-js.txt)) und
[Leaflet](https://leafletjs.com) (BSD-2-Clause, [Lizenztext](web/vendor/LICENSE-leaflet.txt)). Kartendaten
© [OpenStreetMap](https://www.openstreetmap.org/copyright)-Mitwirkende.
