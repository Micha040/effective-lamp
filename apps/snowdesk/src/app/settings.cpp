// SnowDesk – Einstellungen laden/speichern
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace app {

namespace {

const wchar_t* kSection = L"SnowDesk";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"SnowDesk";

struct PresetValues {
    float amount, wind;
};
const PresetValues kPresets[3] = {{0.45f, 0.2f}, {1.0f, 0.3f}, {2.6f, 0.75f}};

float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

std::wstring readString(const std::wstring& file, const wchar_t* key, const wchar_t* def) {
    std::vector<wchar_t> buf(4096);
    DWORD n = GetPrivateProfileStringW(kSection, key, def, buf.data(), (DWORD)buf.size(), file.c_str());
    return std::wstring(buf.data(), n);
}

float readFloat(const std::wstring& file, const wchar_t* key, float def) {
    std::wstring s = readString(file, key, L"");
    if (s.empty()) return def;
    for (wchar_t& c : s) {
        if (c == L',') c = L'.';   // falls jemand die Datei von Hand mit Komma bearbeitet
    }
    wchar_t* end = nullptr;
    double v = wcstod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v)) return def;
    return (float)v;
}

int readInt(const std::wstring& file, const wchar_t* key, int def) {
    std::wstring s = readString(file, key, L"");
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 10);
    if (end == s.c_str()) return def;
    return (int)v;
}

bool readBool(const std::wstring& file, const wchar_t* key, bool def) { return readInt(file, key, def ? 1 : 0) != 0; }

bool writeString(const std::wstring& file, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(kSection, key, value.c_str(), file.c_str()) != FALSE;
}

std::wstring fmtFloat(float v) {
    wchar_t buf[64];
    _snwprintf(buf, 63, L"%.3f", (double)v);
    buf[63] = 0;
    // Punkt als Dezimaltrenner erzwingen (falls eine Locale aktiv wäre)
    for (wchar_t* p = buf; *p; ++p) {
        if (*p == L',') *p = L'.';
    }
    return buf;
}

std::wstring fmtInt(int v) {
    wchar_t buf[32];
    _snwprintf(buf, 31, L"%d", v);
    buf[31] = 0;
    return buf;
}

// Neue INI-Datei als UTF-16 anlegen, damit Write/GetPrivateProfileString Unicode erhalten
void ensureUnicodeFile(const std::wstring& file) {
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    WriteFile(h, bom, 2, &written, nullptr);
    CloseHandle(h);
}

}  // namespace

const wchar_t* presetName(int preset) {
    switch (preset) {
        case PresetLight: return L"Leicht";
        case PresetMedium: return L"Mittel";
        case PresetStorm: return L"Schneesturm";
        default: return L"Eigene";
    }
}

std::wstring defaultGames() {
    return L"cs2.exe;csgo.exe;valorant-win64-shipping.exe;fortniteclient-win64-shipping.exe;r5apex.exe;r5apex_dx12.exe;"
           L"league of legends.exe;dota2.exe;overwatch.exe;rocketleague.exe;rainbowsix.exe;rainbowsix_be.exe;cod.exe;"
           L"tslgame.exe;escapefromtarkov.exe;gta5.exe;gta5_enhanced.exe;eldenring.exe;marvel-win64-shipping.exe;"
           L"discovery.exe;deadlock.exe;project8.exe";
}

void Settings::applyPreset(int p) {
    if (p < PresetLight || p > PresetStorm) {
        preset = PresetCustom;
        return;
    }
    preset = p;
    amount = kPresets[p].amount;
    wind = kPresets[p].wind;
}

int Settings::matchingPreset() const {
    for (int p = 0; p < 3; ++p) {
        if (std::fabs(amount - kPresets[p].amount) < 0.01f && std::fabs(wind - kPresets[p].wind) < 0.01f) return p;
    }
    return PresetCustom;
}

void Settings::clampAll() {
    amount = clampf(amount, 0.05f, 6.0f);
    wind = clampf(wind, 0.0f, 1.0f);
    flakeSize = clampf(flakeSize, 0.4f, 2.5f);
    fallSpeed = clampf(fallSpeed, 0.3f, 2.5f);
    maxPile = clampf(std::round(maxPile), 0.0f, 150.0f);
    fillMinutes = clampf(fillMinutes, 0.5f, 60.0f);
    crystals = clampf(crystals, 0.0f, 0.5f);
    if (release < snow::ReleaseMix || release > snow::ReleaseGentle) release = snow::ReleaseMix;
    if (fps != 30 && fps != 60 && fps != 0) fps = 60;
    if (render < RenderBehind || render > RenderFront) render = RenderBehind;
    if (preset < PresetLight || preset > PresetCustom) preset = matchingPreset();
}

snow::Params Settings::params() const {
    snow::Params p;
    p.amount = amount;
    p.flakeSize = flakeSize;
    p.fallSpeed = fallSpeed;
    p.wind = wind;
    p.maxPileDip = maxPile;
    p.fillMinutes = fillMinutes;
    p.crystalShare = crystals;
    p.releaseStyle = release;
    p.mouseBlow = mouseBlow;
    return p;
}

std::vector<std::wstring> Settings::gameList() const {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&]() {
        // Leerzeichen am Rand weg, klein schreiben
        size_t a = 0, b = cur.size();
        while (a < b && iswspace(cur[a])) ++a;
        while (b > a && iswspace(cur[b - 1])) --b;
        std::wstring t = cur.substr(a, b - a);
        cur.clear();
        if (t.empty()) return;
        // nur der Dateiname zählt (falls ein Pfad eingetragen wurde)
        size_t slash = t.find_last_of(L"\\/");
        if (slash != std::wstring::npos) t = t.substr(slash + 1);
        CharLowerBuffW(&t[0], (DWORD)t.size());
        if (t.size() < 4 || t.compare(t.size() - 4, 4, L".exe") != 0) t += L".exe";
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    };
    for (wchar_t c : games) {
        if (c == L';' || c == L'\n' || c == L'\r' || c == L',') flush();
        else cur += c;
    }
    flush();
    return out;
}

std::wstring settingsPath() { return dataDir() + L"settings.ini"; }

Settings loadSettings() {
    Settings s;
    std::wstring f = settingsPath();
    bool exists = GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES;
    s.games = defaultGames();
    if (!exists) return s;
    s.enabled = readBool(f, L"enabled", s.enabled);
    s.amount = readFloat(f, L"amount", s.amount);
    s.wind = readFloat(f, L"wind", s.wind);
    s.flakeSize = readFloat(f, L"size", s.flakeSize);
    s.fallSpeed = readFloat(f, L"speed", s.fallSpeed);
    s.maxPile = readFloat(f, L"maxPile", s.maxPile);
    s.fillMinutes = readFloat(f, L"fillMinutes", s.fillMinutes);
    s.crystals = readFloat(f, L"crystals", s.crystals);
    s.release = readInt(f, L"release", s.release);
    s.mouseBlow = readBool(f, L"mouse", s.mouseBlow);
    s.fps = readInt(f, L"fps", s.fps);
    s.render = readInt(f, L"render", s.render);
    s.pauseGames = readBool(f, L"pauseGames", s.pauseGames);
    std::wstring g = readString(f, L"games", L"\x01");
    if (g != L"\x01") s.games = g;
    s.autostart = readBool(f, L"autostart", s.autostart);
    s.realWeather = readBool(f, L"realWeather", s.realWeather);
    std::wstring place = readString(f, L"place", L"");
    float lat = readFloat(f, L"lat", 1000.0f), lon = readFloat(f, L"lon", 1000.0f);
    if (!place.empty() && lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
        s.place = place;
        s.placeDetail = readString(f, L"placeDetail", place.c_str());
        s.lat = lat;
        s.lon = lon;
    }
    s.preset = readInt(f, L"preset", -1);
    s.clampAll();
    if (s.preset != PresetCustom && s.preset != s.matchingPreset()) s.preset = s.matchingPreset();
    return s;
}

bool saveSettings(const Settings& s) {
    std::wstring f = settingsPath();
    ensureUnicodeFile(f);
    bool ok = true;
    ok &= writeString(f, L"enabled", s.enabled ? L"1" : L"0");
    ok &= writeString(f, L"preset", fmtInt(s.preset));
    ok &= writeString(f, L"amount", fmtFloat(s.amount));
    ok &= writeString(f, L"wind", fmtFloat(s.wind));
    ok &= writeString(f, L"size", fmtFloat(s.flakeSize));
    ok &= writeString(f, L"speed", fmtFloat(s.fallSpeed));
    ok &= writeString(f, L"maxPile", fmtFloat(s.maxPile));
    ok &= writeString(f, L"fillMinutes", fmtFloat(s.fillMinutes));
    ok &= writeString(f, L"crystals", fmtFloat(s.crystals));
    ok &= writeString(f, L"release", fmtInt(s.release));
    ok &= writeString(f, L"mouse", s.mouseBlow ? L"1" : L"0");
    ok &= writeString(f, L"fps", fmtInt(s.fps));
    ok &= writeString(f, L"render", fmtInt(s.render));
    ok &= writeString(f, L"pauseGames", s.pauseGames ? L"1" : L"0");
    ok &= writeString(f, L"games", s.games);
    ok &= writeString(f, L"autostart", s.autostart ? L"1" : L"0");
    ok &= writeString(f, L"realWeather", s.realWeather ? L"1" : L"0");
    ok &= writeString(f, L"place", s.place);
    ok &= writeString(f, L"placeDetail", s.placeDetail);
    {
        wchar_t buf[48];
        _snwprintf(buf, 47, L"%.5f", s.lat);
        buf[47] = 0;
        for (wchar_t* q = buf; *q; ++q)
            if (*q == L',') *q = L'.';
        ok &= writeString(f, L"lat", buf);
        _snwprintf(buf, 47, L"%.5f", s.lon);
        buf[47] = 0;
        for (wchar_t* q = buf; *q; ++q)
            if (*q == L',') *q = L'.';
        ok &= writeString(f, L"lon", buf);
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, f.c_str());   // Cache leeren
    return ok;
}

void mirrorToRegistry(const Settings& s, bool running) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    DWORD enabled = (s.enabled && running) ? 1 : 0;
    DWORD preset = (DWORD)s.preset;
    DWORD real = s.realWeather ? 1 : 0;
    RegSetValueExW(key, L"Enabled", 0, REG_DWORD, (const BYTE*)&enabled, sizeof(enabled));
    RegSetValueExW(key, L"Preset", 0, REG_DWORD, (const BYTE*)&preset, sizeof(preset));
    RegSetValueExW(key, L"RealWeather", 0, REG_DWORD, (const BYTE*)&real, sizeof(real));
    std::wstring place = s.place.substr(0, 60);
    RegSetValueExW(key, L"Place", 0, REG_SZ, (const BYTE*)place.c_str(), (DWORD)((place.size() + 1) * sizeof(wchar_t)));
    std::wstring exe = exePath();
    RegSetValueExW(key, L"Exe", 0, REG_SZ, (const BYTE*)exe.c_str(), (DWORD)((exe.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

bool setAutostart(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG rc;
    if (on) {
        std::wstring cmd = L"\"" + exePath() + L"\" --autostart";
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool autostartRegistered() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_SZ;
}

}  // namespace app
