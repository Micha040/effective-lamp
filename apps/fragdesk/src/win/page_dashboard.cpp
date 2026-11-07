// FragDesk – Seite „Übersicht“
#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }

std::wstring statText(const char* id, const fd::Counters& c) {
    const fd::Stat* s = fd::findStat(id);
    return s ? W(fd::formatStat(s->expr.eval(c), s->def->fmt)) : L"–";
}

std::wstring sideLine(const char* id, const fd::Career& car) {
    const fd::Stat* s = fd::findStat(id);
    if (s && fd::statSideless(*s))   // z. B. Siegquote: Siege und Niederlagen statt T/CT
        return std::to_wstring(car.wins) + L" Siege  ·  " + std::to_wstring(car.losses) + L" Niederlagen" +
               (car.draws ? L"  ·  " + std::to_wstring(car.draws) + L" Unentschieden" : L"");
    return L"T " + statText(id, car.t) + L"  ·  CT " + statText(id, car.ct);
}

}  // namespace

// Filterzeile (Modus, Zeitraum, Map) – auch auf der Statistik-Seite; bricht bei schmalen Fenstern um
float filterRow(Canvas& c, App& app, float x, float y, float w) {
    const float rowH = 30, lineGap = 10;
    float cy = y;
    float cx = x + c.chipRow(x, cy, App::modeLabels(), app.modeFilter, "mode", [&app](int i) {
        app.modeFilter = i;
        app.invalidateCareer();
        app.invalidate();
    }) + 18;
    auto periods = App::periodLabels();
    if (cx + c.chipRowWidth(periods) > x + w) {
        cx = x;
        cy += rowH + lineGap;
    } else {
        c.line(cx - 9, cy + 6, cx - 9, cy + 24, c.th.border, 1);
    }
    cx += c.chipRow(cx, cy, periods, app.periodFilter, "period", [&app](int i) {
        app.periodFilter = i;
        app.invalidateCareer();
        app.invalidate();
    }) + 12;
    // Map-Auswahl
    std::wstring mapLabel = app.mapFilter.empty() ? L"Alle Maps" : W(fd::mapName(app.mapFilter));
    float bw = c.measure(mapLabel, F::SmallSemi) + (app.mapFilter.empty() ? 46 : 60);
    if (cx + bw > x + w) {
        cx = x;
        cy += rowH + lineGap;
    }
    RectF br(cx, cy, bw, rowH);
    uint32_t id = hashId("mapsel");
    c.round(br, 15, c.hot(id) ? c.th.btnHover : c.th.btn);
    if (!app.mapFilter.empty()) c.round(RectF(br.X + 12, cy + 10, 10, 10), 3, rgb(fd::mapColor(app.mapFilter)));
    float tx = app.mapFilter.empty() ? 14 : 28;
    c.text(mapLabel, F::SmallSemi, app.mapFilter.empty() ? c.th.text2 : c.th.text, RectF(br.X + tx, cy, bw - tx - 26, rowH));
    c.icon(Icon::ChevronDown, RectF(br.X + bw - 22, cy + 9, 12, 12), c.th.text2, 1.6f);
    c.region(br, id, [&app] {
        HMENU menu = CreatePopupMenu();
        std::vector<std::string> maps;
        std::map<std::string, int> count;
        for (const auto& m : app.lib.matches)
            if (m->player(app.me)) ++count[m->map];
        AppendMenuW(menu, MF_STRING | (app.mapFilter.empty() ? MF_CHECKED : 0), 1, L"Alle Maps");
        maps.push_back("");
        for (const auto& kv : count) {
            std::wstring l = W(fd::mapName(kv.first)) + L"  (" + std::to_wstring(kv.second) + L")";
            AppendMenuW(menu, MF_STRING | (app.mapFilter == kv.first ? MF_CHECKED : 0), UINT(maps.size() + 1), l.c_str());
            maps.push_back(kv.first);
        }
        POINT cur;
        GetCursorPos(&cur);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cur.x, cur.y + 12, 0, app.hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd > 0 && size_t(cmd) <= maps.size()) {
            app.mapFilter = maps[cmd - 1];
            app.invalidateCareer();
            app.invalidate();
        }
    });
    return cy + rowH - y;
}

namespace {

void formCard(Canvas& c, App& app, const fd::Career& car, const RectF& r) {
    c.card(r);
    c.text(L"Form", F::H3, c.th.text, RectF(r.X + 18, r.Y + 14, r.Width - 36, 22));
    c.text(L"Die letzten 10 Matches", F::Tiny, c.th.text3, RectF(r.X + 18, r.Y + 36, r.Width - 36, 16));
    size_t n = car.points.size(), from = n > 10 ? n - 10 : 0;
    float cw = std::min(34.0f, (r.Width - 36 - 9 * 5) / 10.0f);
    float x = r.X + 18, y = r.Y + 64;
    int wins = 0, cnt = 0;
    double rat = 0, kills = 0, deaths = 0, dmg = 0, rounds = 0;   // K/D und ADR über alle 10 Matches zusammen
    for (size_t i = from; i < n; ++i) {
        const auto& p = car.points[i];
        bool win = p.me->result == "win", loss = p.me->result == "loss";
        Color col = win ? c.th.success : loss ? c.th.danger : c.th.text3;
        RectF b(x, y, cw, 40);
        uint32_t id = hashId("form:" + p.match->checksum);
        c.round(b, 6, alpha(col, c.hot(id) ? 110 : 60));
        c.text(win ? L"S" : loss ? L"N" : L"U", F::BodySemi, col, RectF(b.X, b.Y + 2, cw, 22), Align::Center);
        c.round(RectF(b.X + cw / 2 - 4, b.Y + 27, 8, 6), 2, rgb(fd::mapColor(p.match->map)));
        std::string cs = p.match->checksum;
        std::wstring tip = matchTitle(*p.match, app.me) + L"\n" + W(fd::resultName(p.me->result)) + L" · Rating " + W(fd::fmtDec(p.rating2, 2)) +
                           L"\n" + dateTimeText(matchEpoch(*p.match));
        c.region(b, id, [&app, cs] { app.openMatch(cs); }, tip);
        x += cw + 5;
        wins += win;
        ++cnt;
        rat += p.rating2;
        kills += p.me->all.get("kills");
        deaths += p.me->all.get("deaths");
        dmg += p.me->all.get("dmg");
        rounds += p.me->all.get("rounds");
    }
    if (cnt == 0) return;
    struct Mini {
        const wchar_t* label;
        std::wstring value;
        Color col;
    } minis[] = {
        {L"Siege", std::to_wstring(wins) + L" / " + std::to_wstring(cnt), c.th.text},
        {L"Ø Rating", W(fd::fmtDec(rat / cnt, 2)), ratingColor(c.th, rat / cnt)},
        {L"ADR", W(fd::fmtDec(rounds > 0 ? dmg / rounds : 0, 1)), c.th.text},
        {L"K/D", W(fd::fmtDec(deaths > 0 ? kills / deaths : kills, 2)), c.th.text},
    };
    float mw = (r.Width - 36) / 2, my = r.Y + 124;
    for (int i = 0; i < 4; ++i) {
        float mx = r.X + 18 + (i % 2) * mw, yy = my + (i / 2) * 58;
        c.text(minis[i].label, F::Small, c.th.text2, RectF(mx, yy, mw, 18));
        c.text(minis[i].value, F::Num, minis[i].col, RectF(mx, yy + 18, mw, 32));
    }
}

void premierCard(Canvas& c, App& app, const fd::Career& car, const RectF& r) {
    c.card(r);
    c.text(L"Premier-Rating", F::H3, c.th.text, RectF(r.X + 18, r.Y + 14, 300, 22));
    auto pts = premierPoints(app, car);
    if (pts.empty()) {
        c.text(L"Noch keine Premier-Matches ausgewertet", F::Body, c.th.text2, RectF(r.X + 18, r.Y + 50, r.Width - 36, 20));
        c.text(L"Lade Premier-Demos in CS2 unter „Deine Matches“ herunter – FragDesk liest das Rating vor und nach jedem Match aus der Demo.",
               F::Small, c.th.text3, RectF(r.X + 18, r.Y + 74, r.Width - 36, 40), Align::Left, false, true);
        return;
    }
    int cur = car.premierCurrent;
    std::wstring num = W(fd::fmtRating(cur));
    c.text(num, F::NumHuge, rgb(fd::premierColor(cur)), RectF(r.X + 18, r.Y + 38, 260, 54));
    float nw = c.measure(num, F::NumHuge);
    // letztes Premier-Match (das letzte Match überhaupt kann FACEIT o. Ä. sein)
    fd::MatchPoint last;
    for (auto it = car.points.rbegin(); it != car.points.rend(); ++it)
        if (it->premierNew > 0 || it->premierOld > 0) {
            last = *it;
            break;
        }
    float bx = r.X + 18 + nw + 14;
    if (last.premierOld > 0 && last.premierNew > 0) {
        int d = last.premierNew - last.premierOld;
        std::wstring t = W(fd::fmtSigned(d, 0)) + L" zuletzt";
        float pw = c.pillWidth(t, F::SmallSemi) + 6;
        c.pill(RectF(bx, r.Y + 54, pw, 24), t, alpha(d >= 0 ? c.th.success : c.th.danger, 40), d >= 0 ? c.th.success : c.th.danger, F::SmallSemi);
        bx += pw + 8;
    }
    c.text(L"Bestwert " + W(fd::fmtRating(car.premierBest)), F::Small, c.th.text3, RectF(bx, r.Y + 54, 200, 24));
    LineChartOpts o;
    o.key = "premier-dash";
    o.fmtY = [](double v) { return W(fd::fmtRating(int(v))); };
    o.pointColor = [](double v) { return rgb(fd::premierColor(int(v))); };
    o.line = c.th.text2;
    o.area = false;
    for (int b : {0, 5000, 10000, 15000, 20000, 25000, 30000}) o.bands.push_back({double(b), rgb(fd::premierColor(b + 1))});
    lineChart(c, RectF(r.X + 8, r.Y + 98, r.Width - 22, r.Height - 108), pts, o);
}

}  // namespace

float drawDashboard(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = w - pad * 2, y = 28;
    std::wstring sub;
    if (!app.lib.loaded()) sub = L"Bibliothek wird geladen …";
    else if (app.lib.matches.empty()) sub = L"Willkommen bei FragDesk";
    else sub = (app.meName.empty() ? L"" : L"Hallo " + app.meName + L" – ") + std::to_wstring(app.lib.matches.size()) +
               (app.lib.matches.size() == 1 ? L" Match ausgewertet" : L" Matches ausgewertet");
    y += pageTitle(c, x, y, cw, L"Übersicht", sub) + 10;

    if (app.lib.loaded() && app.lib.matches.empty()) {
        float h = emptyState(c, x, y, cw, Icon::Download, L"Noch keine Matches",
                             L"Lade eine Demo herunter – in CS2 unter „Deine Matches“ oder bei FACEIT mit „Watch Demo“. FragDesk findet sie "
                             L"automatisch und wertet sie auf Knopfdruck aus. Du kannst Demos auch selbst hinzufügen.");
        y += h;
        float bw1 = c.buttonWidth(L"Demos hinzufügen", Icon::Plus), bw2 = c.buttonWidth(L"Zu den Matches");
        float bx = x + (cw - bw1 - bw2 - 10) / 2;
        c.button(RectF(bx, y, bw1, 38), L"Demos hinzufügen", "dash-add", [&app] { app.addDemos(); }, Btn::Primary, Icon::Plus);
        c.button(RectF(bx + bw1 + 10, y, bw2, 38), L"Zu den Matches", "dash-matches", [&app] { app.go(Page::Matches); });
        return y + 60;
    }

    y += filterRow(c, app, x, y, cw) + 20;
    const fd::Career& car = app.career();
    if (car.matches == 0) {
        y += emptyState(c, x, y, cw, Icon::Stats, L"Keine passenden Matches",
                        app.me.empty() ? L"Wähle in den Einstellungen dein Konto aus."
                                       : L"Für diese Auswahl gibt es noch keine ausgewerteten Matches. Ändere den Filter oder werte weitere Demos aus.");
        return y + 20;
    }

    const float gap = 16;
    bool wide = cw >= 900;
    // Reihe 1: Premier + Form
    if (wide) {
        float w1 = std::floor((cw - gap) * 0.64f);
        premierCard(c, app, car, RectF(x, y, w1, 286));
        formCard(c, app, car, RectF(x + w1 + gap, y, cw - w1 - gap, 286));
        y += 286 + gap;
    } else {
        premierCard(c, app, car, RectF(x, y, cw, 286));
        y += 286 + gap;
        formCard(c, app, car, RectF(x, y, cw, 260));
        y += 260 + gap;
    }

    // Reihe 2: Kennzahlen
    struct K {
        const char* id;
        const wchar_t* label;
        Icon icon;
    } kpis[] = {
        {"rating", L"Rating 2.0", Icon::Star},          {"kd", L"K/D", Icon::Crosshair},
        {"adr", L"ADR", Icon::Target},                  {"kast", L"KAST", Icon::Shield},
        {"hsPct", L"Headshot-Quote", Icon::Crosshair},  {"winrate", L"Siegquote", Icon::Trophy},
        {"openSuccess", L"Eröffnungsduelle gewonnen", Icon::Flash}, {"clutchRate", L"Clutches gewonnen", Icon::Star},
    };
    int perRow = wide ? 4 : 2;
    float kw = (cw - gap * (perRow - 1)) / perRow, kh = 104;
    for (int i = 0; i < 8; ++i) {
        float kx = x + (i % perRow) * (kw + gap), ky = y + (i / perRow) * (kh + gap);
        const fd::Stat* s = fd::findStat(kpis[i].id);
        double v = s->expr.eval(car.all);
        Color vc = std::string(kpis[i].id) == "rating" ? ratingColor(c.th, v) : Color(0, 0, 0, 0);
        statCard(c, RectF(kx, ky, kw, kh), kpis[i].label, W(fd::formatStat(v, s->def->fmt)), sideLine(kpis[i].id, car), W(s->def->hint), vc,
                 kpis[i].icon);
    }
    y += (8 / perRow) * (kh + gap);

    // Reihe 3: letzte Matches + Waffen
    float colW = wide ? (cw - gap) / 2 : cw;
    float rowY = y;
    {
        float h = 52 + 6 * 46 + 12;
        RectF r(x, rowY, colW, h);
        c.card(r);
        c.text(L"Letzte Matches", F::H3, c.th.text, RectF(r.X + 18, r.Y + 14, 200, 22));
        c.button(RectF(r.X + r.Width - 112, r.Y + 10, 98, 30), L"Alle", "dash-allmatches", [&app] { app.go(Page::Matches); }, Btn::Ghost,
                 Icon::Chevron);
        std::vector<Col> cols = {{L"", -3}, {L"", 74, Align::Right}, {L"", 66, Align::Right}, {L"", 58, Align::Right}};
        std::vector<Row> rows;
        for (size_t i = car.points.size(); i-- > 0 && rows.size() < 6;) {
            const auto& p = car.points[i];
            Row row;
            Cell map(W(fd::mapName(p.match->map)));
            map.dot = rgb(fd::mapColor(p.match->map));
            map.font = F::BodySemi;
            map.sub = relativeDay(matchEpoch(*p.match)) + L" · " + W(fd::modeName(p.match->mode));
            row.cells.push_back(map);
            Cell score(std::to_wstring(p.match->scoreOf(p.me->team)) + L":" + std::to_wstring(p.match->scoreOf(p.me->team == "A" ? "B" : "A")));
            score.color = p.me->result == "win" ? c.th.success : p.me->result == "loss" ? c.th.danger : c.th.text2;
            score.font = F::BodySemi;
            row.cells.push_back(score);
            row.cells.push_back(Cell(W(fd::fmtInt(p.me->stat("kills"))) + L"–" + W(fd::fmtInt(p.me->stat("deaths")))));
            Cell rat(W(fd::fmtDec(p.rating2, 2)));
            rat.color = ratingColor(c.th, p.rating2);
            rat.font = F::BodySemi;
            row.cells.push_back(rat);
            std::string cs = p.match->checksum;
            row.click = [&app, cs] { app.openMatch(cs); };
            row.key = cs;
            rows.push_back(row);
        }
        TableOpts o;
        o.key = "dash-recent";
        o.header = false;
        o.rowH = 46;
        table(c, r.X + 6, r.Y + 48, r.Width - 12, cols, rows, o);
        if (wide) {
            RectF r2(x + colW + gap, rowY, colW, h);
            c.card(r2);
            c.text(L"Waffen", F::H3, c.th.text, RectF(r2.X + 18, r2.Y + 14, 200, 22));
            c.text(L"Kills und Headshot-Quote", F::Tiny, c.th.text3, RectF(r2.X + 18, r2.Y + 34, 300, 16));
            std::vector<std::pair<double, std::string>> ws;
            for (const auto& kv : car.weapons) ws.push_back({kv.second.second.get("kills"), kv.first});
            std::sort(ws.rbegin(), ws.rend());
            std::vector<Bar> bars;
            double mx = ws.empty() ? 1 : ws[0].first;
            for (size_t i = 0; i < ws.size() && i < 6 && ws[i].first > 0; ++i) {
                const auto& cnt = car.weapons.at(ws[i].second).second;
                Bar b;
                b.label = W(ws[i].second);
                b.v = ws[i].first;
                b.value = W(fd::fmtInt(b.v)) + L" Kills";
                b.sub = L"HS " + W(fd::formatStat(fd::statValue("hsPct", cnt), fd::Fmt::Pct0)) + L" · Treffer " +
                        W(fd::formatStat(fd::statValue("accuracy", cnt), fd::Fmt::Pct0));
                bars.push_back(b);
            }
            barList(c, r2.X + 18, r2.Y + 60, r2.Width - 36, bars, mx, 44, 140);
            if (bars.empty()) c.text(L"Noch keine Kills", F::Small, c.th.text3, RectF(r2.X + 18, r2.Y + 60, 200, 20));
        }
        y += h + gap;
    }
    if (!wide) {
        // Waffen unter den Matches
        float h = 60 + 6 * 44 + 10;
        RectF r2(x, y, cw, h);
        c.card(r2);
        c.text(L"Waffen", F::H3, c.th.text, RectF(r2.X + 18, r2.Y + 14, 200, 22));
        std::vector<std::pair<double, std::string>> ws;
        for (const auto& kv : car.weapons) ws.push_back({kv.second.second.get("kills"), kv.first});
        std::sort(ws.rbegin(), ws.rend());
        std::vector<Bar> bars;
        double mx = ws.empty() ? 1 : ws[0].first;
        for (size_t i = 0; i < ws.size() && i < 6 && ws[i].first > 0; ++i) {
            Bar b;
            b.label = W(ws[i].second);
            b.v = ws[i].first;
            b.value = W(fd::fmtInt(b.v));
            bars.push_back(b);
        }
        barList(c, r2.X + 18, r2.Y + 52, r2.Width - 36, bars, mx, 44, 140);
        y += h + gap;
    }

    // Reihe 4: Maps + Momente
    rowY = y;
    {
        std::vector<std::pair<int, std::string>> maps;
        for (const auto& kv : car.maps) maps.push_back({kv.second.matches, kv.first});
        std::sort(maps.rbegin(), maps.rend());
        size_t nm = std::min<size_t>(maps.size(), 7);
        float h = 60 + std::max<size_t>(nm, 3) * 44 + 10;
        RectF r(x, rowY, colW, h);
        c.card(r);
        c.text(L"Maps", F::H3, c.th.text, RectF(r.X + 18, r.Y + 14, 200, 22));
        c.text(L"Siegquote und Rating", F::Tiny, c.th.text3, RectF(r.X + 18, r.Y + 34, 300, 16));
        std::vector<Bar> bars;
        for (size_t i = 0; i < nm; ++i) {
            const auto& g = car.maps.at(maps[i].second);
            Bar b;
            b.label = W(fd::mapName(maps[i].second));
            b.sub = std::to_wstring(g.matches) + (g.matches == 1 ? L" Match" : L" Matches") + L" · Rating " +
                    W(fd::formatStat(fd::statValue("rating", g.all), fd::Fmt::Dec2));
            b.v = g.matches ? 100.0 * g.wins / g.matches : 0;
            b.value = W(fd::fmtPct(b.v, 0));
            b.color = rgb(fd::mapColor(maps[i].second));
            std::string mapId = maps[i].second;
            b.click = [&app, mapId] {
                app.mapFilter = mapId;
                app.invalidateCareer();
                app.go(Page::Stats);
            };
            b.tip = std::to_wstring(g.wins) + L" Siege, " + std::to_wstring(g.losses) + L" Niederlagen" +
                    (g.draws ? L", " + std::to_wstring(g.draws) + L" Unentschieden" : L"");
            bars.push_back(b);
        }
        barList(c, r.X + 18, r.Y + 58, r.Width - 36, bars, 100, 44, 150);

        // Momente
        RectF r2 = wide ? RectF(x + colW + gap, rowY, colW, h) : RectF(x, rowY + h + gap, cw, 60 + 5 * 46);
        c.card(r2);
        c.text(L"Beste Momente", F::H3, c.th.text, RectF(r2.X + 18, r2.Y + 14, 200, 22));
        c.button(RectF(r2.X + r2.Width - 112, r2.Y + 10, 98, 30), L"Alle", "dash-allmoments", [&app] { app.go(Page::Highlights); }, Btn::Ghost,
                 Icon::Chevron);
        struct Mom {
            const fd::Match* m;
            const fd::HighlightInfo* h;
        };
        std::vector<Mom> moms;
        for (const auto& p : car.points)
            for (const auto& h : p.match->highlights)
                if (h.player == app.me) moms.push_back({p.match, &h});
        std::sort(moms.begin(), moms.end(), [](const Mom& a, const Mom& b) { return a.h->score > b.h->score; });
        float my = r2.Y + 52;
        int shown = 0;
        for (const auto& mo : moms) {
            if (shown >= 5 || my + 46 > r2.Y + r2.Height) break;
            RectF rr(r2.X + 8, my, r2.Width - 16, 46);
            std::string key = mo.m->checksum + "/" + mo.h->id;
            uint32_t id = hashId("dmom:" + key);
            if (c.hot(id)) c.round(rr, 8, c.th.hover);
            c.icon(mo.h->clutch ? Icon::Star : Icon::Spark, RectF(rr.X + 10, rr.Y + 14, 18, 18), c.th.accentText, 1.6f);
            c.text(W(fd::highlightTitle(*mo.h)), F::BodySemi, c.th.text, RectF(rr.X + 38, rr.Y + 4, rr.Width - 170, 22));
            c.text(W(fd::mapName(mo.m->map)) + L" · Runde " + std::to_wstring(mo.h->round) + L" · " + relativeDay(matchEpoch(*mo.m)),
                   F::Tiny, c.th.text3, RectF(rr.X + 38, rr.Y + 25, rr.Width - 170, 16));
            const Clip* clip = app.clipFor(mo.m->checksum, mo.h->id);
            MomentRef ref{mo.m->checksum, mo.h->id};
            if (clip) {
                std::wstring path = clip->path;
                c.button(RectF(rr.X + rr.Width - 122, rr.Y + 8, 114, 30), L"Video", "dmv:" + key, [path] { openInShell(path); }, Btn::Secondary, Icon::Play);
            } else {
                c.button(RectF(rr.X + rr.Width - 122, rr.Y + 8, 114, 30), L"Ansehen", "dmw:" + key, [&app, ref] { app.watch(ref); }, Btn::Secondary,
                         Icon::Eye, true, L"In CS2 öffnen und zum Moment springen");
            }
            std::string cs = mo.m->checksum;
            c.region(RectF(rr.X, rr.Y, rr.Width - 130, rr.Height), id, [&app, cs] { app.openMatch(cs, 6); });
            my += 46;
            ++shown;
        }
        if (moms.empty()) c.text(L"Noch keine Momente – sie werden beim Auswerten erkannt.", F::Small, c.th.text3, RectF(r2.X + 18, r2.Y + 56, r2.Width - 36, 20));
        y = std::max(rowY + h, r2.Y + r2.Height) + gap;
    }
    return y + 12;
}

}  // namespace fdw
