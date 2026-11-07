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
    // nicht leer: Grund, warum gerade keine Videos ausgeliefert werden (z. B. CS2 läuft)
    std::function<std::string()> mediaBlocked;
};

// Eine Anfrage beantworten (aus dem Thread des HTTP-Servers)
void handle(Host& host, const Request& req, Response& res);

std::string contentType(const std::string& path);

}  // namespace fdapi
