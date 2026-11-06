// SnowDesk – echtes Wetter, plattformunabhängiger Teil: Antworten von Open-Meteo auswerten und in Schnee/Regen/Nebel/
// Wind übersetzen. (Das Abrufen selbst steht in src/app/weather.cpp.)
#pragma once
#include <string>
#include <vector>

namespace wx {

struct WeatherNow {
    bool valid = false;
    float tempC = 0;
    float precipMm = 0, rainMm = 0, showersMm = 0, snowCm = 0;   // Summe im Abfrage-Intervall
    int intervalSec = 900;
    int code = 0;              // WMO-Wettercode
    float cloud = 0;           // Bewölkung in %
    float windKmh = 0, windDirDeg = 0, gustKmh = 0;   // Richtung: woher der Wind kommt (0 = Nord, 270 = West)
    bool isDay = true;
    std::string time;          // Ortszeit der Messung ("2026-11-06T17:45")
};

struct Place {
    std::wstring name;         // "Hamburg"
    std::wstring detail;       // "Hamburg, Deutschland"
    double lat = 0, lon = 0;
};

bool parseCurrent(const std::string& json, WeatherNow& out);
bool parsePlaces(const std::string& json, std::vector<Place>& out);

// Was SnowDesk daraus macht
struct WeatherLook {
    float snow = 0;            // Schneemenge (wie "Menge"), 0 = keiner
    float rain = 0;            // 0 = keiner, 0.3 Niesel, 1 mäßig, 2.5 Starkregen
    bool drizzle = false;
    bool wetSnow = false;      // Schnee bei Plusgraden: bleibt nicht liegen
    float wind = 0;            // -1..1 (positiv = nach rechts, also Wind nach Osten)
    float fog = 0;             // 0..1
    bool thunder = false;
    float meltDipPerMin = 0;   // Tauwetter
};
WeatherLook lookFor(const WeatherNow& w);

std::wstring conditionText(int code);                  // "leichter Regen"
std::wstring describeWeather(const WeatherNow& w);   // "4 °C · leichter Regen · Wind 22 km/h aus Südwest"
std::wstring utf8ToWide(const std::string& s);
std::string urlEncode(const std::wstring& s);        // UTF-8, %XX

}  // namespace wx
