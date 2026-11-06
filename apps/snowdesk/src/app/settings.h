// SnowDesk – Einstellungen (settings.ini im Datenordner + Spiegel in HKCU\Software\SnowDesk für das Kontextmenü)
#pragma once
#include "common.h"

#include "../snowcore.h"

namespace app {

enum Preset : int { PresetLight = 0, PresetMedium = 1, PresetStorm = 2, PresetCustom = 3 };
enum RenderMode : int { RenderBehind = 0, RenderAlt = 1, RenderFront = 2 };   // hinter Symbolen / Alternative / vor Symbolen

struct Settings {
    bool enabled = true;
    int preset = PresetMedium;
    float amount = 1.0f;        // Schneemenge
    float wind = 0.3f;          // 0..1
    float flakeSize = 1.0f;     // 0.5..2
    float fallSpeed = 1.0f;     // 0.5..2
    float maxPile = 32.0f;      // DIP, 0 = nichts bleibt liegen (32 ≈ eine Titelleiste)
    float fillMinutes = 6.0f;   // bei Menge 1
    float crystals = 0.07f;     // Anteil Kristalle
    int release = snow::ReleaseMix;
    bool mouseBlow = false;
    int fps = 60;               // 30, 60 oder 0 = Bildwiederholrate des Monitors
    int render = RenderBehind;
    bool pauseGames = true;
    std::wstring games;         // durch ; getrennt
    bool autostart = true;
    // Echtes Wetter: was fällt, Wind, Nebel, Tauen kommen vom Wetter am Ort (Open-Meteo)
    bool realWeather = false;
    std::wstring place = L"Hamburg";
    std::wstring placeDetail = L"Hamburg, Deutschland";
    double lat = 53.5507, lon = 9.9930;

    void applyPreset(int p);          // setzt Menge + Wind passend zur Stufe
    int matchingPreset() const;       // welche Stufe passt zu Menge/Wind (sonst PresetCustom)
    snow::Params params() const;
    std::vector<std::wstring> gameList() const;   // klein geschrieben, ohne Leerzeichen
    void clampAll();
};

const wchar_t* presetName(int preset);
std::wstring defaultGames();

Settings loadSettings();
bool saveSettings(const Settings& s);
void mirrorToRegistry(const Settings& s, bool running);
bool setAutostart(bool on);           // HKCU\...\Run
bool autostartRegistered();
std::wstring settingsPath();

}  // namespace app
