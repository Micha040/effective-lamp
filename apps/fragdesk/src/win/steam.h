// FragDesk – Steam und CS2 finden (nur Dateien lesen: Registry, loginusers.vdf, libraryfolders.vdf)
#pragma once
#include <string>
#include <vector>

namespace fdw {

struct SteamAccount {
    std::string steamId, accountName, personaName;
    bool mostRecent = false;
};

struct SteamInfo {
    std::wstring steamDir;
    std::wstring cs2Dir;       // ...\Counter-Strike Global Offensive
    std::vector<SteamAccount> accounts;

    std::wstring cs2Exe() const;        // ...\game\bin\win64\cs2.exe
    std::wstring replaysDir() const;    // ...\game\csgo\replays
    std::wstring csgoDir() const;       // ...\game\csgo
    std::wstring appManifest() const;   // ...\steamapps\appmanifest_730.acf der Bibliothek mit CS2
    std::string recentSteamId() const;  // zuletzt angemeldetes Konto
};

SteamInfo detectSteam(const std::wstring& cs2Override);

// Kostenloser DLC „Counter-Strike 2 Workshop Tools“: Nur in diesem Modus (-tools) öffnet CS2 unter Windows die Konsole
// für -netconport (sonst fehlt dem Spiel an der Stelle noch WSAStartup). Laut appmanifest_730.acf: Depot 2347779 / DLC 2279721.
enum class Tools { Unknown, Missing, Installed };
Tools workshopTools(const SteamInfo& steam);

}  // namespace fdw
