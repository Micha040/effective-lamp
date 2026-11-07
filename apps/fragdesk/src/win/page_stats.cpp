// FragDesk – Seite „Statistiken“: alle Kennzahlen über die ausgewählten Matches
#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }
double V(const char* id, const fd::Counters& c) { return fd::statValue(id, c); }

float groupCard(Canvas& c, float x, float y, float w, const char* group, const fd::Career& car, const std::wstring& title = L"") {
    const fd::StatGroup* g = fd::findGroup(group);
    if (!g) return 0;
    float h = 28 + 34 * g->stats.size() + 60;
    c.card(RectF(x, y, w, h));
    c.text(title.empty() ? W(g->title) : title, F::H3, c.th.text, RectF(x + 18, y + 14, w - 36, 22));
    statTable(c, x + 6, y + 46, w - 12, g->stats, car.all, car.t, car.ct, true, group);
    return h;
}

// zwei Gruppen nebeneinander (oder untereinander, wenn schmal)
float twoGroups(Canvas& c, float x, float y, float w, const char* a, const char* b, const fd::Career& car) {
    if (w < 980) {
        float h1 = groupCard(c, x, y, w, a, car);
        float h2 = groupCard(c, x, y + h1 + 16, w, b, car);
        return h1 + 16 + h2;
    }
    float cw = (w - 16) / 2;
    float h1 = groupCard(c, x, y, cw, a, car);
    float h2 = groupCard(c, x + cw + 16, y, cw, b, car);
    return std::max(h1, h2);
}

float weaponsTab(Canvas& c, App& app, const fd::Career& car, float x, float y, float w) {
    float y0 = y;
    std::vector<Col> cols = {
        {L"WAFFE", -2},
        {L"KILLS", 60, Align::Right},
        {L"ANTEIL", 64, Align::Right, L"Anteil an allen Kills"},
        {L"HS", 54, Align::Right, L"Headshot-Quote"},
        {L"SCHADEN", 76, Align::Right},
        {L"SCHÜSSE", 72, Align::Right},
        {L"TREFFER", 72, Align::Right, L"Treffsicherheit"},
        {L"1. SCHUSS", 78, Align::Right, L"Treffsicherheit des ersten Schusses"},
        {L"SPRAY", 64, Align::Right, L"Treffsicherheit ab der 4. Kugel"},
        {L"BEWEGT", 70, Align::Right, L"Anteil der Schüsse in Bewegung"},
        {L"KOPF", 60, Align::Right, L"Kopftreffer unter allen Treffern"},
        {L"Ø DIST.", 68, Align::Right},
        {L"WB", 44, Align::Right, L"Wallbang-Kills"},
        {L"EK", 44, Align::Right, L"Eröffnungskills"},
    };
    double totalKills = car.all.get("kills");
    std::vector<Row> rows;
    for (const auto& kv : car.weapons) {
        const fd::Counters& cn = kv.second.second;
        if (cn.get("kills") == 0 && cn.get("shots") == 0) continue;
        Row row;
        Cell n(W(kv.first));
        n.font = F::BodySemi;
        row.cells.push_back(n);
        auto st = [&](const char* id) {
            const fd::Stat* s = fd::findStat(id);
            double v = s->expr.eval(cn);
            return Cell(W(fd::formatStat(v, s->def->fmt)), v);
        };
        row.cells.push_back(st("kills"));
        double share = totalKills > 0 ? 100 * cn.get("kills") / totalKills : NAN;
        row.cells.push_back(Cell(W(fd::formatStat(share, fd::Fmt::Pct0)), share));
        row.cells.push_back(st("hsPct"));
        row.cells.push_back(Cell(W(fd::fmtInt(cn.get("dmg"))), cn.get("dmg")));
        row.cells.push_back(st("shots"));
        row.cells.push_back(st("accuracy"));
        row.cells.push_back(st("firstShotAcc"));
        row.cells.push_back(st("sprayAcc"));
        row.cells.push_back(st("movingPct"));
        row.cells.push_back(st("headHitPct"));
        row.cells.push_back(st("avgDist"));
        row.cells.push_back(st("kWallbang"));
        row.cells.push_back(st("openKills"));
        rows.push_back(row);
    }
    SortState& s = app.sort("cweapons", 1, true);
    sortRows(rows, s.col, s.desc);
    float th = 34 + std::max<size_t>(rows.size(), 1) * 44 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "cweapons";
    o.rowH = 44;
    o.sortCol = s.col;
    o.sortDesc = s.desc;
    o.onSort = [&app](int col) {
        SortState& st = app.sort("cweapons", 1, true);
        if (st.col == col) st.desc = !st.desc;
        else {
            st.col = col;
            st.desc = col != 0;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 20;

    // Waffenarten und gestorben durch
    float cw = w >= 980 ? (w - 16) / 2 : w;
    float h = 0;
    {
        float hh = groupCard(c, x, y, cw, "classes", car, L"Kills nach Waffenart");
        h = hh;
    }
    {
        float bx = w >= 980 ? x + cw + 16 : x, by = w >= 980 ? y : y + h + 16;
        std::vector<std::pair<int, std::string>> d;
        for (const auto& kv : car.deathsBy) d.push_back({kv.second, kv.first});
        std::sort(d.rbegin(), d.rend());
        size_t n = std::min<size_t>(d.size(), 8);
        float hh = 60 + std::max<size_t>(n, 1) * 38 + 10;
        c.card(RectF(bx, by, cw, hh));
        c.text(L"Gestorben durch", F::H3, c.th.text, RectF(bx + 18, by + 14, cw - 36, 22));
        std::vector<Bar> bars;
        double total = car.all.get("deaths");
        for (size_t i = 0; i < n; ++i) {
            Bar b;
            b.label = W(d[i].second);
            b.v = d[i].first;
            b.value = std::to_wstring(d[i].first) + L"× · " + W(fd::fmtPct(total > 0 ? 100.0 * d[i].first / total : 0, 0));
            b.color = c.th.danger;
            bars.push_back(b);
        }
        barList(c, bx + 18, by + 52, cw - 36, bars, d.empty() ? 1 : d[0].first, 38, 140);
        h = w >= 980 ? std::max(h, hh) : h + 16 + hh;
    }
    y += h + 10;
    return y - y0;
}

float mapsTab(Canvas& c, App& app, const fd::Career& car, float x, float y, float w) {
    float y0 = y;
    std::vector<Col> cols = {
        {L"MAP", -2},
        {L"MATCHES", 88, Align::Right},
        {L"SIEGE", 64, Align::Right},
        {L"SIEGQUOTE", 96, Align::Right},
        {L"RATING", 76, Align::Right},
        {L"K/D", 60, Align::Right},
        {L"ADR", 64, Align::Right},
        {L"KAST", 76, Align::Right},
        {L"HS", 54, Align::Right},
        {L"T-RUNDEN", 86, Align::Right, L"Runden als T gewonnen"},
        {L"CT-RUNDEN", 90, Align::Right, L"Runden als CT gewonnen"},
        {L"EK : ET", 76, Align::Right, L"Eröffnungskills : Eröffnungstode"},
    };
    std::vector<Row> rows;
    for (const auto& kv : car.maps) {
        const fd::GroupAgg& g = kv.second;
        Row row;
        Cell n(W(fd::mapName(kv.first)));
        n.dot = rgb(fd::mapColor(kv.first));
        n.font = F::BodySemi;
        row.cells.push_back(n);
        row.cells.push_back(Cell(std::to_wstring(g.matches), g.matches));
        row.cells.push_back(Cell(std::to_wstring(g.wins), g.wins));
        double wr = g.matches ? 100.0 * g.wins / g.matches : NAN;
        Cell wc(W(fd::formatStat(wr, fd::Fmt::Pct0)), wr);
        wc.color = wr >= 55 ? c.th.success : wr < 45 ? c.th.danger : c.th.text;
        wc.font = F::BodySemi;
        row.cells.push_back(wc);
        double r = V("rating", g.all);
        Cell rc(W(fd::formatStat(r, fd::Fmt::Dec2)), r);
        rc.color = ratingColor(c.th, r);
        row.cells.push_back(rc);
        auto st = [&](const char* id, const fd::Counters& cn) {
            const fd::Stat* s = fd::findStat(id);
            double v = s->expr.eval(cn);
            return Cell(W(fd::formatStat(v, s->def->fmt)), v);
        };
        row.cells.push_back(st("kd", g.all));
        row.cells.push_back(st("adr", g.all));
        row.cells.push_back(st("kast", g.all));
        row.cells.push_back(st("hsPct", g.all));
        row.cells.push_back(st("roundWinrate", g.t));
        row.cells.push_back(st("roundWinrate", g.ct));
        row.cells.push_back(Cell(W(fd::fmtInt(g.all.get("openKills"))) + L" : " + W(fd::fmtInt(g.all.get("openDeaths"))),
                                 g.all.get("openKills") - g.all.get("openDeaths")));
        std::string id = kv.first;
        row.click = [&app, id] {
            app.mapFilter = app.mapFilter == id ? "" : id;
            app.invalidateCareer();
            app.invalidate();
        };
        row.highlight = app.mapFilter == kv.first;
        row.key = kv.first;
        rows.push_back(row);
    }
    SortState& s = app.sort("cmaps", 1, true);
    sortRows(rows, s.col, s.desc);
    float th = 34 + std::max<size_t>(rows.size(), 1) * 48 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "cmaps";
    o.rowH = 48;
    o.sortCol = s.col;
    o.sortDesc = s.desc;
    o.onSort = [&app](int col) {
        SortState& st = app.sort("cmaps", 1, true);
        if (st.col == col) st.desc = !st.desc;
        else {
            st.col = col;
            st.desc = col != 0;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 8;
    c.text(app.mapFilter.empty() ? L"Klick auf eine Map filtert alle Statistiken danach." : L"Gefiltert nach " + W(fd::mapName(app.mapFilter)) +
                                                                                              L" – noch mal klicken hebt den Filter auf.",
           F::Small, c.th.text3, RectF(x, y, w, 20));
    y += 30;
    // Modi
    std::vector<Bar> bars;
    for (const auto& kv : car.modes) {
        const fd::GroupAgg& g = kv.second;
        Bar b;
        b.label = W(fd::modeName(kv.first));
        b.sub = std::to_wstring(g.matches) + L" Matches · Rating " + W(fd::formatStat(V("rating", g.all), fd::Fmt::Dec2));
        b.v = g.matches ? 100.0 * g.wins / g.matches : 0;
        b.value = W(fd::fmtPct(b.v, 0));
        bars.push_back(b);
    }
    float hh = 60 + bars.size() * 46 + 10;
    c.card(RectF(x, y, w, hh));
    c.text(L"Siegquote nach Modus", F::H3, c.th.text, RectF(x + 18, y + 14, w - 36, 22));
    barList(c, x + 18, y + 54, w - 36, bars, 100, 46, 180);
    y += hh + 10;
    return y - y0;
}

float clutchTab(Canvas& c, const fd::Career& car, float x, float y, float w) {
    float y0 = y;
    // Tabelle 1v1 … 1v5
    float h = 60 + 5 * 46 + 16;
    c.card(RectF(x, y, w, h));
    c.text(L"Clutches nach Lage", F::H3, c.th.text, RectF(x + 18, y + 14, w - 36, 22));
    std::vector<Bar> bars;
    for (int n = 1; n <= 5; ++n) {
        double att = car.all.get(("clutch" + std::to_string(n)).c_str()), won = car.all.get(("clutchWon" + std::to_string(n)).c_str());
        Bar b;
        b.label = L"1 gegen " + std::to_wstring(n);
        b.sub = W(fd::fmtInt(won)) + L" von " + W(fd::fmtInt(att)) + L" gewonnen";
        b.v = att > 0 ? 100 * won / att : 0;
        b.value = att > 0 ? W(fd::fmtPct(b.v, 0)) : L"–";
        bars.push_back(b);
    }
    barList(c, x + 18, y + 54, w - 36, bars, 100, 46, 180);
    y += h + 16;
    y += groupCard(c, x, y, w, "clutch", car);
    return y - y0 + 10;
}

float aimTab(Canvas& c, const fd::Career& car, float x, float y, float w) {
    float y0 = y;
    bool wide = w >= 980;
    float cw = wide ? (w - 16) / 2 : w;
    float h1 = groupCard(c, x, y, cw, "aim", car);
    float fx = wide ? x + cw + 16 : x, fy = wide ? y : y + h1 + 16;
    float fh = 330;
    c.card(RectF(fx, fy, cw, fh));
    c.text(L"Treffer nach Körperzonen", F::H3, c.th.text, RectF(fx + 18, fy + 14, cw - 36, 22));
    hitFigure(c, RectF(fx + 10, fy + 46, cw - 20, fh - 56), V("headHitPct", car.all), V("hitNeckPct", car.all), V("hitChestPct", car.all),
              V("hitStomachPct", car.all), V("hitArmsPct", car.all), V("hitLegsPct", car.all));
    float rest = wide ? std::max(h1, fh) : h1 + 16 + fh;
    y += rest + 16;
    y += groupCard(c, x, y, w, "hitzones", car);
    return y - y0 + 10;
}

float trendTab(Canvas& c, App& app, const fd::Career& car, float x, float y, float w) {
    float y0 = y;
    struct T {
        const wchar_t* title;
        const wchar_t* sub;
        std::function<double(const fd::MatchPoint&)> val;
        std::function<std::wstring(double)> fmt;
        double ref;
    };
    std::vector<T> charts = {
        {L"Rating 2.0", L"pro Match, gestrichelt: Schnitt der letzten 5", [](const fd::MatchPoint& p) { return p.rating2; },
         [](double v) { return W(fd::fmtDec(v, 2)); }, 1.0},
        {L"ADR", L"Schaden pro Runde", [](const fd::MatchPoint& p) { return p.adr; }, [](double v) { return W(fd::fmtDec(v, 0)); }, NAN},
        {L"K/D", L"Kills pro Tod", [](const fd::MatchPoint& p) { return p.kd; }, [](double v) { return W(fd::fmtDec(v, 1)); }, 1.0},
        {L"Headshot-Quote", L"in Prozent", [](const fd::MatchPoint& p) { return p.hsPct; }, [](double v) { return W(fd::fmtDec(v, 0)); }, NAN},
        {L"KAST", L"in Prozent", [](const fd::MatchPoint& p) { return p.kast; }, [](double v) { return W(fd::fmtDec(v, 0)); }, NAN},
    };
    // Premier zuerst, falls vorhanden
    auto pp = premierPoints(app, car);
    if (!pp.empty()) {
        float h = 300;
        c.card(RectF(x, y, w, h));
        c.text(L"Premier-Rating", F::H3, c.th.text, RectF(x + 18, y + 14, w - 36, 22));
        LineChartOpts o;
        o.key = "trend-premier";
        o.fmtY = [](double v) { return W(fd::fmtRating(int(v))); };
        o.pointColor = [](double v) { return rgb(fd::premierColor(int(v))); };
        o.line = c.th.text2;
        o.area = false;
        for (int b : {0, 5000, 10000, 15000, 20000, 25000, 30000}) o.bands.push_back({double(b), rgb(fd::premierColor(b + 1))});
        lineChart(c, RectF(x + 8, y + 46, w - 22, h - 56), pp, o);
        y += h + 16;
    }
    bool wide = w >= 980;
    float cw = wide ? (w - 16) / 2 : w;
    for (size_t i = 0; i < charts.size(); ++i) {
        const T& t = charts[i];
        float cx = wide && i % 2 == 1 ? x + cw + 16 : x;
        float h = 240;
        c.card(RectF(cx, y, cw, h));
        c.text(t.title, F::H3, c.th.text, RectF(cx + 18, y + 14, cw - 36, 22));
        c.text(t.sub, F::Tiny, c.th.text3, RectF(cx + 18, y + 34, cw - 36, 16));
        std::vector<ChartPoint> pts;
        for (const auto& p : car.points) {
            ChartPoint cp;
            cp.y = t.val(p);
            int64_t when = matchEpoch(*p.match);
            cp.label = dateText(when).substr(0, 6);
            cp.tip = t.fmt(cp.y) + L"\n" + matchTitle(*p.match, app.me) + L" · " + W(fd::resultName(p.me->result)) + L"\n" + dateTimeText(when);
            std::string id = p.match->checksum;
            cp.click = [&app, id] { app.openMatch(id); };
            pts.push_back(cp);
        }
        LineChartOpts o;
        o.key = "trend" + std::to_string(i);
        o.fmtY = t.fmt;
        o.ref = t.ref;
        o.movingAvg = 5;
        if (std::wstring(t.sub) == L"in Prozent") o.yCap = 100;
        lineChart(c, RectF(cx + 8, y + 56, cw - 22, h - 64), pts, o);
        if (!wide || i % 2 == 1 || i + 1 == charts.size()) y += h + 16;
    }
    return y - y0;
}

}  // namespace

float drawStats(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = w - pad * 2, y = 28;
    const fd::Career& car = app.career();
    std::wstring sub = car.matches ? (car.name.empty() ? L"" : W(car.name) + L" · ") + std::to_wstring(car.matches) +
                                         (car.matches == 1 ? L" Match · " : L" Matches · ") + W(fd::fmtInt(car.all.get("rounds"))) + L" Runden"
                                   : L"Noch keine Daten für diese Auswahl";
    y += pageTitle(c, x, y, cw, L"Statistiken", sub) + 10;
    y += filterRow(c, app, x, y, cw) + 18;
    if (car.matches == 0) {
        y += emptyState(c, x, y, cw, Icon::Stats, L"Keine passenden Matches", L"Ändere den Filter oder werte weitere Demos aus.");
        return y + 20;
    }
    std::vector<std::wstring> tabs = {L"Allgemein", L"Waffen", L"Maps", L"Eröffnung & Trades", L"Clutches", L"Utility", L"Zielen",
                                      L"Besondere Kills", L"Schaden & Zeit", L"Economy & Bombe", L"Verlauf"};
    y += c.tabs(x, y, cw, tabs, app.statsTab, "stab", [&app](int i) {
        app.statsTab = i;
        app.invalidate();
    }) + 20;
    switch (app.statsTab) {
        case 0: y += twoGroups(c, x, y, cw, "overview", "multi", car) + 16; break;
        case 1: y += weaponsTab(c, app, car, x, y, cw); break;
        case 2: y += mapsTab(c, app, car, x, y, cw); break;
        case 3: y += groupCard(c, x, y, cw, "opening", car) + 16; break;
        case 4: y += clutchTab(c, car, x, y, cw); break;
        case 5: y += groupCard(c, x, y, cw, "utility", car) + 16; break;
        case 6: y += aimTab(c, car, x, y, cw); break;
        case 7: y += groupCard(c, x, y, cw, "special", car) + 16; break;
        case 8: y += groupCard(c, x, y, cw, "damage", car) + 16; break;
        case 9: y += twoGroups(c, x, y, cw, "economy", "bomb", car) + 16; break;
        default: y += trendTab(c, app, car, x, y, cw); break;
    }
    c.text(L"Werte als T und CT beziehen sich auf die Runden auf der jeweiligen Seite. Fahr mit der Maus über eine Kennzahl für eine Erklärung.",
           F::Small, c.th.text3, RectF(x, y, cw, 20));
    return y + 44;
}

}  // namespace fdw
