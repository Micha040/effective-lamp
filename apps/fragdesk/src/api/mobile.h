// FragDesk – Handy-App: Anmeldung, Routen, Dateien. Plattformunabhängig; FragDesk (Windows) und der Testserver
// (Linux) liefern über Host die Daten, die App-Dateien und den Zugriff auf Videos.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../core/model.h"
#include "http.h"

namespace fdapi {

// Version der Schnittstelle (die Handy-App prüft sie)
constexpr int kApiVersion = 1;

struct ClipInfo {
    std::string id;          // stabil, aus dem Pfad (clipId)
    std::string path;        // UTF-8, nur für den Host
    std::string title, match, highlight, map;
    int64_t created = 0, size = 0;   // Sekunden seit 1970, Bytes
    double seconds = 0;
};
std::string clipId(const std::string& utf8Path);

// Daten für die Ansichten. Alle Aufrufe kommen aus demselben Thread (FragDesk: UI-Thread, siehe Host::withSource).
class Source {
public:
    virtual ~Source() = default;
    virtual std::string me() = 0;          // eigene SteamID (leer = unbekannt)
    virtual std::string meName() = 0;
    virtual bool loaded() = 0;             // Bibliothek fertig geladen
    virtual std::vector<const fd::Match*> matches() = 0;   // neueste zuerst, ohne Details
    virtual int generation() = 0;          // ändert sich bei jeder Änderung an matches()
    virtual bool detail(const std::string& checksum, fd::Match& out) = 0;   // mit Kills, Runden, Highlights …
    virtual std::vector<ClipInfo> clips() = 0;
    virtual int newDemos() { return 0; }   // noch nicht ausgewertete Demos
    // Pfad (UTF-8) einer Zusatzdatei neben der Auswertung: ext "heat" (Heatmap, FDHM) oder "replay" (2D-Ansicht, FDRP),
    // auf dem eigenen Server auch "noreplay" (FragDesk meldet: keine 2D-Daten möglich); leer, wenn es sie nicht gibt.
    // Nur den Pfad hier holen – lesen und auswerten außerhalb (readAll im Thread der Anfrage), damit große Dateien nicht
    // den Thread der Daten (FragDesk: UI-Thread) aufhalten.
    virtual std::string matchFile(const std::string& checksum, const std::string& ext) {
        (void)checksum;
        (void)ext;
        return std::string();
    }
};

// Gekoppelte Geräte und Kopplungscode. Thread-sicher.
struct Device {
    std::string token, name;
    int64_t created = 0, lastSeen = 0;
};

class Auth {
public:
    std::function<void()> onChange;   // Geräteliste geändert (aus beliebigem Thread) – speichern

    void setDevices(std::vector<Device> devices);
    std::vector<Device> devices() const;
    bool remove(const std::string& token);
    void removeAll();

    std::string startPairing(int minutes = 10, const std::string& fixed = std::string());   // neuer 6-stelliger Code (fixed: nur für Tests)
    void stopPairing();
    std::string pairingCode() const;             // leer = keine Kopplung offen
    int64_t pairingUntil() const;

    bool check(const std::string& token);   // gültig? (merkt sich „zuletzt gesehen“)
    // Code prüfen; liefert ein neues Token oder leer (err: Grund, tooMany: zu viele Versuche)
    std::string pair(const std::string& code, const std::string& name, std::string& err, bool& tooMany);

private:
    mutable std::mutex mu_;
    std::vector<Device> devices_;
    std::string code_;
    int64_t codeUntil_ = 0;
    int attempts_ = 0;
};

std::string randomHex(int bytes);   // aus dem Zufallsgenerator des Systems
int64_t nowEpoch();

// Stand der 2D-Daten eines Matches, das (noch) keine brauchbare .replay-Datei hat (Host::prepareReplay). Die Texte
// dazu stehen in view_replay.cpp.
struct ReplayState {
    enum Kind {
        Working,   // wird gerade aus der Demo erstellt
        Queued,    // wartet, bis ein anderes Match fertig ist
        WaitCs2,   // wird erstellt, sobald CS2 zu ist (nur im Performance-Modus „Performance“)
        WaitRecording,   // wird erstellt, sobald FragDesk am PC mit der Aufnahme fertig ist (jeder Modus)
        NoDemo,    // Demo-Datei nicht mehr da
        Failed,    // Erstellen ging schief (error: Grund)
    } kind = Failed;
    std::string error;
};

struct Host {
    std::string version;   // FragDesk-Version
    std::string kind = "pc";   // "pc" (FragDesk) oder "server"
    Auth* auth = nullptr;
    // fn im Thread der Daten ausführen und auf das Ende warten; false, wenn das gerade nicht geht (FragDesk beendet sich)
    std::function<bool(const std::function<void(Source&)>& fn)> withSource;
    // Datei der Handy-App (Pfad ohne führenden Schrägstrich, z. B. "app.js"); false = gibt es nicht
    std::function<bool(const std::string& path, std::string& body)> asset;
    // Datei öffnen (UTF-8-Pfad); nullptr = geht nicht
    std::function<FileReader(const std::string& path, int64_t& size)> openFile;
    // Vorschaubild (JPEG) eines Clips; optional
    std::function<bool(const ClipInfo& clip, std::string& jpeg)> thumbnail;
    // nicht leer: Grund, warum gerade keine Videos ausgeliefert werden (FragDesk nimmt auf; CS2 läuft im Performance-Modus
    // „Performance“)
    std::function<std::string()> mediaBlocked;
    // Radarbild (PNG) einer Karte – name wie fd::RadarMap::name (z. B. "de_nuke"), lower: untere Ebene; false = keins
    std::function<bool(const std::string& name, bool lower, std::string& png)> radar;
    // 2D-Ansicht ohne brauchbare .replay-Datei: FragDesk (direkt vom PC) erstellt sie im Hintergrund aus der Demo (demo:
    // Pfad laut Auswertung) und sagt, wie weit es ist – nie, während FragDesk aufnimmt, und im Performance-Modus
    // „Performance“ nie, solange CS2 läuft. Aus dem Thread der Anfrage, kehrt sofort zurück. broken: die vorhandene Datei
    // ist unbrauchbar (neu erstellen), retry: nach einem Fehler noch einmal versuchen. Leer (eigener Server): die Ansicht
    // erklärt selbst, warum es keine gibt.
    std::function<ReplayState(const std::string& checksum, const std::string& demo, bool broken, bool retry)> prepareReplay;
};

// Datei ganz lesen (über Host::openFile, im aufrufenden Thread); false, wenn sie fehlt, größer als maxBytes ist oder
// sich nicht lesen lässt
bool readAll(Host& host, const std::string& path, std::string& out, int64_t maxBytes);

// Eine Anfrage beantworten (aus dem Thread des HTTP-Servers)
void handle(Host& host, const Request& req, Response& res);

std::string contentType(const std::string& path);

}  // namespace fdapi
