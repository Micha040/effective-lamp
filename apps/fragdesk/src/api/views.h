// FragDesk – Ansichten der Handy-App: JSON mit fertig formatierten Texten (genau wie in FragDesk) plus Rohwerten
// für Diagramme und Sortierung. Jede Ansicht läuft im Thread der Daten (Source).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../core/format.h"
#include "../core/stats.h"
#include "jsonw.h"
#include "mobile.h"

namespace fdapi {

struct ViewCtx {
    Source& src;
    const Request& req;
    Host& host;
    std::string me;   // eigene SteamID
};

// ---- Ansichten (je eine Route) ----------------------------------------------------------------------------------
void viewInfo(ViewCtx& c, Response& res);       // GET /api/v1/info
void viewOverview(ViewCtx& c, Response& res);   // GET /api/v1/overview?mode=&period=&map=
void viewMatches(ViewCtx& c, Response& res);    // GET /api/v1/matches
void viewMatch(ViewCtx& c, Response& res, const std::string& checksum);   // GET /api/v1/match/<Prüfsumme>?player=
void viewStats(ViewCtx& c, Response& res);      // GET /api/v1/stats?mode=&period=&map=&tab=
void viewMoments(ViewCtx& c, Response& res);    // GET /api/v1/moments

// Heatmaps (view_heat.cpp): GET /api/v1/heat?match=<Prüfsumme> bzw. ?mode=&period=&map=&pick= plus Auswahl. Eigene
// Route statt einer Ansicht: Nur das Zusammenstellen läuft über Host::withSource, Dateien liest und rechnet der Thread
// der Anfrage (nie der UI-Thread von FragDesk).
void heatRoute(Host& host, const Request& req, Response& res);

// ---- Gemeinsame Helfer ------------------------------------------------------------------------------------------
std::string hexColor(uint32_t rgb);   // 0xRRGGBB -> "#rrggbb"
// Farbton eines Ratings wie in FragDesk: "good" (≥ 1,10), "bad" (< 0,90), "muted" (keins), sonst ""
const char* ratingTone(double rating);
// Ergebnis als Ton: "win", "loss", "draw" (sonst "")
const char* resultTone(const std::string& result);

int64_t matchEpoch(const fd::Match& m);
std::string dateText(int64_t epoch);       // 06.11.2026
std::string dateTimeText(int64_t epoch);   // 06.11.2026, 21:13
std::string relativeDay(int64_t epoch);    // Heute, 21:13 / Gestern, 21:13 / 06.11., 21:13 / 06.11.2025
std::string matchTitle(const fd::Match& m, const std::string& me);   // "Mirage · 13:11" (Team von me zuerst)

// Filter wie in FragDesk: mode 0–5 (Alle, Premier, FACEIT, Wettkampf, Wingman, Sonstige), period 0–2, map
fd::Filter filterFrom(const Request& req);
const std::vector<std::string>& modeLabels();
const std::vector<std::string>& periodLabels();
const char* modeKey(int index);   // "", "premier", "faceit", "competitive", "wingman", "other"

// Karriere für den Filter (zwischengespeichert, solange sich die Matches nicht ändern)
const fd::Career& careerFor(Source& src, const std::string& me, const fd::Filter& f);

std::string statText(const char* id, const fd::Counters& c);   // formatiert, "–" ohne Wert
double statNumber(const char* id, const fd::Counters& c);       // NaN ohne Wert
// "T 1,30 · CT 0,96" bzw. bei Kennzahlen fürs ganze Match "23 Siege · 17 Niederlagen"
std::string sideLine(const char* id, const fd::Career& car);

// Kennzahl als Objekt: {"id","label","value","t","ct","hint","better","v"}
void writeStat(JsonW& j, const fd::Stat& s, const fd::Counters& all, const fd::Counters* t, const fd::Counters* ct);

// Radarbild einer Karte für Heatmap und 2D-Ansicht als JSON-Wert (nach j.key(…)):
//   {"name", "image", "lowerImage" (nur mit unterer Ebene), "posX", "posY", "span", "lowerZ"} – null ohne Radarbild.
// Umrechnung wie fd::radarUV: u = (x − posX) / span, v = (posY − y) / span (0..1 im Bild); untere Ebene, wenn
// lowerImage da ist und z < lowerZ. Die Bild-Adressen tragen ?v=<Version> (lange im Browser-Speicher).
void writeRadar(JsonW& j, const std::string& map, const std::string& version);

}  // namespace fdapi
