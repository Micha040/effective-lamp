// DeskCats – Einstellungen (siehe settings.h)
#include "settings.h"

#include <algorithm>
#include <cwctype>

namespace app {

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

DWORD readDword(HKEY k, const wchar_t* name, DWORD fallback) {
    DWORD v = fallback, size = sizeof(v);
    if (RegGetValueW(k, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return fallback;
    return v;
}

std::wstring readString(HKEY k, const wchar_t* name, const std::wstring& fallback) {
    DWORD size = 0;
    if (RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t)) return fallback;
    std::wstring s(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, &s[0], &size) != ERROR_SUCCESS) return fallback;
    while (!s.empty() && s.back() == L'\0') s.pop_back();
    return s;
}

void writeDword(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)); }
void writeString(HKEY k, const wchar_t* name, const std::wstring& s) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(s.c_str()), (DWORD)((s.size() + 1) * sizeof(wchar_t)));
}

}  // namespace

bool Settings::operator==(const Settings& o) const {
    return fatos == o.fatos && kosta == o.kosta && size == o.size && activity == o.activity && onWindows == o.onWindows &&
           onTaskbar == o.onTaskbar && allMonitors == o.allMonitors && visible == o.visible && games == o.games;
}

std::vector<std::wstring> defaultGames() {
    return {L"cs2.exe", L"VALORANT-Win64-Shipping.exe", L"r5apex.exe", L"FortniteClient-Win64-Shipping.exe", L"RainbowSix.exe",
            L"Overwatch.exe", L"League of Legends.exe", L"dota2.exe", L"RocketLeague.exe"};
}

std::wstring joinGames(const std::vector<std::wstring>& g) {
    std::wstring s;
    for (size_t i = 0; i < g.size(); ++i) {
        if (i) s += L"; ";
        s += g[i];
    }
    return s;
}

std::vector<std::wstring> splitGames(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&] {
        size_t a = 0, b = cur.size();
        while (a < b && iswspace(cur[a])) ++a;
        while (b > a && iswspace(cur[b - 1])) --b;
        std::wstring t = cur.substr(a, b - a);
        if (!t.empty()) {
            if (t.size() < 4 || _wcsicmp(t.c_str() + t.size() - 4, L".exe") != 0) t += L".exe";
            bool dup = false;
            for (const auto& o : out) dup = dup || _wcsicmp(o.c_str(), t.c_str()) == 0;
            if (!dup) out.push_back(t);
        }
        cur.clear();
    };
    for (wchar_t c : s) {
        if (c == L';' || c == L',' || c == L'\n' || c == L'\r') flush();
        else cur += c;
    }
    flush();
    return out;
}

Settings loadSettings() {
    Settings s;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        s.games = defaultGames();
        return s;
    }
    s.fatos = readDword(k, L"Fatos", 1) != 0;
    s.kosta = readDword(k, L"Kosta", 1) != 0;
    s.size = (int)std::max<DWORD>(2, std::min<DWORD>(5, readDword(k, L"Size", 3)));
    s.activity = (int)std::min<DWORD>(2, readDword(k, L"Activity", 1));
    s.onWindows = readDword(k, L"OnWindows", 1) != 0;
    s.onTaskbar = readDword(k, L"OnTaskbar", 1) != 0;
    s.allMonitors = readDword(k, L"AllMonitors", 1) != 0;
    s.visible = readDword(k, L"Visible", 1) != 0;
    std::wstring g = readString(k, L"Games", L"\x01");
    s.games = g == L"\x01" ? defaultGames() : splitGames(g);
    RegCloseKey(k);
    if (!s.fatos && !s.kosta) s.fatos = true;
    return s;
}

void saveSettings(const Settings& s) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        logLine(L"Einstellungen speichern fehlgeschlagen");
        return;
    }
    writeDword(k, L"Fatos", s.fatos);
    writeDword(k, L"Kosta", s.kosta);
    writeDword(k, L"Size", (DWORD)s.size);
    writeDword(k, L"Activity", (DWORD)s.activity);
    writeDword(k, L"OnWindows", s.onWindows);
    writeDword(k, L"OnTaskbar", s.onTaskbar);
    writeDword(k, L"AllMonitors", s.allMonitors);
    writeDword(k, L"Visible", s.visible);
    writeString(k, L"Games", joinGames(s.games));
    RegCloseKey(k);
}

bool autostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD size = 0;
    bool on = RegGetValueW(k, nullptr, L"DeskCats", RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

void setAutostart(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) writeString(k, L"DeskCats", L"\"" + exePath() + L"\" --autostart");
    else RegDeleteValueW(k, L"DeskCats");
    RegCloseKey(k);
}

void publishMenuState(bool visible, bool sleeping) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    writeDword(k, L"Visible", visible);
    writeDword(k, L"Sleeping", sleeping);
    RegCloseKey(k);
}

}  // namespace app
