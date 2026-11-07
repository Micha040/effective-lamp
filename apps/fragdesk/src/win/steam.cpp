// FragDesk – Steam und CS2 finden (siehe steam.h)
#include "steam.h"

#include "../core/vdf.h"
#include "common.h"

namespace fdw {

namespace {

std::wstring regString(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM view = 0) {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof buf;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | view, nullptr, buf, &size) != ERROR_SUCCESS) return std::wstring();
    return buf;
}

std::wstring normalize(std::wstring p) {
    for (auto& c : p)
        if (c == L'/') c = L'\\';
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();
    return p;
}

bool isCs2Dir(const std::wstring& dir) { return fileExists(joinPath(dir, L"game\\bin\\win64\\cs2.exe")); }

}  // namespace

std::wstring SteamInfo::cs2Exe() const { return cs2Dir.empty() ? std::wstring() : joinPath(cs2Dir, L"game\\bin\\win64\\cs2.exe"); }
std::wstring SteamInfo::csgoDir() const { return cs2Dir.empty() ? std::wstring() : joinPath(cs2Dir, L"game\\csgo"); }
std::wstring SteamInfo::replaysDir() const { return cs2Dir.empty() ? std::wstring() : joinPath(cs2Dir, L"game\\csgo\\replays"); }

std::wstring SteamInfo::appManifest() const {
    // cs2Dir = <Bibliothek>\steamapps\common\Counter-Strike Global Offensive
    std::wstring steamapps = cs2Dir.empty() ? std::wstring() : parentDir(parentDir(cs2Dir));
    return steamapps.empty() ? std::wstring() : joinPath(steamapps, L"appmanifest_730.acf");
}

Tools workshopTools(const SteamInfo& steam) {
    std::wstring path = steam.appManifest();
    std::string text;
    fd::VdfNode root;
    if (path.empty() || !readFile(path, text, 1u << 20) || !fd::parseVdf(text, root)) return Tools::Unknown;
    const fd::VdfNode* app = root.find("AppState");
    const fd::VdfNode* depots = app ? app->find("InstalledDepots") : nullptr;
    if (!depots) return Tools::Unknown;
    for (const auto& d : depots->children)
        if (d.key == "2347779" || (d.block && d.get("dlcappid") == "2279721")) return Tools::Installed;
    return Tools::Missing;
}

std::string SteamInfo::recentSteamId() const {
    for (const auto& a : accounts)
        if (a.mostRecent) return a.steamId;
    return accounts.size() == 1 ? accounts[0].steamId : std::string();
}

SteamInfo detectSteam(const std::wstring& cs2Override) {
    SteamInfo info;
    std::wstring steam = normalize(regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
    if (steam.empty() || !dirExists(steam)) steam = normalize(regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", RRF_SUBKEY_WOW6432KEY));
    if (steam.empty() || !dirExists(steam)) steam = normalize(regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"));
    if (!steam.empty() && dirExists(steam)) info.steamDir = steam;

    // Konten
    std::string text;
    if (!info.steamDir.empty() && readFile(joinPath(info.steamDir, L"config\\loginusers.vdf"), text, 1u << 20)) {
        fd::VdfNode root;
        if (fd::parseVdf(text, root))
            if (const fd::VdfNode* users = root.find("users"))
                for (const auto& u : users->children) {
                    if (!u.block || u.key.size() != 17) continue;
                    SteamAccount a;
                    a.steamId = u.key;
                    a.accountName = u.get("AccountName");
                    a.personaName = u.get("PersonaName");
                    a.mostRecent = u.get("MostRecent") == "1";
                    info.accounts.push_back(a);
                }
    }

    // CS2
    if (!cs2Override.empty() && isCs2Dir(cs2Override)) {
        info.cs2Dir = normalize(cs2Override);
        return info;
    }
    std::vector<std::wstring> libs;
    if (!info.steamDir.empty()) {
        libs.push_back(info.steamDir);
        if (readFile(joinPath(info.steamDir, L"steamapps\\libraryfolders.vdf"), text, 1u << 20)) {
            fd::VdfNode root;
            if (fd::parseVdf(text, root))
                if (const fd::VdfNode* lf = root.find("libraryfolders"))
                    for (const auto& l : lf->children) {
                        std::string p = l.block ? l.get("path") : l.value;   // altes Format: "1" "D:\\SteamLibrary"
                        if (p.empty()) continue;
                        std::wstring w = normalize(widen(p));
                        bool hasCs2 = !l.block || !l.find("apps") || l.find("apps")->find("730");
                        if (hasCs2) libs.insert(libs.begin(), w);
                        else libs.push_back(w);
                    }
        }
    }
    for (const auto& lib : libs) {
        std::wstring dir = joinPath(lib, L"steamapps\\common\\Counter-Strike Global Offensive");
        if (isCs2Dir(dir)) {
            info.cs2Dir = dir;
            break;
        }
    }
    return info;
}

}  // namespace fdw
