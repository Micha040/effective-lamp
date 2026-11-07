// FragDesk – deutsche Anzeige von Zahlen, Namen und Highlights (UTF-8)
#pragma once
#include <cstdint>
#include <string>

#include "model.h"
#include "stats.h"

namespace fd {

std::string formatStat(double v, Fmt f);   // "–" bei NaN
std::string fmtInt(double v);              // 12.345
std::string fmtDec(double v, int decimals);   // 1,23
std::string fmtSigned(double v, int decimals);   // +3 / −2,5
std::string fmtPct(double v, int decimals);   // 45 %
std::string fmtSeconds(double v, int decimals);   // 12,3 s
std::string fmtMoney(double v);            // 4.350 $
std::string fmtRating(int rating);         // 21.595
std::string fmtDuration(double seconds);   // 43 min, 1 h 05 min
std::string fmtBytes(double bytes);        // 87 MB
std::string fmtClock(double seconds);      // 1:07

std::string mapName(const std::string& map);     // de_mirage -> Mirage
uint32_t mapColor(const std::string& map);       // 0xRRGGBB
std::string modeName(const std::string& mode);   // premier -> Premier
std::string sourceName(const std::string& source);
std::string econName(const std::string& econ);
std::string reasonName(const std::string& reason);
std::string sideName(const std::string& side);   // T / CT
std::string resultName(const std::string& result);

// Premier-Farben wie im Spiel
uint32_t premierColor(int rating);

std::string highlightTitle(const HighlightInfo& h);
std::string highlightTagName(const std::string& tag);

// "2026-11-06T21:13:00Z" -> Sekunden seit 1970 (UTC). false bei ungültigem Text.
bool parseIsoUtc(const std::string& iso, int64_t& epoch);

}  // namespace fd
