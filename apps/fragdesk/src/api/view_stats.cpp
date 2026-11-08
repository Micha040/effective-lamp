// FragDesk – Handy-App: Statistiken (wie page_stats.cpp). Gebaut wird nur der gewählte Tab (tab=0–10 wie in FragDesk;
// Tab 11, die Heatmaps, liefert view_heat.cpp). Ein Tab ist eine Liste von Blöcken, die die App der Reihe nach zeigt:
//   group  – Kennzahlen einer Gruppe mit Gesamt, als T und als CT (wie statTable)
//   table  – Tabelle (Waffen, Maps): Spalten, Zellen mit Text und Rohwert zum Sortieren, Sortierung wie in FragDesk
//   bars   – Balkenliste (wie barList)
//   chart  – Verlauf über die Matches (wie lineChart); die Angaben zu den Matches stehen einmal in "matches"
//   figure – Treffer nach Körperzonen (wie hitFigure)
//   notice – Hinweis (ältere Auswertungen)
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "views.h"

namespace fdapi {

namespace {

double V(const char* id, const fd::Counters& c) { return fd::statValue(id, c); }

// ---- Bausteine ----------------------------------------------------------------------------------------------------
void group(JsonW& j, const char* id, const fd::Career& car, const char* title = nullptr) {
    const fd::StatGroup* g = fd::findGroup(id);
    if (!g) return;
    j.obj().kv("kind", "group").kv("id", g->id).kv("title", title ? title : g->title);
    j.key("stats").arr();
    for (const char* sid : g->stats)
        if (const fd::Stat* s = fd::findStat(sid)) writeStat(j, *s, car.all, &car.t, &car.ct);
    j.end().end();
}

struct Col {
    const char* label;   // Spaltenkopf (die App setzt ihn in Großbuchstaben wie FragDesk)
    const char* tip;     // Erklärung (in FragDesk der Tooltip am Spaltenkopf)
};

template <size_t N>
void cols(JsonW& j, const Col (&c)[N]) {
    j.key("cols").arr();
    for (const Col& col : c) j.obj().kv("label", col.label).kv("tip", col.tip).end();
    j.end();
}

// Zelle mit Namen (fett, ohne Rohwert: wird nach Text sortiert), optional mit Farbkästchen davor
void nameCell(JsonW& j, const std::string& text, const std::string& dot = std::string()) {
    j.obj().kv("text", text).kb("b", true);
    if (!dot.empty()) j.kv("dot", dot);
    j.end();
}

// Zelle mit Zahl: fertiger Text plus Rohwert (null ohne Wert – solche Zellen stehen beim Sortieren hinten)
void numCell(JsonW& j, const std::string& text, double v, const char* tone = "", bool bold = false) {
    j.obj().kv("text", text).kn("v", v);
    if (*tone) j.kv("tone", tone);
    if (bold) j.kb("b", true);
    j.end();
}

void statCell(JsonW& j, const char* id, const fd::Counters& c) { numCell(j, statText(id, c), statNumber(id, c)); }

// Balken: Anteil an maxV wie barList
void bar(JsonW& j, const std::string& label, const std::string& sub, const std::string& value, double v, double maxV) {
    double frac = maxV > 0 && v > 0 ? std::min(1.0, v / maxV) : 0;
    j.obj().kv("label", label);
    if (!sub.empty()) j.kv("sub", sub);
    j.kv("value", value).kn("frac", frac).end();
}

// ---- Tabs ---------------------------------------------------------------------------------------------------------
void weaponsTab(JsonW& j, const fd::Career& car) {
    static const Col kCols[] = {
        {"Waffe", ""},
        {"Kills", ""},
        {"Anteil", "Anteil an allen Kills"},
        {"HS", "Headshot-Quote"},
        {"Schaden", ""},
        {"Schüsse", ""},
        {"Treffer", "Treffsicherheit"},
        {"1. Schuss", "Treffsicherheit des ersten Schusses"},
        {"Spray", "Treffsicherheit ab der 4. Kugel"},
        {"Bewegt", "Anteil der Schüsse in Bewegung"},
        {"Kopf", "Kopftreffer unter allen Treffern"},
        {"Ø Dist.", ""},
        {"WB", "Wallbang-Kills"},
        {"EK", "Eröffnungskills"},
    };
    j.obj().kv("kind", "table").kv("id", "weapons").kv("tap", "details");
    cols(j, kCols);
    j.key("sort").obj().ki("col", 1).kb("desc", true).end();
    double totalKills = car.all.get("kills");
    j.key("rows").arr();
    for (const auto& kv : car.weapons) {
        const fd::Counters& cn = kv.second.second;
        if (cn.get("kills") == 0 && cn.get("shots") == 0) continue;
        j.obj().kv("key", kv.first).key("cells").arr();
        nameCell(j, kv.first);
        statCell(j, "kills", cn);
        double share = totalKills > 0 ? 100 * cn.get("kills") / totalKills : NAN;
        numCell(j, fd::formatStat(share, fd::Fmt::Pct0), share);
        statCell(j, "hsPct", cn);
        numCell(j, fd::fmtInt(cn.get("dmg")), cn.get("dmg"));
        for (const char* id : {"shots", "accuracy", "firstShotAcc", "sprayAcc", "movingPct", "headHitPct", "avgDist", "kWallbang", "openKills"})
            statCell(j, id, cn);
        j.end().end();
    }
    j.end();
    j.kv("note", "Tippe auf eine Waffe für alle Werte, auf einen Spaltenkopf zum Sortieren.").end();

    group(j, "classes", car, "Kills nach Waffenart");

    // Gestorben durch: die acht häufigsten
    std::vector<std::pair<int, std::string>> d;
    for (const auto& kv : car.deathsBy) d.push_back({kv.second, kv.first});
    std::sort(d.rbegin(), d.rend());
    size_t n = std::min<size_t>(d.size(), 8);
    double total = car.all.get("deaths"), mx = d.empty() ? 1 : d[0].first;
    j.obj().kv("kind", "bars").kv("id", "deathsBy").kv("title", "Gestorben durch").kv("tone", "bad").key("items").arr();
    for (size_t i = 0; i < n; ++i)
        bar(j, d[i].second, "", std::to_string(d[i].first) + "× · " + fd::fmtPct(total > 0 ? 100.0 * d[i].first / total : 0, 0), d[i].first, mx);
    j.end().end();
}

void mapsTab(JsonW& j, const fd::Career& car, const std::string& mapFilter) {
    static const Col kCols[] = {
        {"Map", ""},
        {"Matches", ""},
        {"Siege", ""},
        {"Siegquote", ""},
        {"Rating", ""},
        {"K/D", ""},
        {"ADR", ""},
        {"KAST", ""},
        {"HS", ""},
        {"T-Runden", "Runden als T gewonnen"},
        {"CT-Runden", "Runden als CT gewonnen"},
        {"EK : ET", "Eröffnungskills : Eröffnungstode"},
    };
    j.obj().kv("kind", "table").kv("id", "maps").kv("tap", "map");
    cols(j, kCols);
    j.key("sort").obj().ki("col", 1).kb("desc", true).end();
    j.key("rows").arr();
    for (const auto& kv : car.maps) {
        const fd::GroupAgg& g = kv.second;
        j.obj().kv("key", kv.first).kv("map", kv.first).kb("on", mapFilter == kv.first).key("cells").arr();
        nameCell(j, fd::mapName(kv.first), hexColor(fd::mapColor(kv.first)));
        numCell(j, std::to_string(g.matches), g.matches);
        numCell(j, std::to_string(g.wins), g.wins);
        double wr = g.matches ? 100.0 * g.wins / g.matches : NAN;
        numCell(j, fd::formatStat(wr, fd::Fmt::Pct0), wr, wr >= 55 ? "good" : wr < 45 ? "bad" : "", true);
        double r = V("rating", g.all);
        numCell(j, fd::formatStat(r, fd::Fmt::Dec2), r, ratingTone(r));
        for (const char* id : {"kd", "adr", "kast", "hsPct"}) statCell(j, id, g.all);
        statCell(j, "roundWinrate", g.t);
        statCell(j, "roundWinrate", g.ct);
        double ok = g.all.get("openKills"), od = g.all.get("openDeaths");
        numCell(j, fd::fmtInt(ok) + " : " + fd::fmtInt(od), ok - od);
        j.end().end();
    }
    j.end();
    j.kv("note", mapFilter.empty() ? std::string("Tippen auf eine Map filtert alle Statistiken danach.")
                                   : "Gefiltert nach " + fd::mapName(mapFilter) + " – noch mal tippen hebt den Filter auf.");
    j.end();

    j.obj().kv("kind", "bars").kv("id", "modes").kv("title", "Siegquote nach Modus").key("items").arr();
    for (const auto& kv : car.modes) {
        const fd::GroupAgg& g = kv.second;
        double v = g.matches ? 100.0 * g.wins / g.matches : 0;
        bar(j, fd::modeName(kv.first), std::to_string(g.matches) + " Matches · Rating " + fd::formatStat(V("rating", g.all), fd::Fmt::Dec2),
            fd::fmtPct(v, 0), v, 100);
    }
    j.end().end();
}

void clutchTab(JsonW& j, const fd::Career& car) {
    j.obj().kv("kind", "bars").kv("id", "clutchLevels").kv("title", "Clutches nach Lage").key("items").arr();
    for (int n = 1; n <= 5; ++n) {
        double att = car.all.get(("clutch" + std::to_string(n)).c_str()), won = car.all.get(("clutchWon" + std::to_string(n)).c_str());
        double v = att > 0 ? 100 * won / att : 0;
        bar(j, "1 gegen " + std::to_string(n), fd::fmtInt(won) + " von " + fd::fmtInt(att) + " gewonnen", att > 0 ? fd::fmtPct(v, 0) : "–", v, 100);
    }
    j.end().end();
    group(j, "clutch", car);
}

void aimTab(JsonW& j, const fd::Career& car) {
    // Statt „Neu auswerten“ (nur am PC) ein Hinweis – wie reanalyzeNote, über die Matches der Auswahl
    int old = 0;
    for (const auto& p : car.points)
        if (p.match && p.match->schema < fd::kMatchSchema) ++old;   // wie App::outdated
    if (old > 0)
        j.obj()
            .kv("kind", "notice")
            .kv("tone", "info")
            .kv("title", old == 1 ? std::string("Ein Match wurde mit einer älteren FragDesk-Version ausgewertet")
                                  : std::to_string(old) + " Matches wurden mit einer älteren FragDesk-Version ausgewertet")
            .kv("text", "Ihnen fehlen die Werte für Crosshair-Placement und Reaktionszeit – in FragDesk am PC neu auswerten.")
            .end();
    group(j, "crosshair", car);
    group(j, "aim", car);
    struct Zone {
        const char* id;
        const char* label;
        const char* stat;
    };
    static const Zone kZones[] = {
        {"head", "Kopf", "headHitPct"},       {"neck", "Hals", "hitNeckPct"}, {"chest", "Brust", "hitChestPct"},
        {"stomach", "Bauch", "hitStomachPct"}, {"arms", "Arme", "hitArmsPct"}, {"legs", "Beine", "hitLegsPct"},
    };
    j.obj().kv("kind", "figure").kv("id", "hits").kv("title", "Treffer nach Körperzonen").key("zones").arr();
    for (const Zone& z : kZones) {
        double v = V(z.stat, car.all);
        j.obj().kv("id", z.id).kv("label", z.label).kv("value", fd::fmtPct(v, 1)).kn("v", v).end();
    }
    j.end().end();
    group(j, "hitzones", car);
}

// Verlauf: Punkte verweisen mit "i" auf die Matches der Auswahl (car.points), die einmal mitkommen
void trendTab(JsonW& j, const fd::Career& car) {
    // Premier zuerst, falls vorhanden (wie premierPoints)
    bool premier = false;
    for (const auto& p : car.points)
        if (p.premierNew > 0 || p.premierOld > 0) premier = true;
    if (premier) {
        j.obj().kv("kind", "chart").kv("id", "premier").kv("title", "Premier-Rating").kb("area", false);
        j.key("fmt").obj().ki("dec", 0).kv("unit", "").end();
        j.key("points").arr();
        for (size_t i = 0; i < car.points.size(); ++i) {
            const fd::MatchPoint& p = car.points[i];
            if (p.premierNew <= 0 && p.premierOld <= 0) continue;
            int v = p.premierNew > 0 ? p.premierNew : p.premierOld;
            std::string change;
            if (p.premierOld > 0 && p.premierNew > 0) change = " (" + fd::fmtSigned(p.premierNew - p.premierOld, 0) + ")";
            j.obj().ki("i", int64_t(i)).ki("y", v).kv("value", fd::fmtRating(v) + change).kv("color", hexColor(fd::premierColor(v))).end();
        }
        j.end();
        j.key("bands").arr();
        for (int b : {0, 5000, 10000, 15000, 20000, 25000, 30000}) j.obj().ki("from", b).kv("color", hexColor(fd::premierColor(b + 1))).end();
        j.end().end();
    }

    struct Trend {
        const char* id;
        const char* title;
        const char* sub;
        double (*val)(const fd::MatchPoint&);
        std::string (*fmt)(double);
        int dec;            // Nachkommastellen der Achse (wie fmt)
        const char* unit;   // hinter den Zahlen der Achse
        double ref;         // Bezugslinie, NaN = keine
    };
    static const Trend kCharts[] = {
        {"rating", "Rating 2.0", "pro Match, gestrichelt: Schnitt der letzten 5", [](const fd::MatchPoint& p) { return p.rating2; },
         [](double v) { return fd::fmtDec(v, 2); }, 2, "", 1.0},
        {"adr", "ADR", "Schaden pro Runde", [](const fd::MatchPoint& p) { return p.adr; }, [](double v) { return fd::fmtDec(v, 0); }, 0, "", NAN},
        {"kd", "K/D", "Kills pro Tod", [](const fd::MatchPoint& p) { return p.kd; }, [](double v) { return fd::fmtDec(v, 1); }, 1, "", 1.0},
        {"hsPct", "Headshot-Quote", "in Prozent", [](const fd::MatchPoint& p) { return p.hsPct; }, [](double v) { return fd::fmtDec(v, 0); }, 0, "",
         NAN},
        {"kast", "KAST", "in Prozent", [](const fd::MatchPoint& p) { return p.kast; }, [](double v) { return fd::fmtDec(v, 0); }, 0, "", NAN},
        {"crosshair", "Crosshair-Placement", "Ø Grad zum Kopf beim Auftauchen, kleiner ist besser", [](const fd::MatchPoint& p) { return p.crosshair; },
         [](double v) { return fd::fmtDeg(v, 1); }, 1, "°", NAN},
        {"reaction", "Reaktionszeit", "Ø ms bis zum ersten Treffer, kleiner ist besser", [](const fd::MatchPoint& p) { return p.reaction; },
         [](double v) { return fd::fmtMs(v); }, 0, "\xC2\xA0ms", NAN},
    };
    for (const Trend& t : kCharts) {
        j.obj().kv("kind", "chart").kv("id", t.id).kv("title", t.title).kv("sub", t.sub).kb("area", true).kn("ref", t.ref).ki("avg", 5);
        j.key("fmt").obj().ki("dec", t.dec).kv("unit", t.unit).end();
        bool any = false;
        j.key("points").arr();
        for (size_t i = 0; i < car.points.size(); ++i) {
            double y = t.val(car.points[i]);
            if (std::isnan(y)) continue;   // ältere Auswertung ohne diesen Wert
            j.obj().ki("i", int64_t(i)).kn("y", y).kv("value", t.fmt(y)).end();
            any = true;
        }
        j.end();
        if (!any) j.kv("empty", "Noch keine Werte – die Matches wurden mit einer älteren Version ausgewertet (in FragDesk am PC neu auswerten).");
        j.end();
    }
}

int tabFrom(const Request& req) {
    std::string t = req.param("tab");
    if (t.empty() || t.size() > 2 || !std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return 0;
    int n = std::stoi(t);
    return n <= 10 ? n : 0;
}

}  // namespace

void viewStats(ViewCtx& c, Response& res) {
    int tab = tabFrom(c.req);
    fd::Filter f = filterFrom(c.req);
    const fd::Career& car = careerFor(c.src, c.me, f);

    JsonW j;
    j.obj().kv("title", "Statistiken").ki("tab", tab);
    std::string sub;
    if (car.matches)
        sub = (car.name.empty() ? std::string() : car.name + " · ") + std::to_string(car.matches) + (car.matches == 1 ? " Match · " : " Matches · ") +
              fd::fmtInt(car.all.get("rounds")) + " Runden";
    else sub = c.src.loaded() ? "Noch keine Daten für diese Auswahl" : "Bibliothek wird geladen …";
    j.kv("sub", sub);
    if (car.matches == 0) {
        j.key("empty")
            .obj()
            .kv("title", "Keine passenden Matches")
            .kv("text", c.me.empty() ? "Wähle in FragDesk unter Einstellungen dein Konto aus." : "Ändere den Filter oder werte weitere Demos aus.")
            .end();
        j.key("blocks").arr().end();
        res.body = j.end().take();
        return;
    }
    j.key("empty").null();
    j.kv("foot", "Werte als T und CT beziehen sich auf die Runden auf der jeweiligen Seite. Tippe auf eine Kennzahl für eine Erklärung.");

    j.key("blocks").arr();
    switch (tab) {
        case 0:
            group(j, "overview", car);
            group(j, "multi", car);
            break;
        case 1: weaponsTab(j, car); break;
        case 2: mapsTab(j, car, f.map); break;
        case 3: group(j, "opening", car); break;
        case 4: clutchTab(j, car); break;
        case 5: group(j, "utility", car); break;
        case 6: aimTab(j, car); break;
        case 7: group(j, "special", car); break;
        case 8: group(j, "damage", car); break;
        case 9:
            group(j, "economy", car);
            group(j, "bomb", car);
            break;
        default: trendTab(j, car); break;
    }
    j.end();

    if (tab == 10) {   // Angaben zu den Matches für die Tipps im Verlauf (Text wie in FragDesk: Wert, Match · Ergebnis, Datum)
        j.key("matches").arr();
        for (const auto& p : car.points) {
            int64_t t = matchEpoch(*p.match);
            j.obj()
                .kv("id", p.match->checksum)
                .kv("label", dateText(t).substr(0, 6))
                .kv("info", matchTitle(*p.match, c.me) + " · " + fd::resultName(p.me->result) + "\n" + dateTimeText(t))
                .end();
        }
        j.end();
    }
    res.body = j.end().take();
}

}  // namespace fdapi
