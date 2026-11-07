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
    std::string recentSteamId() const;  // zuletzt angemeldetes Konto
};

SteamInfo detectSteam(const std::wstring& cs2Override);

}  // namespace fdw
