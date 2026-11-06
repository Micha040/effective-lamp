// SnowDesk – echtes Wetter abrufen (Open-Meteo, kostenlos, ohne Konto). Nur solange "Echtes Wetter" an ist und
// SnowDesk nicht pausiert: alle 15 Minuten eine kleine HTTPS-Anfrage, in einem kurzen eigenen Thread; das Ergebnis
// kommt als Fensternachricht zurück. Auswerten/Übersetzen: src/weathercore.cpp.
#pragma once
#include "common.h"

#include "../weathercore.h"

namespace app {

constexpr UINT WM_APP_WEATHER = WM_APP + 20;   // lParam: WeatherReply* (der Empfänger löscht es)

struct WeatherReply {
    int kind = 0;              // 0 Wetter, 1 Ortssuche
    unsigned id = 0;
    bool ok = false;
    std::wstring error;
    wx::WeatherNow weather;
    std::vector<wx::Place> places;
};

class WeatherClient {
public:
    void setTarget(HWND hwnd) { hwnd_ = hwnd; }
    bool busy() const;                       // läuft gerade eine Anfrage?
    unsigned fetchWeather(double lat, double lon);
    unsigned searchPlace(const std::wstring& name);

private:
    unsigned start(int kind, const std::wstring& path);
    HWND hwnd_ = nullptr;
    unsigned next_ = 1;
};

}  // namespace app
