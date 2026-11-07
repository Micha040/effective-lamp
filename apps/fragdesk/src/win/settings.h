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

struct MobileSettings {
    bool enabled = false;                 // aus, bis man es einschaltet
    int port = 7420;
    std::string address;                  // eigene Adresse fürs Handy (z. B. https://pc.tailnet.ts.net), leer = automatisch
    std::vector<MobileDevice> devices;    // gekoppelte Handys
};

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
