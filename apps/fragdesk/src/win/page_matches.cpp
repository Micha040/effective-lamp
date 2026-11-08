// FragDesk – Seite „Matches“: neue Demos und alle ausgewerteten Matches
#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }

float demosCard(Canvas& c, App& app, float x, float y, float w) {
    auto& demos = app.lib.demos;
    int open = 0;
    for (const auto& d : demos)
        if (d.state == DemoFile::State::New || d.state == DemoFile::State::Failed) ++open;
    const float rowH = 56;
    size_t shown = std::min<size_t>(demos.size(), 12);
    float h = 64 + shown * rowH + (demos.size() > shown ? 30 : 8);
    RectF r(x, y, w, h);
    c.card(r);
    c.round(RectF(r.X + 1, r.Y + 1, 4, r.Height - 2), 2, c.th.accent);
    c.text(L"Neue Demos", F::H3, c.th.text, RectF(r.X + 20, r.Y + 14, 300, 22));
    c.text(std::to_wstring(demos.size()) + (demos.size() == 1 ? L" Demo wartet auf die Auswertung" : L" Demos warten auf die Auswertung"), F::Tiny,
           c.th.text3, RectF(r.X + 20, r.Y + 36, 400, 16));
    if (open > 1) {
        std::wstring label = L"Alle auswerten (" + std::to_wstring(open) + L")";
        float bw = c.buttonWidth(label, Icon::Play);
        c.button(RectF(r.X + r.Width - bw - 16, r.Y + 14, bw, 34), label, "demos-all", [&app] { app.lib.analyzeAll(); app.invalidate(); },
                 Btn::Primary, Icon::Play);
    }
    float cy = r.Y + 60;
    for (size_t i = 0; i < shown; ++i) {
        const DemoFile& d = demos[i];
        RectF rr(r.X + 8, cy, r.Width - 16, rowH);
        std::string key = narrow(d.path);
        uint32_t id = hashId("demo:" + key);
        if (c.hot(id)) c.round(rr, 8, c.th.hover);
        c.region(rr, id, nullptr, L"", false);   // vor den Knöpfen: die späteren Flächen liegen oben
        Icon ic = d.origin == L"CS2" ? Icon::Film : d.origin == L"Downloads" || d.origin == L"Valve" ? Icon::Download : Icon::Folder;
        c.circle(rr.X + 26, rr.Y + rowH / 2, 16, c.th.btn);
        c.icon(ic, RectF(rr.X + 17, rr.Y + rowH / 2 - 9, 18, 18), c.th.text2, 1.6f);
        float tx = rr.X + 54, tw = rr.Width - 54 - 230;
        bool faceit = fileName(d.path).rfind(L"1-", 0) == 0;
        std::wstring origin = d.origin == L"CS2" ? L"CS2 (Premier/Wettkampf)" : faceit ? L"FACEIT" : d.origin == L"Valve" ? L"Von Valve geladen" : d.origin;
        c.text(fileName(d.path), F::BodySemi, c.th.text, RectF(tx, rr.Y + 8, tw, 22));
        c.text(origin + L" · " + W(fd::fmtBytes(double(d.size))) + L" · " + relativeDay(d.mtime), F::Tiny, c.th.text3,
               RectF(tx, rr.Y + 30, tw, 16));
        c.tip(RectF(tx, rr.Y, tw, rowH), d.path);
        float bx = rr.X + rr.Width - 224;
        switch (d.state) {
            case DemoFile::State::New:
                c.button(RectF(bx + 70, rr.Y + 11, 114, 34), L"Auswerten", "an:" + key, [&app, p = d.path] { app.lib.analyze(p); app.invalidate(); },
                         Btn::Secondary, Icon::Play);
                break;
            case DemoFile::State::Queued:
            case DemoFile::State::Running: {
                c.text(d.step.empty() ? L"Wartet …" : d.step, F::Small, c.th.accentText, RectF(bx - 20, rr.Y, 200, rowH), Align::Right);
                break;
            }
            case DemoFile::State::Failed:
                c.text(d.error, F::Small, c.th.danger, RectF(bx - 160, rr.Y + 4, 224, rowH - 8), Align::Right, true, true);
                c.button(RectF(bx + 70, rr.Y + 11, 114, 34), L"Erneut", "re:" + key, [&app, p = d.path] { app.lib.analyze(p); app.invalidate(); },
                         Btn::Secondary, Icon::Refresh);
                break;
        }
        if (d.state == DemoFile::State::New || d.state == DemoFile::State::Failed) {
            DemoFile copy = d;
            c.iconButton(RectF(rr.X + rr.Width - 40, rr.Y + 11, 34, 34), Icon::Close, "ign:" + key,
                         [&app, copy] {
                             app.settings.ignored.push_back(Library::ignoreKey(copy));
                             app.saveSettings();
                             app.lib.setIgnored(app.settings.ignored);
                             auto& v = app.lib.demos;
                             v.erase(std::remove_if(v.begin(), v.end(), [&](const DemoFile& x) { return samePath(x.path, copy.path); }), v.end());
                             app.invalidate();
                         },
                         L"Nicht mehr anbieten");
        }
        cy += rowH;
    }
    if (demos.size() > shown)
        c.text(L"… und " + std::to_wstring(demos.size() - shown) + L" weitere", F::Small, c.th.text3, RectF(r.X + 20, cy, 300, 24));
    return h;
}

}  // namespace

float drawMatches(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = w - pad * 2, y = 28;
    std::wstring sub = std::to_wstring(app.lib.matches.size()) + (app.lib.matches.size() == 1 ? L" Match ausgewertet" : L" Matches ausgewertet");
    if (app.lib.pulledCount() > 0) sub += L" · " + std::to_wstring(app.lib.pulledCount()) + L" davon vom Server";
    if (!app.lib.demos.empty()) sub += L" · " + std::to_wstring(app.lib.demos.size()) + L" neue Demos";
    float th = pageTitle(c, x, y, cw - 360, L"Matches", sub);
    float b1 = c.buttonWidth(L"Demos hinzufügen", Icon::Plus), b2 = 40;
    c.button(RectF(x + cw - b1 - b2 - 8, y + 6, b1, 36), L"Demos hinzufügen", "m-add", [&app] { app.addDemos(); }, Btn::Primary, Icon::Plus);
    c.iconButton(RectF(x + cw - b2, y + 6, 36, 36), Icon::Refresh, "m-scan", [&app] { app.rescan(); }, L"Demo-Ordner neu durchsuchen");
    y += th + 16;

    if (!app.lib.demos.empty()) y += demosCard(c, app, x, y, cw) + 20;

    if (app.lib.matches.empty()) {
        if (app.lib.demos.empty())
            y += emptyState(c, x, y, cw, Icon::List, L"Noch keine Matches",
                            L"FragDesk schaut im CS2-Ordner „replays“ und in deinen Downloads nach Demos. Lade eine Demo in CS2 unter "
                            L"„Deine Matches“ herunter oder bei FACEIT mit „Watch Demo“ – sie erscheint dann hier.");
        return y + 20;
    }

    // Filter
    c.chipRow(x, y, App::modeLabels(), app.listMode, "listmode", [&app](int i) {
        app.listMode = i;
        app.invalidate();
    });
    y += 46;

    static const char* modes[] = {"", "premier", "faceit", "competitive", "wingman", "other"};
    std::string mode = app.listMode > 0 && app.listMode < 6 ? modes[app.listMode] : "";
    bool anyPremier = false;
    for (const auto& m : app.lib.matches)
        if (m->mode == "premier") anyPremier = true;

    std::vector<Col> cols = {
        {L"DATUM", 118},
        {L"MAP", -2},
        {L"MODUS", 96},
        {L"ERGEBNIS", 92, Align::Center},
        {L"K", 46, Align::Right},
        {L"D", 46, Align::Right},
        {L"A", 46, Align::Right},
        {L"ADR", 62, Align::Right, L"Schaden pro Runde"},
        {L"KAST", 64, Align::Right, L"Runden mit Kill, Assist, Überleben oder Trade"},
        {L"HS", 54, Align::Right, L"Headshot-Quote"},
        {L"RATING", 70, Align::Right, L"Rating 2.0 (Näherung)"},
    };
    if (anyPremier) cols.push_back({L"PREMIER", 118, Align::Right});
    std::vector<Row> rows;
    for (const auto& mp : app.lib.matches) {
        const fd::Match& m = *mp;
        if (!mode.empty() && m.mode != mode) continue;
        const fd::PlayerInfo* p = m.player(app.me);
        Row row;
        int64_t t = matchEpoch(m);
        Cell date(relativeDay(t), double(t));
        date.font = F::Small;
        date.color = c.th.text2;
        row.cells.push_back(date);
        Cell map(W(fd::mapName(m.map)));
        map.dot = rgb(fd::mapColor(m.map));
        map.font = F::BodySemi;
        map.sub = W(fd::sourceName(m.source)) + L" · " + W(fd::fmtDuration(m.duration));
        if (const PulledInfo* pi = app.lib.pulled(m.checksum)) {   // kleiner Hinweis: kam vom eigenen Server
            map.sub += L" · vom Server";
            map.tip = pi->fetched ? L"Vom Server geholt – dein Server hat das Match selbst bei Valve geladen." : L"Vom Server geholt.";
        }
        row.cells.push_back(map);
        row.cells.push_back(Cell(W(fd::modeName(m.mode))));
        std::wstring score;
        Color sc = c.th.text2;
        if (p) {
            int mine = m.scoreOf(p->team), theirs = m.scoreOf(p->team == "A" ? "B" : "A");
            score = std::to_wstring(mine) + L" : " + std::to_wstring(theirs);
            sc = p->result == "win" ? c.th.success : p->result == "loss" ? c.th.danger : c.th.text2;
            row.accentBar = sc;
        } else score = std::to_wstring(m.scoreA) + L" : " + std::to_wstring(m.scoreB);
        Cell sCell(score, p ? (p->result == "win" ? 1 : p->result == "loss" ? -1 : 0) : 0);
        sCell.color = sc;
        sCell.font = F::BodySemi;
        row.cells.push_back(sCell);
        auto num = [&](double v) { return Cell(p ? W(fd::fmtInt(v)) : L"–", p ? v : NAN); };
        row.cells.push_back(num(p ? p->stat("kills") : 0));
        row.cells.push_back(num(p ? p->stat("deaths") : 0));
        row.cells.push_back(num(p ? p->stat("assists") + p->stat("flashAssists") : 0));
        row.cells.push_back(Cell(p ? W(fd::fmtDec(p->adr, 1)) : L"–", p ? p->adr : NAN));
        row.cells.push_back(Cell(p ? W(fd::fmtPct(p->kast, 0)) : L"–", p ? p->kast : NAN));
        double hs = p && p->stat("kills") > 0 ? 100 * p->stat("hsKills") / p->stat("kills") : NAN;
        row.cells.push_back(Cell(W(fd::formatStat(hs, fd::Fmt::Pct0)), hs));
        Cell rat(p ? W(fd::fmtDec(p->rating, 2)) : L"–", p ? p->rating : NAN);
        rat.color = p ? ratingColor(c.th, p->rating) : c.th.text3;
        rat.font = F::BodySemi;
        row.cells.push_back(rat);
        if (anyPremier) {
            Cell pr(L"", NAN);
            if (p && p->rankType == 11 && (p->rankNew > 0 || p->rankOld > 0)) {
                int v = p->rankNew > 0 ? p->rankNew : p->rankOld;
                pr.text = W(fd::fmtRating(v));
                pr.color = rgb(fd::premierColor(v));
                pr.font = F::BodySemi;
                pr.sort = v;
                if (p->rankOld > 0 && p->rankNew > 0) pr.sub = W(fd::fmtSigned(p->rankNew - p->rankOld, 0));
            }
            row.cells.push_back(pr);
        }
        std::string cs = m.checksum;
        row.click = [&app, cs] { app.openMatch(cs); };
        row.key = cs;
        rows.push_back(std::move(row));
    }
    SortState& st = app.sort("matches", 0, true);
    sortRows(rows, st.col, st.desc);
    RectF card(x, y, cw, 0);
    float tableH = 34 + rows.size() * 52 + 12;
    c.card(RectF(x, y, cw, tableH + 8));
    TableOpts o;
    o.key = "matches";
    o.rowH = 52;
    o.sortCol = st.col;
    o.sortDesc = st.desc;
    o.onSort = [&app](int col) {
        SortState& s = app.sort("matches", 0, true);
        if (s.col == col) s.desc = !s.desc;
        else {
            s.col = col;
            s.desc = col != 1 && col != 2;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 6, cw - 8, cols, rows, o);
    y += tableH + 8;
    if (rows.empty()) c.text(L"Keine Matches in diesem Modus", F::Body, c.th.text3, RectF(x, y - 60, cw, 30), Align::Center);
    return y + 24;
}

}  // namespace fdw
