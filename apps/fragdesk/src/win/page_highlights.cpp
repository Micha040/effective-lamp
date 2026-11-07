// FragDesk – Seite „Momente“: Vorschläge aus allen Matches und aufgenommene Videos
#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }

float recordingCard(Canvas& c, App& app, float x, float y, float w) {
    if (!app.recording()) return 0;
    RectF r(x, y, w, 64);
    c.round(r, 12, alpha(c.th.accent, c.th.dark ? 30 : 46));
    c.roundStroke(r, 12, alpha(c.th.accent, 150), 1);
    c.icon(Icon::Record, RectF(r.X + 18, r.Y + 22, 20, 20), c.th.danger);
    c.text(L"Aufnahme läuft", F::BodySemi, c.th.text, RectF(r.X + 50, r.Y + 10, w - 200, 22));
    c.text(app.recordStatus(), F::Small, c.th.text2, RectF(r.X + 50, r.Y + 32, w - 200, 20));
    c.button(RectF(r.X + w - 128, r.Y + 15, 112, 34), L"Abbrechen", "rec-cancel", [&app] { app.cancelRecording(); }, Btn::Danger);
    return 64 + 16;
}

float suggestions(Canvas& c, App& app, float x, float y, float w) {
    float y0 = y;
    c.chipRow(x, y, {L"Meine Momente", L"Alle Spieler"}, app.allPlayers ? 1 : 0, "hl-who", [&app](int i) {
        app.allPlayers = i == 1;
        app.invalidate();
    });
    std::vector<MomentRef> refs;
    for (const auto& key : app.selected) {
        size_t slash = key.find('/');
        if (slash != std::string::npos) refs.push_back({key.substr(0, slash), key.substr(slash + 1)});
    }
    std::wstring recLabel = refs.empty() ? L"Ausgewählte aufnehmen" : L"Ausgewählte aufnehmen (" + std::to_wstring(refs.size()) + L")";
    float bw = c.buttonWidth(recLabel, Icon::Record);
    std::wstring block = app.recording()             ? L"Es läuft bereits eine Aufnahme"
                         : app.steam.cs2Exe().empty() ? L"CS2 wurde nicht gefunden – Ordner in den Einstellungen wählen"
                         : refs.empty()               ? L"Erst Momente links ankreuzen"
                                                      : L"";
    c.button(RectF(x + w - bw, y - 3, bw, 36), recLabel, "hl-rec", [&app, refs] { app.record(refs); }, Btn::Primary, Icon::Record, block.empty(),
             block);
    if (!refs.empty()) {
        float b2 = c.buttonWidth(L"Auswahl leeren");
        c.button(RectF(x + w - bw - b2 - 8, y - 3, b2, 36), L"Auswahl leeren", "hl-clear", [&app] { app.selected.clear(); app.invalidate(); },
                 Btn::Ghost);
    }
    y += 50;

    struct Mom {
        const fd::Match* m;
        const fd::HighlightInfo* h;
    };
    std::vector<Mom> moms;
    fd::Filter f = app.filter();
    for (const auto& mp : app.lib.matches) {
        if (!f.mode.empty() && mp->mode != f.mode) continue;
        if (!f.map.empty() && mp->map != f.map) continue;
        for (const auto& h : mp->highlights)
            if (app.allPlayers || h.player == app.me) moms.push_back({mp.get(), &h});
    }
    std::stable_sort(moms.begin(), moms.end(), [](const Mom& a, const Mom& b) {
        if (a.h->score != b.h->score) return a.h->score > b.h->score;
        return a.m->date > b.m->date;
    });
    if (moms.empty()) {
        y += emptyState(c, x, y, w, Icon::Spark, L"Noch keine Momente",
                        L"Momente erkennt FragDesk beim Auswerten: Multikills, gewonnene Clutches, Noscopes, Wallbangs, Kills durch Smoke und mehr.");
        return y - y0;
    }
    size_t limit = std::min<size_t>(moms.size(), 60);
    for (size_t i = 0; i < limit; ++i) {
        const Mom& mo = moms[i];
        std::string key = mo.m->checksum + "/" + mo.h->id;
        bool on = app.selected.count(key) > 0;
        RectF rr(x, y, w, 64);
        if (!c.visible(rr)) {
            y += 72;
            continue;
        }
        uint32_t id = hashId("hl:" + key);
        c.round(rr, 12, on ? alpha(c.th.accent, c.th.dark ? 26 : 40) : c.hot(id) ? c.th.hover : c.th.card);
        c.roundStroke(rr, 12, on ? alpha(c.th.accent, 160) : c.th.border, 1);
        c.region(rr, id, nullptr, L"", false);   // zuerst: Kästchen, Titel und Knöpfe darüber bleiben anklickbar
        RectF box(rr.X + 16, rr.Y + 21, 22, 22);
        c.round(box, 6, on ? c.th.accent : c.th.btn);
        if (on) c.icon(Icon::Check, RectF(box.X + 3, box.Y + 3, 16, 16), c.th.onAccent, 2.2f);
        c.region(RectF(rr.X, rr.Y, 52, rr.Height), hashId("hlsel:" + key), [&app, key] {
            if (app.selected.count(key)) app.selected.erase(key);
            else app.selected.insert(key);
            app.invalidate();
        });
        float tx = rr.X + 54;
        c.round(RectF(tx, rr.Y + 14, 6, 36), 3, rgb(fd::mapColor(mo.m->map)));
        tx += 16;
        std::wstring title = W(fd::highlightTitle(*mo.h));
        const fd::PlayerInfo* p = mo.m->player(mo.h->player);
        if (app.allPlayers && p) title = W(p->name) + L": " + title;
        c.text(title, F::BodySemi, c.th.text, RectF(tx, rr.Y + 9, rr.Width - 470, 24));
        std::wstring meta = matchTitle(*mo.m, app.me) + L" · Runde " + std::to_wstring(mo.h->round) + L" · " +
                            W(fd::fmtDec(mo.h->seconds(mo.m->tickrate), 0)) + L" s · " + relativeDay(matchEpoch(*mo.m));
        c.text(meta, F::Small, c.th.text3, RectF(tx, rr.Y + 33, rr.Width - 470, 20));
        std::string cs = mo.m->checksum;
        c.region(RectF(tx, rr.Y, rr.Width - 470, rr.Height), hashId("hlopen:" + key), [&app, cs] { app.openMatch(cs, 6); }, L"Match öffnen");
        int stars = std::max(1, std::min(5, mo.h->score / 40 + 1));
        for (int s = 0; s < 5; ++s)
            c.icon(Icon::Star, RectF(rr.X + rr.Width - 402 + s * 15, rr.Y + 25, 14, 14), s < stars ? c.th.accent : c.th.track);
        MomentRef ref{mo.m->checksum, mo.h->id};
        float bx = rr.X + rr.Width - 316;
        std::wstring block = app.momentBlocker(*mo.m);
        c.button(RectF(bx, rr.Y + 14, 150, 36), L"In CS2 ansehen", "hw:" + key, [&app, ref] { app.watch(ref); }, Btn::Secondary, Icon::Eye,
                 block.empty(), block);
        if (const Clip* clip = app.clipFor(mo.m->checksum, mo.h->id)) {
            std::wstring path = clip->path;
            c.button(RectF(bx + 158, rr.Y + 14, 150, 36), L"Video öffnen", "hv:" + key, [path] { openInShell(path); }, Btn::Secondary, Icon::Play);
        } else {
            c.button(RectF(bx + 158, rr.Y + 14, 150, 36), L"Aufnehmen", "hr:" + key, [&app, ref] { app.record({ref}); }, Btn::Secondary, Icon::Record,
                     block.empty(), block);
        }
        y += 72;
    }
    if (moms.size() > limit)
        c.text(L"Die " + std::to_wstring(limit) + L" besten von " + std::to_wstring(moms.size()) + L" Momenten", F::Small, c.th.text3, RectF(x, y, w, 20));
    return y - y0 + 30;
}

float videos(Canvas& c, App& app, float x, float y, float w) {
    float y0 = y;
    std::wstring dir = app.settings.effectiveVideoDir();
    c.text(L"Gespeichert in " + dir, F::Small, c.th.text2, RectF(x, y, w - 200, 30));
    float bw = c.buttonWidth(L"Ordner öffnen", Icon::Folder);
    c.button(RectF(x + w - bw, y - 2, bw, 34), L"Ordner öffnen", "v-open", [dir] {
        createDirs(dir);
        openInShell(dir);
    }, Btn::Secondary, Icon::Folder);
    y += 46;
    if (app.clips.empty()) {
        y += emptyState(c, x, y, w, Icon::Film, L"Noch keine Videos",
                        L"Wähle bei „Vorschläge“ oder in einem Match Momente aus und klicke „Aufnehmen“. CS2 spielt sie dann ab, FragDesk nimmt sie "
                        L"auf und legt die Videos hier ab.");
        return y - y0;
    }
    std::vector<Col> cols = {{L"VIDEO", -3}, {L"MATCH", -2}, {L"LÄNGE", 70, Align::Right}, {L"GRÖSSE", 80, Align::Right}, {L"", 210}};
    std::vector<Row> rows;
    for (const auto& cl : app.clips) {
        Row row;
        Cell t(cl.title);
        t.font = F::BodySemi;
        t.icon = Icon::Play;
        t.iconColor = c.th.accentText;
        t.sub = relativeDay(cl.created);
        row.cells.push_back(t);
        const fd::Match* m = app.lib.find(cl.match);
        row.cells.push_back(Cell(m ? matchTitle(*m, app.me) : W(fd::mapName(cl.map))));
        row.cells.push_back(Cell(W(fd::fmtClock(cl.seconds)), cl.seconds));
        row.cells.push_back(Cell(W(fd::fmtBytes(double(cl.size))), double(cl.size)));
        row.cells.push_back(Cell(L""));
        std::wstring path = cl.path;
        row.click = [path] { openInShell(path); };
        row.key = narrow(cl.path);
        rows.push_back(row);
    }
    float th = 34 + rows.size() * 52 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "clips";
    o.rowH = 52;
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    // Aktionen pro Zeile
    float ry = y + 4 + 34;
    for (size_t i = 0; i < app.clips.size(); ++i) {
        std::wstring path = app.clips[i].path;
        c.button(RectF(x + w - 214, ry + 9, 110, 34), L"Im Ordner", "cf:" + narrow(path), [path] { showInExplorer(path); }, Btn::Ghost, Icon::Folder);
        c.iconButton(RectF(x + w - 96, ry + 9, 34, 34), Icon::Trash, "cd:" + narrow(path),
                     [&app, path] {
                         if (MessageBoxW(app.hwnd, (L"Video löschen?\n\n" + path).c_str(), L"FragDesk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
                         DeleteFileW(path.c_str());
                         app.clips.erase(std::remove_if(app.clips.begin(), app.clips.end(), [&](const Clip& x) { return x.path == path; }),
                                         app.clips.end());
                         app.saveClips();
                         app.invalidate();
                     },
                     L"Video löschen");
        ry += 52;
    }
    y += th + 10;
    return y - y0;
}

}  // namespace

float drawHighlights(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = w - pad * 2, y = 28;
    y += pageTitle(c, x, y, cw, L"Momente", L"Erkannte Highlights ansehen und als Video aufnehmen") + 12;
    y += recordingCard(c, app, x, y, cw);
    y += c.tabs(x, y, cw, {L"Vorschläge", L"Videos (" + std::to_wstring(app.clips.size()) + L")"}, app.highlightsTab, "htab", [&app](int i) {
        app.highlightsTab = i;
        app.invalidate();
    }) + 20;
    if (app.highlightsTab == 0) y += suggestions(c, app, x, y, cw);
    else y += videos(c, app, x, y, cw);
    return y + 20;
}

}  // namespace fdw
