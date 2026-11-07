// FragDesk – Anzeige (siehe format.h)
#include "format.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fd {

namespace {

const char* kNbsp = "\xC2\xA0";      // geschütztes Leerzeichen
const char* kMinus = "\xE2\x88\x92";  // −
const char* kDash = "\xE2\x80\x93";   // –

std::string group(long long n) {
    bool neg = n < 0;
    unsigned long long u = neg ? 0ULL - (unsigned long long)n : (unsigned long long)n;
    std::string digits = std::to_string(u);
    std::string out;
    int cnt = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        out.insert(out.begin(), digits[i]);
        if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), '.');
    }
    return neg ? std::string(kMinus) + out : out;
}

bool bad(double v) { return std::isnan(v) || std::isinf(v); }

}  // namespace

std::string fmtInt(double v) {
    if (bad(v)) return kDash;
    return group((long long)std::llround(v));
}

std::string fmtDec(double v, int decimals) {
    if (bad(v)) return kDash;
    if (decimals <= 0) return fmtInt(v);
    if (decimals > 6) decimals = 6;
    double scale = std::pow(10.0, decimals);
    long long scaled = std::llround(std::fabs(v) * scale);
    long long whole = scaled / (long long)scale;
    long long frac = scaled % (long long)scale;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*lld", decimals, frac);
    std::string s = group(whole) + "," + buf;
    if (v < 0 && scaled != 0) s = std::string(kMinus) + s;
    return s;
}

std::string fmtSigned(double v, int decimals) {
    if (bad(v)) return kDash;
    std::string s = fmtDec(std::fabs(v), decimals);
    double scale = std::pow(10.0, decimals);
    if (std::llround(std::fabs(v) * scale) == 0) return s;
    return (v > 0 ? "+" : kMinus) + s;
}

std::string fmtPct(double v, int decimals) {
    if (bad(v)) return kDash;
    return fmtDec(v, decimals) + kNbsp + "%";
}

std::string fmtSeconds(double v, int decimals) {
    if (bad(v)) return kDash;
    return fmtDec(v, decimals) + kNbsp + "s";
}

std::string fmtMoney(double v) {
    if (bad(v)) return kDash;
    return fmtInt(v) + kNbsp + "$";
}

std::string fmtRating(int rating) { return rating > 0 ? group(rating) : std::string(kDash); }

std::string fmtDuration(double seconds) {
    if (bad(seconds) || seconds < 0) return kDash;
    long long min = std::llround(seconds / 60);
    if (min < 60) return std::to_string(min) + kNbsp + "min";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld%sh%s%02lld%smin", min / 60, kNbsp, kNbsp, min % 60, kNbsp);
    return buf;
}

std::string fmtBytes(double bytes) {
    if (bad(bytes) || bytes < 0) return kDash;
    if (bytes < 1024 * 1024) return fmtInt(std::ceil(bytes / 1024)) + kNbsp + "KB";
    if (bytes < 1024.0 * 1024 * 1024) return fmtInt(bytes / (1024 * 1024)) + kNbsp + "MB";
    return fmtDec(bytes / (1024.0 * 1024 * 1024), 1) + kNbsp + "GB";
}

std::string fmtClock(double seconds) {
    if (bad(seconds) || seconds < 0) seconds = 0;
    long long s = std::llround(seconds);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
    return buf;
}

std::string formatStat(double v, Fmt f) {
    switch (f) {
        case Fmt::Int: return fmtInt(v);
        case Fmt::Dec1: return fmtDec(v, 1);
        case Fmt::Dec2: return fmtDec(v, 2);
        case Fmt::Pct0: return fmtPct(v, 0);
        case Fmt::Pct1: return fmtPct(v, 1);
        case Fmt::Sec0: return fmtSeconds(v, 0);
        case Fmt::Sec1: return fmtSeconds(v, 1);
        case Fmt::Meter: return bad(v) ? std::string(kDash) : fmtDec(v, 1) + kNbsp + "m";
        case Fmt::Money: return fmtMoney(v);
        case Fmt::Signed: return fmtSigned(v, 0);
        case Fmt::Signed1: return fmtSigned(v, 1);
    }
    return fmtDec(v, 2);
}

// ---- Namen ----------------------------------------------------------------------------------------------------
namespace {

struct MapEntry {
    const char* id;
    const char* name;
    uint32_t color;
};

const MapEntry kMaps[] = {
    {"de_mirage", "Mirage", 0xD9A35B},   {"de_inferno", "Inferno", 0xD8644E}, {"de_nuke", "Nuke", 0x3FA7D6},
    {"de_ancient", "Ancient", 0x4CAF7A}, {"de_anubis", "Anubis", 0xC9A227},   {"de_dust2", "Dust II", 0xE2BF7A},
    {"de_train", "Train", 0x8C9BAB},     {"de_overpass", "Overpass", 0x7E8CE0}, {"de_vertigo", "Vertigo", 0x5BC0EB},
    {"de_office", "Office", 0xA7A7A7},   {"cs_italy", "Italy", 0xE07A5F},     {"cs_office", "Office", 0xA7A7A7},
    {"de_basalt", "Basalt", 0x9C7B6B},   {"de_edin", "Edin", 0x6FA88E},       {"cs_agency", "Agency", 0x8E8EC9},
    {"de_palais", "Palais", 0xC98FB4},   {"de_whistle", "Whistle", 0x7FB3A6}, {"de_thera", "Thera", 0x6DA6D9},
    {"de_mills", "Mills", 0xB59A6D},     {"de_grail", "Grail", 0xB88A5A},     {"de_jura", "Jura", 0x7AA36B},
    {"de_brewery", "Brewery", 0xC9A061}, {"de_dogtown", "Dogtown", 0xA88370},
};

const uint32_t kPalette[] = {0x6C8EF5, 0xE07A5F, 0x4CAF7A, 0xC9A227, 0x9B7EDE, 0x3FA7D6, 0xD98FB4, 0x7FB3A6};

}  // namespace

std::string mapName(const std::string& map) {
    for (const auto& m : kMaps)
        if (map == m.id) return m.name;
    std::string s = map;
    for (const char* prefix : {"de_", "cs_", "ar_", "dz_", "gd_"})
        if (s.rfind(prefix, 0) == 0) {
            s = s.substr(3);
            break;
        }
    if (s.empty()) return kDash;
    s[0] = char(std::toupper((unsigned char)s[0]));
    return s;
}

uint32_t mapColor(const std::string& map) {
    for (const auto& m : kMaps)
        if (map == m.id) return m.color;
    uint32_t h = 2166136261u;
    for (unsigned char c : map) h = (h ^ c) * 16777619u;
    return kPalette[h % (sizeof kPalette / sizeof kPalette[0])];
}

std::string modeName(const std::string& mode) {
    if (mode == "premier") return "Premier";
    if (mode == "competitive") return "Wettkampf";
    if (mode == "wingman") return "Wingman";
    if (mode == "faceit") return "FACEIT";
    return "Sonstige";
}

std::string sourceName(const std::string& source) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"valve", "Valve"},         {"faceit", "FACEIT"},     {"esea", "ESEA"},           {"esl", "ESL"},
        {"perfectworld", "Perfect World"}, {"5eplay", "5EPlay"}, {"ebot", "eBot"},       {"cevo", "CEVO"},
        {"challengermode", "Challengermode"}, {"esportal", "Esportal"}, {"fastcup", "FASTCUP"}, {"gamersclub", "Gamers Club"},
        {"matchzy", "MatchZy"},     {"popflash", "PopFlash"}, {"renown", "Renown"},       {"unknown", "Unbekannt"},
    };
    for (const auto& n : kNames)
        if (source == n.first) return n.second;
    if (source.empty()) return kDash;
    std::string s = source;
    s[0] = char(std::toupper((unsigned char)s[0]));
    return s;
}

std::string econName(const std::string& econ) {
    if (econ == "pistol") return "Pistolenrunde";
    if (econ == "eco") return "Eco";
    if (econ == "semi") return "Halb-Eco";
    if (econ == "force") return "Force-Buy";
    if (econ == "full") return "Voller Kauf";
    return kDash;
}

std::string reasonName(const std::string& reason) {
    if (reason == "bomb") return "Bombe explodiert";
    if (reason == "defuse") return "Bombe entschärft";
    if (reason == "elimination") return "Alle ausgeschaltet";
    if (reason == "time") return "Zeit abgelaufen";
    if (reason == "surrender") return "Aufgegeben";
    return "Rundenende";
}

std::string sideName(const std::string& side) {
    if (side == "T") return "T";
    if (side == "CT") return "CT";
    return kDash;
}

std::string resultName(const std::string& result) {
    if (result == "win") return "Sieg";
    if (result == "loss") return "Niederlage";
    return "Unentschieden";
}

uint32_t premierColor(int r) {
    if (r <= 0) return 0x8A94A6;
    if (r < 5000) return 0xB0C3D9;
    if (r < 10000) return 0x5E98D9;
    if (r < 15000) return 0x4B69FF;
    if (r < 20000) return 0x8847FF;
    if (r < 25000) return 0xD32CE6;
    if (r < 30000) return 0xEB4B4B;
    return 0xFFD700;
}

// ---- Highlights -----------------------------------------------------------------------------------------------
std::string highlightTagName(const std::string& tag) {
    if (tag == "ace") return "Ass";
    if (tag == "4k") return "4 Kills";
    if (tag == "3k") return "3 Kills";
    if (tag == "2k") return "Doppelkill";
    if (tag == "clutch") return "Clutch";
    if (tag == "noscope") return "Noscope";
    if (tag == "wallbang") return "Wallbang";
    if (tag == "smoke") return "durch Smoke";
    if (tag == "blind") return "geblendet";
    if (tag == "air") return "im Sprung";
    if (tag == "knife") return "Messer";
    if (tag == "zeus") return "Zeus";
    if (tag == "long") return "weiter Schuss";
    if (tag == "collateral") return "Kollateral";
    if (tag == "quick") return "blitzschnell";
    if (tag == "headshots") return "nur Kopfschüsse";
    return tag;
}

std::string highlightTitle(const HighlightInfo& h) {
    std::string main;
    if (h.clutch > 0) {
        main = "1v" + std::to_string(h.clutch) + "-Clutch";
        if (h.kills >= 2) main += " mit " + std::to_string(h.kills) + " Kills";
    } else if (h.kills >= 5) main = "Ass";
    else if (h.kills == 4) main = "4 Kills";
    else if (h.kills == 3) main = "3 Kills";
    else if (h.kills == 2) main = "Doppelkill";
    else main = "Kill";
    std::vector<std::string> extra;
    for (const auto& t : h.tags) {
        if (t == "ace" || t == "4k" || t == "3k" || t == "2k" || t == "clutch") continue;
        extra.push_back(highlightTagName(t));
    }
    if (h.kills == 1 && !extra.empty() && h.clutch == 0) {
        // Einzelner besonderer Kill: die Besonderheit ist der Titel
        std::string first = extra.front();
        if (!first.empty()) first[0] = char(std::toupper((unsigned char)first[0]));
        main = first == "Messer" ? "Messer-Kill" : first == "Zeus" ? "Zeus-Kill" : first;
        extra.erase(extra.begin());
    }
    std::string s = main;
    for (size_t i = 0; i < extra.size(); ++i) s += (i == 0 ? " \xC2\xB7 " : ", ") + extra[i];
    return s;
}

// ---- Datum ----------------------------------------------------------------------------------------------------
namespace {

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

}  // namespace

bool parseIsoUtc(const std::string& iso, int64_t& epoch) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return false;
    int64_t t = daysFromCivil(y, unsigned(mo), unsigned(d)) * 86400 + h * 3600 + mi * 60 + s;
    // Zeitzone am Ende: Z oder ±hh:mm
    size_t pos = iso.find_first_of("Z+-", 19);
    if (pos != std::string::npos && iso[pos] != 'Z') {
        int oh = 0, om = 0;
        if (std::sscanf(iso.c_str() + pos + 1, "%2d:%2d", &oh, &om) >= 1) {
            int64_t off = oh * 3600 + om * 60;
            t += iso[pos] == '+' ? -off : off;
        }
    }
    epoch = t;
    return true;
}

}  // namespace fd
