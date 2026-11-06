// SnowDesk – echtes Wetter: auswerten und übersetzen (siehe weathercore.h)
#include "weathercore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wx {

namespace {

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// Das passende Objekt { … } ab der Position der öffnenden Klammer (Zeichenketten werden übersprungen)
bool objectSpan(const std::string& s, size_t open, size_t& close) {
    int depth = 0;
    bool str = false;
    for (size_t i = open; i < s.size(); ++i) {
        char c = s[i];
        if (str) {
            if (c == '\\') ++i;
            else if (c == '"') str = false;
            continue;
        }
        if (c == '"') str = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (--depth == 0) {
                close = i;
                return true;
            }
        }
    }
    return false;
}

// Position des Werts zu "key" innerhalb [from, to) – nur auf der obersten Ebene dieses Objekts
size_t valueOf(const std::string& s, size_t from, size_t to, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    int depth = 0;
    bool str = false;
    for (size_t i = from; i < to; ++i) {
        char c = s[i];
        if (str) {
            if (c == '\\') ++i;
            else if (c == '"') str = false;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            continue;
        }
        if (c != '"') continue;
        if (depth == 1 && s.compare(i, k.size(), k) == 0) {
            size_t j = i + k.size();
            while (j < to && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
            if (j < to && s[j] == ':') {
                ++j;
                while (j < to && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
                return j;
            }
        }
        str = true;   // irgendein anderer Schlüssel oder Text
    }
    return std::string::npos;
}

bool numberAt(const std::string& s, size_t pos, double& out) {
    if (pos == std::string::npos || pos >= s.size()) return false;
    if (s.compare(pos, 4, "null") == 0) return false;
    const char* b = s.c_str() + pos;
    char* e = nullptr;
    double v = std::strtod(b, &e);
    if (e == b || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool stringAt(const std::string& s, size_t pos, std::string& out) {
    if (pos == std::string::npos || pos >= s.size() || s[pos] != '"') return false;
    out.clear();
    for (size_t i = pos + 1; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i >= s.size()) return false;
        char e = s[i];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 >= s.size()) return false;
                unsigned cp = (unsigned)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                // Ersatzpaar (Emoji o. Ä.)
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                    unsigned lo = (unsigned)std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) {
                    out += (char)(0xC0 | (cp >> 6));
                    out += (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += (char)(0xE0 | (cp >> 12));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                } else {
                    out += (char)(0xF0 | (cp >> 18));
                    out += (char)(0x80 | ((cp >> 12) & 0x3F));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e; break;
        }
    }
    return false;
}

// Objekt zu "key" auf oberster Ebene finden: Anfang/Ende der Klammern
bool objectFor(const std::string& s, const char* key, size_t& open, size_t& close) {
    size_t root = s.find('{');
    size_t rootClose;
    if (root == std::string::npos || !objectSpan(s, root, rootClose)) return false;
    size_t v = valueOf(s, root, rootClose, key);
    if (v == std::string::npos || (s[v] != '{' && s[v] != '[')) return false;
    open = v;
    return objectSpan(s, open, close);
}

}  // namespace

std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        unsigned cp;
        int n;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c >> 5) == 6) {
            cp = c & 0x1F;
            n = 2;
        } else if ((c >> 4) == 14) {
            cp = c & 0x0F;
            n = 3;
        } else if ((c >> 3) == 30) {
            cp = c & 0x07;
            n = 4;
        } else {
            out += L'?';
            ++i;
            continue;
        }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += n;
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += (wchar_t)(0xD800 + (cp >> 10));
            out += (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out += (wchar_t)cp;
        }
    }
    return out;
}

std::string urlEncode(const std::wstring& w) {
    std::string utf8;
    for (size_t i = 0; i < w.size(); ++i) {
        unsigned cp = (unsigned)w[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)w[++i] - 0xDC00);
        }
        if (cp < 0x80) utf8 += (char)cp;
        else if (cp < 0x800) {
            utf8 += (char)(0xC0 | (cp >> 6));
            utf8 += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8 += (char)(0xE0 | (cp >> 12));
            utf8 += (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8 += (char)(0x80 | (cp & 0x3F));
        } else {
            utf8 += (char)(0xF0 | (cp >> 18));
            utf8 += (char)(0x80 | ((cp >> 12) & 0x3F));
            utf8 += (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8 += (char)(0x80 | (cp & 0x3F));
        }
    }
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool parseCurrent(const std::string& json, WeatherNow& out) {
    out = WeatherNow();
    size_t open, close;
    if (!objectFor(json, "current", open, close) || json[open] != '{') return false;
    auto num = [&](const char* key, double& v) { return numberAt(json, valueOf(json, open, close + 1, key), v); };
    double v;
    if (!num("weather_code", v)) return false;
    out.code = (int)std::lround(v);
    if (num("temperature_2m", v)) out.tempC = (float)v;
    if (num("precipitation", v)) out.precipMm = (float)std::max(0.0, v);
    if (num("rain", v)) out.rainMm = (float)std::max(0.0, v);
    if (num("showers", v)) out.showersMm = (float)std::max(0.0, v);
    if (num("snowfall", v)) out.snowCm = (float)std::max(0.0, v);
    if (num("cloud_cover", v)) out.cloud = (float)v;
    if (num("wind_speed_10m", v)) out.windKmh = (float)std::max(0.0, v);
    if (num("wind_direction_10m", v)) out.windDirDeg = (float)v;
    if (num("wind_gusts_10m", v)) out.gustKmh = (float)std::max(0.0, v);
    if (num("is_day", v)) out.isDay = v > 0.5;
    if (num("interval", v) && v >= 60) out.intervalSec = (int)v;
    std::string t;
    if (stringAt(json, valueOf(json, open, close + 1, "time"), t)) out.time = t;
    out.valid = true;
    return true;
}

bool parsePlaces(const std::string& json, std::vector<Place>& out) {
    out.clear();
    size_t open, close;
    if (!objectFor(json, "results", open, close)) return json.find('{') != std::string::npos;   // keine Treffer
    if (json[open] != '[') return false;
    size_t i = open + 1;
    while (i < close) {
        size_t o = json.find('{', i);
        if (o == std::string::npos || o > close) break;
        size_t c;
        if (!objectSpan(json, o, c)) break;
        Place p;
        std::string name, country, admin;
        double lat, lon;
        if (stringAt(json, valueOf(json, o, c + 1, "name"), name) && numberAt(json, valueOf(json, o, c + 1, "latitude"), lat) &&
            numberAt(json, valueOf(json, o, c + 1, "longitude"), lon)) {
            stringAt(json, valueOf(json, o, c + 1, "country"), country);
            stringAt(json, valueOf(json, o, c + 1, "admin1"), admin);
            p.name = utf8ToWide(name);
            std::string d = name;
            if (!admin.empty() && admin != name) d += ", " + admin;
            if (!country.empty()) d += ", " + country;
            p.detail = utf8ToWide(d);
            p.lat = lat;
            p.lon = lon;
            out.push_back(p);
        }
        i = c + 1;
    }
    return true;
}

// ---------------------------------------------------------------- Übersetzen

WeatherLook lookFor(const WeatherNow& w) {
    WeatherLook L;
    if (!w.valid) return L;
    const int c = w.code;
    const float t = w.tempC;
    const float perHour = 3600.0f / (float)std::max(60, w.intervalSec);
    const float mmh = w.precipMm * perHour;   // Niederschlag mm/h
    // Schnee
    if (c == 71) L.snow = 0.45f;
    else if (c == 73) L.snow = 1.0f;
    else if (c == 75) L.snow = 2.2f;
    else if (c == 77) L.snow = 0.35f;   // Schneegriesel
    else if (c == 85) L.snow = 0.8f;
    else if (c == 86) L.snow = 1.9f;
    // Regen (und Niesel)
    if (c == 51) L.rain = 0.25f, L.drizzle = true;
    else if (c == 53) L.rain = 0.4f, L.drizzle = true;
    else if (c == 55) L.rain = 0.6f, L.drizzle = true;
    else if (c == 56) L.rain = 0.3f, L.drizzle = true;
    else if (c == 57) L.rain = 0.6f, L.drizzle = true;
    else if (c == 61) L.rain = 0.6f;
    else if (c == 63) L.rain = 1.2f;
    else if (c == 65) L.rain = 2.2f;
    else if (c == 66) L.rain = 0.8f;
    else if (c == 67) L.rain = 1.8f;
    else if (c == 80) L.rain = 0.8f;
    else if (c == 81) L.rain = 1.5f;
    else if (c == 82) L.rain = 2.6f;
    else if (c >= 95 && c <= 99) {
        L.rain = c >= 97 ? 2.6f : 1.8f;
        L.thunder = true;
    }
    // Feinabstimmung mit der gemessenen Menge (Code allein ist grob)
    if (L.rain > 0.0f && mmh > 0.0f) L.rain = clampf(L.rain * clampf(0.75f + 0.12f * mmh, 0.75f, 1.4f), 0.15f, 3.0f);
    if (L.snow > 0.0f && w.snowCm > 0.0f) L.snow = clampf(L.snow * clampf(0.75f + 0.25f * w.snowCm * perHour, 0.75f, 1.4f), 0.3f, 2.6f);
    // Niederschlag gemeldet, Code sagt trotzdem nichts: nach Temperatur
    if (L.rain <= 0.0f && L.snow <= 0.0f && c < 50 && mmh >= 0.2f) {
        if (t <= 1.0f) L.snow = 0.4f;
        else {
            L.rain = 0.4f;
            L.drizzle = mmh < 0.6f;
        }
    }
    // Schneeregen: Regen knapp über null mischt sich mit nassem Schnee
    if (L.rain > 0.0f && !L.drizzle && t <= 1.5f && t > -2.0f && L.snow <= 0.0f) {
        L.snow = L.rain * 0.5f;
        L.wetSnow = true;
        L.rain *= 0.6f;
    }
    if (L.snow > 0.0f && t >= 2.0f) L.wetSnow = true;
    // Nebel
    if (c == 45) L.fog = 0.7f;
    else if (c == 48) L.fog = 0.85f;
    // Wind: Stärke nach km/h, Richtung = Ost-West-Anteil (Wind aus Westen weht nach rechts)
    const float s = std::pow(clampf((w.windKmh - 3.0f) / 42.0f, 0.0f, 1.0f), 0.85f);
    if (s > 0.02f) {
        const float rad = w.windDirDeg * 3.14159265f / 180.0f;
        float east = -std::sin(rad);   // Anteil nach Osten
        float sign = east >= 0.0f ? 1.0f : -1.0f;
        L.wind = sign * s * std::max(std::fabs(east), 0.45f);   // auch Nord-/Südwind treibt ein wenig seitlich
        if (w.gustKmh > w.windKmh * 1.6f) L.wind *= 1.1f;
        L.wind = clampf(L.wind, -1.0f, 1.0f);
    }
    // Tauwetter: Plusgrade lassen liegenden Schnee schmelzen, Regen noch schneller
    if (t > 0.5f) {
        L.meltDipPerMin = (t - 0.5f) * 0.6f;
        if (L.rain > 0.0f && !L.wetSnow) L.meltDipPerMin += 1.5f * L.rain;
        if (w.isDay && w.cloud < 50.0f) L.meltDipPerMin += 0.8f;   // Sonne
        L.meltDipPerMin = clampf(L.meltDipPerMin, 0.0f, 12.0f);
    }
    return L;
}

std::wstring conditionText(int c) {
    switch (c) {
        case 0: return L"klar";
        case 1: return L"überwiegend klar";
        case 2: return L"teils bewölkt";
        case 3: return L"bedeckt";
        case 45: return L"Nebel";
        case 48: return L"Nebel mit Reif";
        case 51: return L"leichter Nieselregen";
        case 53: return L"Nieselregen";
        case 55: return L"starker Nieselregen";
        case 56: return L"gefrierender Nieselregen";
        case 57: return L"starker gefrierender Nieselregen";
        case 61: return L"leichter Regen";
        case 63: return L"Regen";
        case 65: return L"starker Regen";
        case 66: return L"gefrierender Regen";
        case 67: return L"starker gefrierender Regen";
        case 71: return L"leichter Schneefall";
        case 73: return L"Schneefall";
        case 75: return L"starker Schneefall";
        case 77: return L"Schneegriesel";
        case 80: return L"leichte Regenschauer";
        case 81: return L"Regenschauer";
        case 82: return L"heftige Regenschauer";
        case 85: return L"leichte Schneeschauer";
        case 86: return L"starke Schneeschauer";
        case 95: return L"Gewitter";
        case 96: return L"Gewitter mit Hagel";
        case 97: return L"schweres Gewitter";
        case 99: return L"Gewitter mit starkem Hagel";
        default: return L"Wettercode " + std::to_wstring(c);
    }
}

std::wstring describeWeather(const WeatherNow& w) {
    if (!w.valid) return L"noch keine Wetterdaten";
    static const wchar_t* dirs[8] = {L"Nord", L"Nordost", L"Ost", L"Südost", L"Süd", L"Südwest", L"West", L"Nordwest"};
    wchar_t buf[200];
    int temp = (int)std::lround(w.tempC);
    std::wstring s = std::to_wstring(temp) + L" °C · " + conditionText(w.code);
    int kmh = (int)std::lround(w.windKmh);
    if (kmh >= 3) {
        int d = (int)std::lround(std::fmod(w.windDirDeg + 360.0, 360.0) / 45.0) % 8;
        std::swprintf(buf, 199, L" · Wind %d km/h aus %ls", kmh, dirs[d]);
        s += buf;
    } else {
        s += L" · windstill";
    }
    return s;
}

}  // namespace wx
