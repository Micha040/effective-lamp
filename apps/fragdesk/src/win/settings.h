// FragDesk – Einstellungen (%LOCALAPPDATA%\FragDesk\settings.json)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fdw {

struct RecordSettings {
    int height = 1080;          // 720, 1080, 1440
    int fps = 60;               // 30, 60
    int quality = 2;            // 1 = sparsam, 2 = gut, 3 = sehr gut (Bitrate)
    bool onlyKillfeed = true;   // HUD bis auf den Killfeed ausblenden
    bool voices = false;        // Sprachchat der Demo hörbar
    bool sound = true;          // Spielton aufnehmen
};

// Handy-App (Einstellungen › Handy-App)
struct MobileDevice {
    std::string token, name;
    int64_t created = 0, lastSeen = 0;
};

// Eigener Server (Einstellungen › Handy-App › „Eigener Server“)
struct ServerSettings {
    std::string url;            // z. B. https://203-0-113-5.sslip.io/ (leer = nicht verbunden)
    std::string token;          // Konto-Token für FragDesk
    std::string user, userId;   // Konto auf dem Server
    bool admin = false;
    bool videos = true;         // Videos hochladen
    std::string profile;        // zuletzt gemeldetes Profil ("Name\nSteamID")
    int64_t lastSync = 0;       // letzter vollständiger Abgleich (Sekunden seit 1970)
    int64_t used = -1, limit = 0;   // Speicher laut Server beim letzten Abgleich (−1 = unbekannt, limit 0 = unbegrenzt)
};

struct MobileSettings {
    bool enabled = false;                 // direkt vom PC: aus, bis man es einschaltet
    bool server = false;                  // Modus „Eigener Server“ (dann lauscht FragDesk selbst nicht)
    int port = 7420;
    std::string address;                  // eigene Adresse fürs Handy (z. B. https://pc.tailnet.ts.net), leer = automatisch
    std::vector<MobileDevice> devices;    // gekoppelte Handys
    ServerSettings srv;
};

// Taugt das als Schlüssel für eine Kopfzeile (Buchstaben, Ziffern, -._~; keine Zeilenumbrüche)?
bool validToken(const std::string& token);

struct Settings {
    std::string steamId;                  // eigenes Konto (leer = automatisch)
    std::wstring cs2Dir;                  // leer = automatisch über Steam
    std::wstring videoDir;                // leer = Videos\FragDesk
    std::vector<std::wstring> folders;    // zusätzliche Demo-Ordner
    bool watchReplays = true;             // CS2-Ordner "replays"
    bool watchDownloads = true;           // Downloads (FACEIT)
    bool autoAnalyze = false;             // neue Demos sofort auswerten
    bool dark = true;                     // wird beim Start aus Windows übernommen, falls theme leer
    std::string theme;                    // "", "dark", "light"
    std::vector<std::string> ignored;     // Demos, die nicht mehr angeboten werden ("Pfad|Größe")
    RecordSettings record;
    MobileSettings mobile;

    bool load();
    bool save() const;
    std::wstring effectiveVideoDir() const;
};

std::string jsonQuote(const std::string& s);

}  // namespace fdw
