// Tests für "Echtes Wetter": Open-Meteo-Antworten auswerten und übersetzen (Linux, g++). Aufruf: ./weather_test
#include "../src/weathercore.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace wx;

static int g_checks = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++g_fail;
}
static std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {
        unsigned cp = (unsigned)c;
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }
    return s;
}
static std::string f(const char* fmt, double a, double b = 0, double c = 0) {
    char buf[256];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    return buf;
}

static std::string current(const char* body) {
    return std::string(
               "{\"latitude\":53.55,\"longitude\":10.0,\"generationtime_ms\":0.07,\"utc_offset_seconds\":3600,\"timezone\":\"Europe/Berlin\","
               "\"timezone_abbreviation\":\"GMT+1\",\"elevation\":8.0,\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","
               "\"temperature_2m\":\"°C\",\"precipitation\":\"mm\",\"rain\":\"mm\",\"showers\":\"mm\",\"snowfall\":\"cm\",\"weather_code\":\"wmo code\","
               "\"cloud_cover\":\"%\",\"wind_speed_10m\":\"km/h\",\"wind_direction_10m\":\"°\",\"wind_gusts_10m\":\"km/h\",\"is_day\":\"\"},"
               "\"current\":{") +
           body + "}}";
}

int main() {
    std::printf("Wetter auswerten\n");
    {
        WeatherNow w;
        bool ok = parseCurrent(current("\"time\":\"2026-11-06T17:45\",\"interval\":900,\"temperature_2m\":6.4,\"precipitation\":0.30,\"rain\":0.30,"
                                       "\"showers\":0.00,\"snowfall\":0.00,\"weather_code\":63,\"cloud_cover\":100,\"wind_speed_10m\":22.3,"
                                       "\"wind_direction_10m\":238,\"wind_gusts_10m\":44.6,\"is_day\":0"),
                               w);
        check(ok && w.valid, "Regen-Antwort gelesen");
        check(w.code == 63 && std::fabs(w.tempC - 6.4f) < 0.01f && std::fabs(w.windKmh - 22.3f) < 0.01f && !w.isDay, "Werte stimmen");
        check(w.time == "2026-11-06T17:45" && w.intervalSec == 900, "Zeit und Intervall");
        WeatherLook L = lookFor(w);
        check(L.rain > 0.9f && L.rain < 1.3f && L.snow == 0.0f && !L.drizzle, f("mäßiger Regen (%.2f)", L.rain));
        check(L.wind > 0.3f && L.wind < 0.6f, f("Wind aus Südwest weht nach rechts (%.2f)", L.wind));
        check(L.meltDipPerMin > 3.0f, f("Plusgrade + Regen: Tauwetter (%.1f DIP/min)", L.meltDipPerMin));
        check(!L.thunder && L.fog == 0.0f, "kein Gewitter, kein Nebel");
        std::string d = narrow(describeWeather(w));
        check(d == "6 °C · Regen · Wind 22 km/h aus Südwest", "Beschreibung: " + d);
    }
    {
        WeatherNow w;
        parseCurrent(current("\"time\":\"2026-12-01T08:00\",\"interval\":900,\"temperature_2m\":-1.5,\"precipitation\":0.25,\"rain\":0.0,"
                             "\"showers\":0.0,\"snowfall\":0.35,\"weather_code\":73,\"cloud_cover\":100,\"wind_speed_10m\":30,"
                             "\"wind_direction_10m\":45,\"wind_gusts_10m\":40,\"is_day\":1"),
                     w);
        WeatherLook L = lookFor(w);
        check(L.snow > 1.0f && L.snow < 1.4f && L.rain == 0.0f && !L.wetSnow, f("Schneefall (%.2f), bleibt liegen", L.snow));
        check(L.wind < -0.3f, f("Wind aus Nordost weht nach links (%.2f)", L.wind));
        check(L.meltDipPerMin == 0.0f, "Frost: nichts taut");
        check(narrow(describeWeather(w)) == "-2 °C · Schneefall · Wind 30 km/h aus Nordost" ||
                  narrow(describeWeather(w)) == "-1 °C · Schneefall · Wind 30 km/h aus Nordost",
              "Beschreibung: " + narrow(describeWeather(w)));
    }
    {
        WeatherNow w;
        parseCurrent(current("\"time\":\"x\",\"interval\":900,\"temperature_2m\":2.0,\"precipitation\":0,\"weather_code\":45,\"wind_speed_10m\":2,"
                             "\"wind_direction_10m\":180,\"is_day\":1,\"cloud_cover\":100"),
                     w);
        WeatherLook L = lookFor(w);
        check(L.fog > 0.5f && L.rain == 0.0f && L.snow == 0.0f, f("Nebel (%.2f)", L.fog));
        check(L.wind == 0.0f, "windstill");
        check(narrow(describeWeather(w)) == "2 °C · Nebel · windstill", "Beschreibung: " + narrow(describeWeather(w)));
    }
    {
        WeatherNow w;
        parseCurrent(current("\"interval\":900,\"temperature_2m\":9,\"precipitation\":0.1,\"weather_code\":3,\"wind_speed_10m\":10,"
                             "\"wind_direction_10m\":270"),
                     w);
        WeatherLook L = lookFor(w);
        check(L.rain > 0.0f && L.drizzle, f("Niederschlag trotz 'bedeckt': feiner Regen (%.2f)", L.rain));
    }
    {
        WeatherNow w;
        parseCurrent(current("\"interval\":900,\"temperature_2m\":18,\"precipitation\":2.0,\"weather_code\":95,\"wind_speed_10m\":35,"
                             "\"wind_direction_10m\":200,\"wind_gusts_10m\":80"),
                     w);
        WeatherLook L = lookFor(w);
        check(L.thunder && L.rain > 1.8f, f("Gewitter mit Starkregen (%.2f)", L.rain));
    }
    {
        WeatherNow w;
        parseCurrent(current("\"interval\":900,\"temperature_2m\":0.8,\"precipitation\":0.2,\"weather_code\":61,\"wind_speed_10m\":5,"
                             "\"wind_direction_10m\":90"),
                     w);
        WeatherLook L = lookFor(w);
        check(L.snow > 0.0f && L.rain > 0.0f && L.wetSnow, f("Schneeregen bei 0,8 °C (Schnee %.2f, Regen %.2f)", L.snow, L.rain));
    }
    {
        WeatherNow w;
        check(!parseCurrent(current("\"weather_code\":null"), w) && !w.valid, "fehlender Wettercode: ungültig");
        check(!parseCurrent("<html>Bad Gateway</html>", w), "kaputte Antwort: ungültig");
        check(!parseCurrent("{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90°.\"}", w), "Fehler-Antwort: ungültig");
        check(!lookFor(WeatherNow()).thunder && lookFor(WeatherNow()).snow == 0.0f, "ohne Daten fällt nichts");
    }

    std::printf("Ortssuche\n");
    {
        std::vector<Place> p;
        bool ok = parsePlaces(
            "{\"results\":[{\"id\":2911298,\"name\":\"Hamburg\",\"latitude\":53.55073,\"longitude\":9.99302,\"elevation\":6.0,"
            "\"feature_code\":\"PPLA\",\"country_code\":\"DE\",\"admin1_id\":2911297,\"timezone\":\"Europe/Berlin\",\"population\":1845229,"
            "\"postcodes\":[\"20038\",\"20095\"],\"country_id\":2921044,\"country\":\"Deutschland\",\"admin1\":\"Hamburg\"},"
            "{\"id\":4950065,\"name\":\"Hamburg\",\"latitude\":42.55,\"longitude\":-78.83,\"country\":\"Vereinigte Staaten\",\"admin1\":\"New York\"}],"
            "\"generationtime_ms\":0.6}",
            p);
        check(ok && p.size() == 2, f("zwei Treffer (%.0f)", (double)p.size()));
        if (p.size() == 2) {
            check(narrow(p[0].detail) == "Hamburg, Deutschland" && std::fabs(p[0].lat - 53.55073) < 1e-6, "Hamburg, Deutschland: " + narrow(p[0].detail));
            check(narrow(p[1].detail) == "Hamburg, New York, Vereinigte Staaten" && p[1].lon < -78, "zweiter Treffer: " + narrow(p[1].detail));
        }
        ok = parsePlaces("{\"results\":[{\"name\":\"M\\u00fcnchen\",\"latitude\":48.13,\"longitude\":11.57,\"country\":\"Deutschland\",\"admin1\":\"Bayern\"}]}", p);
        check(ok && p.size() == 1 && narrow(p[0].detail) == "München, Bayern, Deutschland", "Umlaute (\\u-Escape): " + (p.empty() ? "" : narrow(p[0].detail)));
        ok = parsePlaces("{\"results\":[{\"name\":\"Köln\",\"latitude\":50.9,\"longitude\":6.9,\"country\":\"Deutschland\",\"admin1\":\"Nordrhein-Westfalen\"}]}", p);
        check(ok && p.size() == 1 && narrow(p[0].name) == "Köln", "Umlaute (UTF-8): " + (p.empty() ? "" : narrow(p[0].name)));
        ok = parsePlaces("{\"generationtime_ms\":0.3}", p);
        check(ok && p.empty(), "kein Treffer");
        check(!parsePlaces("Bad Gateway", p), "kaputte Antwort");
        check(urlEncode(L"Köln am Rhein") == "K%C3%B6ln%20am%20Rhein", "Adresse kodiert: " + urlEncode(L"Köln am Rhein"));
    }

    std::printf("\n%d Prüfungen, %d Fehler\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
