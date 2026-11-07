// FragDesk – Seite eines Matches: Übersicht, Scoreboard, Runden, Waffen, Duelle, Utility, Momente
#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }
std::wstring S(const char* id, const fd::Counters& c) {
    const fd::Stat* s = fd::findStat(id);
    return s ? W(fd::formatStat(s->expr.eval(c), s->def->fmt)) : L"–";
}
double V(const char* id, const fd::Counters& c) { return fd::statValue(id, c); }

std::wstring clockAt(const fd::Match& m, int round, int tick) {
    for (const auto& r : m.rounds)
        if (r.n == round) return W(fd::fmtClock((tick - r.freezeEnd) / m.tickrate));
    return L"";
}

const fd::PlayerInfo* focus(App& app, const fd::Match& m) {
    if (!app.focusPlayer.empty())
        if (const fd::PlayerInfo* p = m.player(app.focusPlayer)) return p;
    if (const fd::PlayerInfo* p = m.player(app.me)) return p;
    return m.players.empty() ? nullptr : &m.players[0];
}

float focusSelector(Canvas& c, App& app, const fd::Match& m, const fd::PlayerInfo* p, float x, float y) {
    std::wstring label = L"Werte von " + W(p ? p->name : "–");
    float bw = c.measure(label, F::SmallSemi) + 66;   // Symbol links, Pfeil rechts
    RectF br(x, y, bw, 32);
    uint32_t id = hashId("focus-sel");
    c.round(br, 16, c.hot(id) ? c.th.btnHover : c.th.btn);
    c.icon(Icon::Users, RectF(br.X + 10, y + 8, 16, 16), c.th.text2, 1.6f);
    c.text(label, F::SmallSemi, c.th.text, RectF(br.X + 32, y, bw - 58, 32));
    c.icon(Icon::ChevronDown, RectF(br.X + bw - 20, y + 10, 12, 12), c.th.text2, 1.6f);
    std::vector<std::pair<std::string, std::wstring>> players;
    for (const auto& pl : m.players)
        players.push_back({pl.steamId, W(pl.name) + (pl.steamId == app.me ? L"  (du)" : L"") + L"  – Team " + W(pl.team)});
    c.region(br, id, [&app, players] {
        HMENU menu = CreatePopupMenu();
        for (size_t i = 0; i < players.size(); ++i)
            AppendMenuW(menu, MF_STRING | (app.focusPlayer == players[i].first ? MF_CHECKED : 0), UINT(i + 1), players[i].second.c_str());
        POINT cur;
        GetCursorPos(&cur);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cur.x, cur.y + 12, 0, app.hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd > 0 && size_t(cmd) <= players.size()) {
            app.focusPlayer = players[cmd - 1].first;
            app.invalidate();
        }
    });
    return 32;
}

// ---- Übersicht ------------------------------------------------------------------------------------------------
float tabOverview(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    const fd::PlayerInfo* p = focus(app, m);
    if (!p) return 0;
    float y0 = y;
    y += focusSelector(c, app, m, p, x, y) + 16;
    fd::Counters cnt = fd::matchCounters(*p, p->all);
    struct K {
        const wchar_t* label;
        std::wstring value, sub;
        Color col;
        const char* hint;
    };
    std::wstring kda = W(fd::fmtInt(p->stat("kills"))) + L" / " + W(fd::fmtInt(p->stat("deaths"))) + L" / " +
                       W(fd::fmtInt(p->stat("assists") + p->stat("flashAssists")));
    std::vector<K> ks = {
        {L"Rating 2.0", W(fd::fmtDec(p->rating, 2)), L"Rating 1.0: " + W(fd::fmtDec(p->rating1, 2)), ratingColor(c.th, p->rating), "rating"},
        {L"K / D / A", kda, L"K/D " + S("kd", cnt) + L" · " + S("kdDiff", cnt), Color(0, 0, 0, 0), ""},
        {L"ADR", W(fd::fmtDec(p->adr, 1)), L"Utility " + S("utilDmgPerRound", cnt) + L" pro Runde", Color(0, 0, 0, 0), "adr"},
        {L"KAST", W(fd::fmtPct(p->kast, 0)), L"Überlebt " + S("survival", cnt), Color(0, 0, 0, 0), "kast"},
        {L"Headshots", S("hsPct", cnt), S("hsKills", cnt) + L" Kopfschuss-Kills", Color(0, 0, 0, 0), "hsPct"},
        {L"Eröffnungsduelle", W(fd::fmtInt(p->stat("openKills"))) + L" : " + W(fd::fmtInt(p->stat("openDeaths"))),
         L"gewonnen " + S("openSuccess", cnt), Color(0, 0, 0, 0), "openSuccess"},
        {L"Clutches", S("clutchWins", cnt) + L" / " + S("clutches", cnt), L"Kills im Clutch: " + S("clutchKills", cnt), Color(0, 0, 0, 0), "clutches"},
        {L"Treffsicherheit", S("accuracy", cnt), L"Erster Schuss " + S("firstShotAcc", cnt), Color(0, 0, 0, 0), "accuracy"},
        {L"Multikills", S("multiRounds", cnt), L"3K " + S("k3", cnt) + L" · 4K " + S("k4", cnt) + L" · Ass " + S("k5", cnt), Color(0, 0, 0, 0),
         "multiRounds"},
        {L"Geblendete Gegner", S("enemiesFlashed", cnt), L"Ø " + S("blindTime", cnt) + L" · Flash-Assists " + S("flashAssists", cnt),
         Color(0, 0, 0, 0), "enemiesFlashed"},
    };
    if (p->rankType == 11 && (p->rankNew > 0 || p->rankOld > 0)) {
        int v = p->rankNew > 0 ? p->rankNew : p->rankOld;
        std::wstring sub = p->rankOld > 0 && p->rankNew > 0 ? L"vorher " + W(fd::fmtRating(p->rankOld)) + L" (" +
                                                                    W(fd::fmtSigned(p->rankNew - p->rankOld, 0)) + L")"
                                                              : L"";
        ks.insert(ks.begin(), {L"Premier-Rating", W(fd::fmtRating(v)), sub, rgb(fd::premierColor(v)), ""});
    }
    int perRow = w >= 1000 ? 5 : w >= 760 ? 4 : 3;
    float gap = 14, kw = (w - gap * (perRow - 1)) / perRow, kh = 100;
    for (size_t i = 0; i < ks.size(); ++i) {
        float kx = x + (i % perRow) * (kw + gap), ky = y + (i / perRow) * (kh + gap);
        const fd::Stat* s = *ks[i].hint ? fd::findStat(ks[i].hint) : nullptr;
        statCard(c, RectF(kx, ky, kw, kh), ks[i].label, ks[i].value, ks[i].sub, s ? W(s->def->hint) : L"", ks[i].col);
    }
    y += ((ks.size() + perRow - 1) / perRow) * (kh + gap) + 8;

    // Rundenleiste
    y += sectionTitle(c, x, y, w, L"Runden", L"Grün gewonnen, Rot verloren · Punkte = Kills · Stern = Clutch · × = gestorben · Klick: 2D-Ansicht");
    std::vector<RoundMark> marks;
    int prevHalf = -1;
    for (const auto& r : m.rounds) {
        const fd::PlayerRound* pr = p->round(r.n);
        RoundMark mk;
        mk.n = r.n;
        mk.won = pr ? pr->won : false;
        mk.dead = pr && pr->dead;
        mk.kills = pr ? pr->k : 0;
        mk.clutchWon = pr && pr->clutchWon;
        mk.side = pr ? pr->side : "";
        int half = r.n <= 12 ? 0 : r.n <= 24 ? 1 : 2 + (r.n - 25) / 3;
        mk.half = prevHalf >= 0 && half != prevHalf;
        prevHalf = half;
        std::wstring tip = L"Runde " + std::to_wstring(r.n) + L" (" + W(pr ? pr->side : "") + L") – " + (mk.won ? L"gewonnen" : L"verloren") +
                           L"\n" + W(fd::reasonName(r.reason));
        if (pr) {
            tip += L"\n" + std::to_wstring(pr->k) + L" Kills, " + std::to_wstring(pr->dmg) + L" Schaden";
            if (pr->dead) tip += L"\nGestorben bei " + clockAt(m, r.n, pr->deathTick) + (pr->killerWeapon.empty() ? L"" : L" (" + W(pr->killerWeapon) + L")");
            if (pr->clutch) tip += L"\nClutch 1v" + std::to_wstring(pr->clutch) + (pr->clutchWon ? L" gewonnen" : L" verloren");
            tip += L"\n" + W(fd::econName(pr->econ)) + L", Ausrüstung " + W(fd::fmtMoney(pr->equip));
        }
        mk.tip = tip + L"\n\nKlick: Runde in der 2D-Ansicht";
        int n = r.n;
        mk.click = [&app, n] { app.openReplay(n); };
        marks.push_back(mk);
    }
    roundStrip(c, RectF(x, y, w, 44), marks, "ov-" + m.checksum);
    y += 44 + 32;

    // Momente
    std::vector<const fd::HighlightInfo*> hs;
    for (const auto& h : m.highlights)
        if (h.player == p->steamId) hs.push_back(&h);
    if (!hs.empty()) {
        y += sectionTitle(c, x, y, w, L"Momente", L"Automatisch erkannt – ansehen oder aufnehmen im Tab „Momente“");
        for (size_t i = 0; i < hs.size() && i < 4; ++i) {
            const auto* h = hs[i];
            RectF rr(x, y, w, 50);
            c.card(rr, 10);
            c.icon(h->clutch ? Icon::Star : Icon::Spark, RectF(rr.X + 14, rr.Y + 16, 18, 18), c.th.accentText, 1.6f);
            c.text(W(fd::highlightTitle(*h)), F::BodySemi, c.th.text, RectF(rr.X + 42, rr.Y, rr.Width - 330, 50));
            std::wstring meta = L"Runde " + std::to_wstring(h->round) + L" · " + W(fd::fmtDec(h->seconds(m.tickrate), 0)) + L" s";
            c.text(meta, F::Small, c.th.text3, RectF(rr.X + rr.Width - 300, rr.Y, 120, 50), Align::Right);
            MomentRef ref{m.checksum, h->id};
            std::wstring block = app.momentBlocker(m);
            c.button(RectF(rr.X + rr.Width - 168, rr.Y + 9, 154, 32), L"In CS2 ansehen", "ovw:" + h->id, [&app, ref] { app.watch(ref); },
                     Btn::Secondary, Icon::Eye, block.empty(), block);
            y += 58;
        }
        y += 8;
    }
    return y - y0;
}

// ---- Scoreboard -----------------------------------------------------------------------------------------------
float tabScoreboard(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    float y0 = y;
    bool premier = false;
    for (const auto& p : m.players)
        if (p.rankType == 11 && (p.rankNew > 0 || p.rankOld > 0)) premier = true;
    for (int t = 0; t < 2; ++t) {
        const fd::TeamInfo& team = m.teams[t];
        int score = m.scoreOf(team.letter), other = m.scoreOf(team.letter == "A" ? "B" : "A");
        Color sc = score > other ? c.th.success : score < other ? c.th.danger : c.th.text2;
        std::wstring name = W(team.name);
        if (name.empty() || name == L"Team A" || name == L"Team B") name = L"Team " + W(team.letter);
        bool mine = false;
        for (const auto& p : m.players)
            if (p.team == team.letter && p.steamId == app.me) mine = true;
        if (mine) name += L" (dein Team)";
        c.text(std::to_wstring(score), F::NumBig, sc, RectF(x, y, 60, 40));
        c.text(name, F::H2, c.th.text, RectF(x + 56, y, w - 300, 24));
        c.text(L"Start als " + W(team.startSide) + L" · 1. Hälfte " + std::to_wstring(team.scoreFirstHalf) + L" · 2. Hälfte " +
                   std::to_wstring(team.scoreSecondHalf),
               F::Small, c.th.text3, RectF(x + 56, y + 22, w - 300, 18));
        y += 50;
        std::vector<Col> cols = {
            {L"SPIELER", -3},
            {L"K", 44, Align::Right},
            {L"D", 44, Align::Right},
            {L"A", 44, Align::Right},
            {L"+/−", 50, Align::Right},
            {L"ADR", 58, Align::Right},
            {L"KAST", 60, Align::Right},
            {L"HS", 52, Align::Right},
            {L"RATING", 64, Align::Right},
            {L"EK", 42, Align::Right, L"Eröffnungskills"},
            {L"ET", 42, Align::Right, L"Eröffnungstode"},
            {L"MK", 42, Align::Right, L"Multikill-Runden (2+ Kills)"},
            {L"CL", 48, Align::Right, L"Clutches gewonnen / gespielt"},
            {L"GB", 44, Align::Right, L"Geblendete Gegner"},
            {L"UD", 50, Align::Right, L"Utility-Schaden"},
            {L"MVP", 46, Align::Right},
        };
        if (premier) cols.push_back({L"PREMIER", 120, Align::Right});
        std::vector<Row> rows;
        for (const auto& p : m.players) {
            if (p.team != team.letter) continue;
            fd::Counters cnt = fd::matchCounters(p, p.all);
            Row row;
            Cell name(W(p.name), NAN);
            name.font = F::BodySemi;
            if (!p.crosshair.empty()) name.tip = L"Fadenkreuz-Code: " + W(p.crosshair);
            row.cells.push_back(name);
            auto num = [&](double v) { return Cell(W(fd::fmtInt(v)), v); };
            row.cells.push_back(num(p.stat("kills")));
            row.cells.push_back(num(p.stat("deaths")));
            row.cells.push_back(num(p.stat("assists") + p.stat("flashAssists")));
            double diff = p.stat("kills") - p.stat("deaths");
            Cell d(W(fd::fmtSigned(diff, 0)), diff);
            d.color = diff > 0 ? c.th.success : diff < 0 ? c.th.danger : c.th.text2;
            row.cells.push_back(d);
            row.cells.push_back(Cell(W(fd::fmtDec(p.adr, 1)), p.adr));
            row.cells.push_back(Cell(W(fd::fmtPct(p.kast, 0)), p.kast));
            double hs = V("hsPct", cnt);
            row.cells.push_back(Cell(W(fd::formatStat(hs, fd::Fmt::Pct0)), hs));
            Cell rat(W(fd::fmtDec(p.rating, 2)), p.rating);
            rat.color = ratingColor(c.th, p.rating);
            rat.font = F::BodySemi;
            row.cells.push_back(rat);
            row.cells.push_back(num(p.stat("openKills")));
            row.cells.push_back(num(p.stat("openDeaths")));
            row.cells.push_back(num(V("multiRounds", cnt)));
            double cw = V("clutchWins", cnt), ca = V("clutches", cnt);
            row.cells.push_back(Cell(W(fd::fmtInt(cw)) + L"/" + W(fd::fmtInt(ca)), cw * 100 + ca));
            row.cells.push_back(num(p.stat("enemiesFlashed")));
            row.cells.push_back(num(p.stat("utilDmg")));
            row.cells.push_back(num(p.mvps));
            if (premier) {
                Cell pr(L"", NAN);
                if (p.rankType == 11 && (p.rankNew > 0 || p.rankOld > 0)) {
                    int v = p.rankNew > 0 ? p.rankNew : p.rankOld;
                    pr.text = W(fd::fmtRating(v));
                    pr.color = rgb(fd::premierColor(v));
                    pr.font = F::BodySemi;
                    pr.sort = v;
                    if (p.rankOld > 0 && p.rankNew > 0 && p.rankOld != p.rankNew) pr.sub = W(fd::fmtSigned(p.rankNew - p.rankOld, 0));
                } else pr.text = L"–";
                row.cells.push_back(pr);
            }
            row.highlight = p.steamId == app.me;
            std::string sid = p.steamId;
            row.click = [&app, sid] {
                app.focusPlayer = sid;
                app.matchTab = 0;
                app.invalidate();
            };
            row.key = sid;
            rows.push_back(row);
        }
        std::string key = "sb" + std::to_string(t);
        SortState& st = app.sort(key, 8, true);
        sortRows(rows, st.col, st.desc);
        float th = 34 + rows.size() * 46 + 12;
        c.card(RectF(x, y, w, th));
        TableOpts o;
        o.key = key;
        o.rowH = 46;
        o.sortCol = st.col;
        o.sortDesc = st.desc;
        o.onSort = [&app, key](int col) {
            SortState& s = app.sort(key, 8, true);
            if (s.col == col) s.desc = !s.desc;
            else {
                s.col = col;
                s.desc = col != 0;
            }
            app.invalidate();
        };
        table(c, x + 4, y + 4, w - 8, cols, rows, o);
        y += th + 26;
    }
    c.text(L"Klick auf einen Spieler zeigt seine Werte in der Übersicht.", F::Small, c.th.text3, RectF(x, y - 14, w, 18));
    return y - y0 + 10;
}

// ---- Runden ---------------------------------------------------------------------------------------------------
float tabRounds(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    const fd::PlayerInfo* p = focus(app, m);
    if (!p) return 0;
    float y0 = y;
    y += focusSelector(c, app, m, p, x, y) + 16;
    std::vector<Col> cols = {
        {L"#", 38, Align::Right},
        {L"SEITE", 54, Align::Center},
        {L"ERGEBNIS", -3},
        {L"STAND", 64, Align::Center},
        {L"KAUF", -2, Align::Left, L"Kaufart deines Teams und dein Ausrüstungswert"},
        {L"K", 38, Align::Right},
        {L"SCHADEN", 74, Align::Right},
        {L"TOD", -3, Align::Left, L"Zeitpunkt nach der Kaufphase und Waffe des Gegners"},
        {L"", 96, Align::Left},
    };
    std::vector<Row> rows;
    for (const auto& r : m.rounds) {
        const fd::PlayerRound* pr = p->round(r.n);
        Row row;
        row.cells.push_back(Cell(std::to_wstring(r.n), r.n));
        Cell side(pr ? W(pr->side) : L"–");
        side.color = pr && pr->side == "T" ? c.th.tSide : c.th.ctSide;
        side.font = F::SmallSemi;
        row.cells.push_back(side);
        bool won = pr && pr->won;
        Cell res(W(fd::reasonName(r.reason)));
        res.icon = r.reason == "bomb" ? Icon::Bomb : r.reason == "defuse" ? Icon::Shield : r.reason == "time" ? Icon::Clock : Icon::Crosshair;
        res.iconColor = won ? c.th.success : c.th.danger;
        res.sub = won ? L"gewonnen" : L"verloren";
        row.cells.push_back(res);
        int mine = p->team == "A" ? r.scoreA : r.scoreB, theirs = p->team == "A" ? r.scoreB : r.scoreA;
        row.cells.push_back(Cell(std::to_wstring(mine) + L":" + std::to_wstring(theirs)));
        std::string econ = p->team == "A" ? r.econA : r.econB;
        Cell buy(W(fd::econName(econ)));
        if (pr) buy.sub = W(fd::fmtMoney(pr->equip)) + L" Ausrüstung";
        row.cells.push_back(buy);
        Cell k(pr && pr->k ? std::to_wstring(pr->k) : L"–", pr ? pr->k : 0);
        k.font = pr && pr->k >= 3 ? F::BodySemi : F::Body;
        if (pr && pr->k >= 3) k.color = c.th.accentText;
        row.cells.push_back(k);
        row.cells.push_back(Cell(pr ? std::to_wstring(pr->dmg) : L"–", pr ? pr->dmg : 0));
        Cell death(L"überlebt");
        death.color = c.th.text3;
        if (pr && pr->dead) {
            death.text = clockAt(m, r.n, pr->deathTick) + L" – " + W(m.playerName(pr->killer).empty() ? "Welt" : m.playerName(pr->killer));
            death.sub = W(pr->killerWeapon) + (pr->traded ? L" · getradet" : L"");
            death.color = c.th.text;
        }
        row.cells.push_back(death);
        std::wstring tags;
        if (pr) {
            if (pr->open == "K") tags += L"Eröffnung ";
            if (pr->open == "D") tags += L"Erster Tod ";
            if (pr->clutch) tags += L"1v" + std::to_wstring(pr->clutch) + (pr->clutchWon ? L"✓ " : L" ");
            if (pr->plant) tags += L"Plant ";
            if (pr->defuse) tags += L"Defuse ";
        }
        Cell t(tags);
        t.font = F::Tiny;
        t.color = c.th.text2;
        row.cells.push_back(t);
        row.accentBar = won ? c.th.success : c.th.danger;
        int n = r.n;
        row.click = [&app, n] { app.openReplay(n); };
        rows.push_back(row);
    }
    float th = 34 + rows.size() * 48 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "rounds";
    o.rowH = 48;
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 10;
    return y - y0;
}

// ---- Waffen ---------------------------------------------------------------------------------------------------
float tabWeapons(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    const fd::PlayerInfo* p = focus(app, m);
    if (!p) return 0;
    float y0 = y;
    y += focusSelector(c, app, m, p, x, y) + 16;
    std::vector<Col> cols = {
        {L"WAFFE", -2},
        {L"KILLS", 56, Align::Right},
        {L"HS", 54, Align::Right, L"Headshot-Quote der Kills"},
        {L"SCHADEN", 72, Align::Right},
        {L"SCHÜSSE", 70, Align::Right},
        {L"TREFFER", 70, Align::Right, L"Anteil der Schüsse, die getroffen haben"},
        {L"1. SCHUSS", 78, Align::Right, L"Treffsicherheit des ersten Schusses"},
        {L"SPRAY", 64, Align::Right, L"Treffsicherheit ab der 4. Kugel"},
        {L"BEWEGT", 68, Align::Right, L"Anteil der Schüsse in Bewegung"},
        {L"KOPF", 58, Align::Right, L"Kopftreffer unter allen Treffern"},
        {L"Ø DIST.", 66, Align::Right, L"Durchschnittliche Kill-Distanz"},
    };
    std::vector<Row> rows;
    for (const auto& wp : p->weapons) {
        if (wp.c.get("kills") == 0 && wp.c.get("shots") == 0 && wp.c.get("dmg") == 0) continue;
        Row row;
        Cell n(W(wp.name));
        n.font = F::BodySemi;
        n.sub = W(wp.type == "rifle" ? "Gewehr" : wp.type == "sniper" ? "Scharfschützengewehr" : wp.type == "smg" ? "Maschinenpistole" :
                  wp.type == "pistol" ? "Pistole" : wp.type == "shotgun" ? "Schrotflinte" : wp.type == "machine_gun" ? "Maschinengewehr" :
                  wp.type == "grenade" ? "Granate" : wp.type == "melee" ? "Messer" : "Ausrüstung");
        row.cells.push_back(n);
        auto stat = [&](const char* id) {
            const fd::Stat* s = fd::findStat(id);
            double v = s->expr.eval(wp.c);
            return Cell(W(fd::formatStat(v, s->def->fmt)), v);
        };
        row.cells.push_back(stat("kills"));
        row.cells.push_back(stat("hsPct"));
        row.cells.push_back(Cell(W(fd::fmtInt(wp.c.get("dmg"))), wp.c.get("dmg")));
        row.cells.push_back(stat("shots"));
        row.cells.push_back(stat("accuracy"));
        row.cells.push_back(stat("firstShotAcc"));
        row.cells.push_back(stat("sprayAcc"));
        row.cells.push_back(stat("movingPct"));
        row.cells.push_back(stat("headHitPct"));
        row.cells.push_back(stat("avgDist"));
        rows.push_back(row);
    }
    SortState& st = app.sort("mweapons", 1, true);
    sortRows(rows, st.col, st.desc);
    float th = 34 + std::max<size_t>(rows.size(), 1) * 50 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "mweapons";
    o.rowH = 50;
    o.sortCol = st.col;
    o.sortDesc = st.desc;
    o.onSort = [&app](int col) {
        SortState& s = app.sort("mweapons", 1, true);
        if (s.col == col) s.desc = !s.desc;
        else {
            s.col = col;
            s.desc = col != 0;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 24;

    // Trefferbild und Gestorben durch
    float colW = (w - 16) / 2;
    float h = 300;
    c.card(RectF(x, y, colW, h));
    c.text(L"Treffer nach Körperzonen", F::H3, c.th.text, RectF(x + 18, y + 14, colW - 36, 22));
    fd::Counters cnt = p->all;
    hitFigure(c, RectF(x + 10, y + 46, colW - 20, h - 56), V("headHitPct", cnt), V("hitNeckPct", cnt), V("hitChestPct", cnt), V("hitStomachPct", cnt),
              V("hitArmsPct", cnt), V("hitLegsPct", cnt));
    RectF r2(x + colW + 16, y, colW, h);
    c.card(r2);
    c.text(L"Gestorben durch", F::H3, c.th.text, RectF(r2.X + 18, r2.Y + 14, colW - 36, 22));
    std::vector<Bar> bars;
    double mx = p->deathsBy.empty() ? 1 : p->deathsBy[0].second;
    for (size_t i = 0; i < p->deathsBy.size() && i < 6; ++i) {
        Bar b;
        b.label = W(p->deathsBy[i].first);
        b.v = p->deathsBy[i].second;
        b.value = std::to_wstring(p->deathsBy[i].second) + L"×";
        b.color = c.th.danger;
        bars.push_back(b);
    }
    barList(c, r2.X + 18, r2.Y + 52, r2.Width - 36, bars, mx, 38, 140);
    if (bars.empty()) c.text(L"Kein einziger Tod", F::Body, c.th.text2, RectF(r2.X + 18, r2.Y + 56, 300, 22));
    y += h + 10;
    return y - y0;
}

// ---- Duelle ---------------------------------------------------------------------------------------------------
float tabDuels(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    const fd::PlayerInfo* p = focus(app, m);
    if (!p) return 0;
    float y0 = y;
    y += focusSelector(c, app, m, p, x, y) + 16;
    std::vector<Col> cols = {
        {L"GEGNER", -3},
        {L"KILLS", 72, Align::Right},
        {L"TODE", 68, Align::Right},
        {L"SALDO", 80, Align::Right},
        {L"AUSGETEILT", 112, Align::Right, L"Schaden, den du diesem Gegner zugefügt hast"},
        {L"ERHALTEN", 104, Align::Right, L"Schaden, den dieser Gegner dir zugefügt hat"},
        {L"ERÖFFNUNG", 96, Align::Right, L"Eröffnungsduelle gegen diesen Gegner: gewonnen : verloren"},
    };
    std::vector<Row> rows;
    for (const auto& o : p->opponents) {
        const fd::PlayerInfo* op = m.player(o.steamId);
        if (!op || op->team == p->team) continue;
        Row row;
        Cell n(W(op->name));
        n.font = F::BodySemi;
        row.cells.push_back(n);
        row.cells.push_back(Cell(std::to_wstring(o.k), o.k));
        row.cells.push_back(Cell(std::to_wstring(o.d), o.d));
        Cell d(W(fd::fmtSigned(o.k - o.d, 0)), o.k - o.d);
        d.color = o.k > o.d ? c.th.success : o.k < o.d ? c.th.danger : c.th.text2;
        d.font = F::BodySemi;
        row.cells.push_back(d);
        row.cells.push_back(Cell(std::to_wstring(o.dmgDealt), o.dmgDealt));
        row.cells.push_back(Cell(std::to_wstring(o.dmgTaken), o.dmgTaken));
        row.cells.push_back(Cell(std::to_wstring(o.openK) + L" : " + std::to_wstring(o.openD), o.openK - o.openD));
        rows.push_back(row);
    }
    SortState& st = app.sort("duels", 3, true);
    sortRows(rows, st.col, st.desc);
    float th = 34 + std::max<size_t>(rows.size(), 1) * 46 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "duels";
    o.rowH = 46;
    o.sortCol = st.col;
    o.sortDesc = st.desc;
    o.onSort = [&app](int col) {
        SortState& s = app.sort("duels", 3, true);
        if (s.col == col) s.desc = !s.desc;
        else {
            s.col = col;
            s.desc = col != 0;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 28;

    // Killfeed
    y += sectionTitle(c, x, y, w, L"Killfeed", L"Alle Kills des Matches – deine sind hervorgehoben");
    std::vector<Col> kc = {{L"RUNDE", 64, Align::Right}, {L"ZEIT", 64, Align::Right}, {L"KILLER", -2}, {L"WAFFE", -2}, {L"OPFER", -2}};
    std::vector<Row> kr;
    for (const auto& k : m.kills) {
        Row row;
        row.cells.push_back(Cell(std::to_wstring(k.round), k.round));
        row.cells.push_back(Cell(clockAt(m, k.round, k.tick), k.tick));
        Cell killer(W(k.killer.empty() ? "Welt" : m.playerName(k.killer)));
        killer.color = k.killerSide == "T" ? c.th.tSide : c.th.ctSide;
        killer.font = F::BodySemi;
        row.cells.push_back(killer);
        std::wstring wtxt = W(k.weapon);
        std::wstring flags;
        if (k.headshot) flags += L"Kopfschuss ";
        if (k.penetrated) flags += L"Wallbang ";
        if (k.noscope) flags += L"Noscope ";
        if (k.smoke) flags += L"Smoke ";
        if (k.killerBlind) flags += L"geblendet ";
        if (k.killerAir) flags += L"Sprung ";
        if (k.trade) flags += L"Trade ";
        Cell wc(wtxt);
        wc.sub = flags;
        wc.icon = k.headshot ? Icon::Crosshair : Icon::None;
        wc.iconColor = c.th.accentText;
        row.cells.push_back(wc);
        Cell victim(W(m.playerName(k.victim)));
        victim.color = k.victimSide == "T" ? c.th.tSide : c.th.ctSide;
        row.cells.push_back(victim);
        row.highlight = k.killer == p->steamId || k.victim == p->steamId;
        kr.push_back(row);
    }
    float kh = 34 + kr.size() * 44 + 12;
    c.card(RectF(x, y, w, kh));
    TableOpts ko;
    ko.key = "killfeed";
    ko.rowH = 44;
    table(c, x + 4, y + 4, w - 8, kc, kr, ko);
    y += kh + 10;
    return y - y0;
}

// ---- Utility --------------------------------------------------------------------------------------------------
float tabUtility(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    float y0 = y;
    const fd::PlayerInfo* me = m.player(app.me);
    std::vector<Col> cols = {
        {L"SPIELER", -3},
        {L"FLASHES", 80, Align::Right},
        {L"GEBLENDET", 98, Align::Right, L"Geblendete Gegner"},
        {L"Ø ZEIT", 66, Align::Right, L"Ø Blendzeit pro Gegner"},
        {L"WIRKSAM", 74, Align::Right, L"Flashes, die einen Gegner mindestens 1 s blenden"},
        {L"F-ASSISTS", 88, Align::Right, L"Flash-Assists: geblendeter Gegner wurde von einem Mitspieler getötet"},
        {L"TEAM", 56, Align::Right, L"Mitspieler geblendet"},
        {L"HE", 46, Align::Right, L"HE geworfen"},
        {L"HE-DMG", 70, Align::Right},
        {L"MOLLY", 60, Align::Right, L"Molotovs geworfen"},
        {L"FEUER", 62, Align::Right, L"Feuerschaden"},
        {L"SMOKES", 66, Align::Right},
        {L"UD/R", 58, Align::Right, L"Utility-Schaden pro Runde"},
    };
    std::vector<Row> rows;
    for (const auto& p : m.players) {
        fd::Counters cnt = fd::matchCounters(p, p.all);
        Row row;
        Cell n(W(p.name));
        n.font = F::BodySemi;
        n.sub = L"Team " + W(p.team) + (me && p.team == me->team ? L" (dein Team)" : L"");
        row.cells.push_back(n);
        auto st = [&](const char* id) {
            const fd::Stat* s = fd::findStat(id);
            double v = s->expr.eval(cnt);
            return Cell(W(fd::formatStat(v, s->def->fmt)), v);
        };
        row.cells.push_back(st("flashThrown"));
        row.cells.push_back(st("enemiesFlashed"));
        row.cells.push_back(st("blindTime"));
        row.cells.push_back(st("flashEffective"));
        row.cells.push_back(st("flashAssists"));
        row.cells.push_back(st("teamFlashed"));
        row.cells.push_back(st("heThrown"));
        row.cells.push_back(st("heDmg"));
        row.cells.push_back(st("fireThrown"));
        row.cells.push_back(st("fireDmg"));
        row.cells.push_back(st("smokeThrown"));
        row.cells.push_back(st("utilDmgPerRound"));
        row.highlight = p.steamId == app.me;
        rows.push_back(row);
    }
    SortState& s = app.sort("util", 2, true);
    sortRows(rows, s.col, s.desc);
    float th = 34 + rows.size() * 48 + 12;
    c.card(RectF(x, y, w, th));
    TableOpts o;
    o.key = "util";
    o.rowH = 48;
    o.sortCol = s.col;
    o.sortDesc = s.desc;
    o.onSort = [&app](int col) {
        SortState& st = app.sort("util", 2, true);
        if (st.col == col) st.desc = !st.desc;
        else {
            st.col = col;
            st.desc = col != 0;
        }
        app.invalidate();
    };
    table(c, x + 4, y + 4, w - 8, cols, rows, o);
    y += th + 10;
    return y - y0;
}

// ---- Momente --------------------------------------------------------------------------------------------------
float tabMoments(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    float y0 = y;
    bool recording = app.recording();
    std::wstring block = app.momentBlocker(m);
    // Schalter: nur meine / alle
    c.chipRow(x, y, {L"Meine Momente", L"Alle Spieler"}, app.allPlayers ? 1 : 0, "mom-who", [&app](int i) {
        app.allPlayers = i == 1;
        app.invalidate();
    });
    int sel = 0;
    for (const auto& h : m.highlights)
        if (app.selected.count(m.checksum + "/" + h.id)) ++sel;
    std::wstring recLabel = sel ? L"Ausgewählte aufnehmen (" + std::to_wstring(sel) + L")" : L"Ausgewählte aufnehmen";
    float bw = c.buttonWidth(recLabel, Icon::Record);
    std::vector<MomentRef> refs;
    for (const auto& h : m.highlights)
        if (app.selected.count(m.checksum + "/" + h.id)) refs.push_back({m.checksum, h.id});
    c.button(RectF(x + w - bw, y - 3, bw, 36), recLabel, "mom-rec", [&app, refs] { app.record(refs); }, Btn::Primary, Icon::Record,
             sel > 0 && block.empty(), block.empty() && sel == 0 ? L"Erst Momente links ankreuzen" : block);
    y += 48;
    if (recording) {
        c.round(RectF(x, y, w, 40), 10, alpha(c.th.accent, 30));
        c.icon(Icon::Record, RectF(x + 14, y + 12, 16, 16), c.th.danger);
        c.text(app.recordStatus(), F::BodySemi, c.th.text, RectF(x + 40, y, w - 160, 40));
        c.button(RectF(x + w - 110, y + 5, 100, 30), L"Abbrechen", "mom-cancel", [&app] { app.cancelRecording(); }, Btn::Danger);
        y += 52;
    }
    int shown = 0;
    for (const auto& h : m.highlights) {
        if (!app.allPlayers && h.player != app.me) continue;
        const fd::PlayerInfo* p = m.player(h.player);
        std::string key = m.checksum + "/" + h.id;
        bool on = app.selected.count(key) > 0;
        RectF rr(x, y, w, 66);
        uint32_t id = hashId("mom:" + key);
        c.round(rr, 12, on ? alpha(c.th.accent, c.th.dark ? 26 : 40) : c.hot(id) ? c.th.hover : c.th.card);
        c.roundStroke(rr, 12, on ? alpha(c.th.accent, 160) : c.th.border, 1);
        c.region(rr, id, nullptr, L"", false);   // zuerst: Kästchen und Knöpfe darüber bleiben anklickbar
        // Auswahlkästchen
        RectF box(rr.X + 16, rr.Y + 22, 22, 22);
        c.round(box, 6, on ? c.th.accent : c.th.btn);
        if (on) c.icon(Icon::Check, RectF(box.X + 3, box.Y + 3, 16, 16), c.th.onAccent, 2.2f);
        c.region(RectF(rr.X, rr.Y, 52, rr.Height), hashId("momsel:" + key), [&app, key] {
            if (app.selected.count(key)) app.selected.erase(key);
            else app.selected.insert(key);
            app.invalidate();
        });
        float tx = rr.X + 54;
        c.text(W(fd::highlightTitle(h)), F::BodySemi, c.th.text, RectF(tx, rr.Y + 10, rr.Width - 420, 24));
        std::wstring meta = L"Runde " + std::to_wstring(h.round) + L" · " + W(h.side) + L" · " + W(fd::fmtDec(h.seconds(m.tickrate), 0)) + L" s";
        if (!h.weapons.empty()) {
            meta += L" · ";
            for (size_t i = 0; i < h.weapons.size(); ++i) meta += (i ? L", " : L"") + W(h.weapons[i]);
        }
        if (app.allPlayers && p) meta = W(p->name) + L" · " + meta;
        c.text(meta, F::Small, c.th.text3, RectF(tx, rr.Y + 34, rr.Width - 420, 20));
        // Bewertung als Sterne (1–5)
        int stars = std::max(1, std::min(5, h.score / 40 + 1));
        for (int s = 0; s < 5; ++s)
            c.icon(Icon::Star, RectF(rr.X + rr.Width - 404 + s * 15, rr.Y + 25, 14, 14), s < stars ? c.th.accent : c.th.track);
        MomentRef ref{m.checksum, h.id};
        const Clip* clip = app.clipFor(m.checksum, h.id);
        float bx = rr.X + rr.Width - 316;
        c.button(RectF(bx, rr.Y + 15, 150, 36), L"In CS2 ansehen", "mw:" + key, [&app, ref] { app.watch(ref); }, Btn::Secondary, Icon::Eye,
                 block.empty(), block);
        if (clip) {
            std::wstring path = clip->path;
            c.button(RectF(bx + 158, rr.Y + 15, 150, 36), L"Video öffnen", "mv:" + key, [path] { openInShell(path); }, Btn::Secondary, Icon::Play);
        } else {
            c.button(RectF(bx + 158, rr.Y + 15, 150, 36), L"Aufnehmen", "mr:" + key, [&app, ref] { app.record({ref}); }, Btn::Secondary, Icon::Record,
                     block.empty(), block);
        }
        y += 74;
        ++shown;
    }
    if (shown == 0)
        y += emptyState(c, x, y, w, Icon::Film, L"Keine Momente",
                        app.allPlayers ? L"In diesem Match wurde nichts Besonderes erkannt." : L"Für dich wurde in diesem Match nichts Besonderes erkannt. "
                                                                                                  L"Schau bei „Alle Spieler“ nach.");
    c.text(L"„In CS2 ansehen“ startet CS2 mit der Demo und springt zum Moment. „Aufnehmen“ spielt die Abschnitte in CS2 ab und speichert ein Video "
           L"in deinem Highlight-Ordner. FragDesk steuert CS2 dabei nur über dessen Konsole und lädt nichts ins Spiel.",
           F::Small, c.th.text3, RectF(x, y + 6, w, 40), Align::Left, false, true);
    y += 54;
    return y - y0;
}

}  // namespace

float drawMatch(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = w - pad * 2, y = 22;
    c.button(RectF(x - 8, y, 104, 32), L"Zurück", "back", [&app] { app.go(app.backPage); }, Btn::Ghost, Icon::Back);
    y += 42;
    if (!app.detail) {
        y += emptyState(c, x, y, cw, Icon::Warning, L"Match nicht verfügbar", app.detailError.empty() ? L"Die Auswertung fehlt." : app.detailError);
        return y + 20;
    }
    const fd::Match& m = *app.detail;
    const fd::PlayerInfo* me = m.player(app.me);
    // Kopf
    c.round(RectF(x, y + 6, 6, 52), 3, rgb(fd::mapColor(m.map)));
    c.text(W(fd::mapName(m.map)), F::Display, c.th.text, RectF(x + 18, y, 400, 38));
    std::wstring meta = W(fd::modeName(m.mode)) + L" · " + dateTimeText(matchEpoch(m)) + L" · " + W(fd::fmtDuration(m.duration)) + L" · " +
                        std::to_wstring(m.rounds.size()) + L" Runden" + (m.overtimes ? L" (Verlängerung)" : L"");
    c.text(meta, F::Body, c.th.text2, RectF(x + 18, y + 38, cw - 400, 20));
    // Ergebnis rechts
    {
        std::wstring score = std::to_wstring(m.scoreA) + L" : " + std::to_wstring(m.scoreB);
        if (me) score = std::to_wstring(m.scoreOf(me->team)) + L" : " + std::to_wstring(m.scoreOf(me->team == "A" ? "B" : "A"));
        Color sc = !me ? c.th.text : me->result == "win" ? c.th.success : me->result == "loss" ? c.th.danger : c.th.text2;
        float sw = c.measure(score, F::NumBig);
        c.text(score, F::NumBig, sc, RectF(x + cw - sw - 4, y, sw + 4, 40), Align::Right);
        if (me) {
            std::wstring res = W(fd::resultName(me->result));
            float pw = c.pillWidth(res, F::SmallSemi) + 6;
            c.pill(RectF(x + cw - sw - pw - 16, y + 9, pw, 24), res, alpha(sc, 40), sc, F::SmallSemi);
        }
    }
    y += 70;
    // Aktionen
    {
        float bx = x;
        std::wstring demo = widen(m.demoPath);
        float b0 = c.buttonWidth(L"2D-Ansicht", Icon::Map);
        c.button(RectF(bx, y, b0, 34), L"2D-Ansicht", "m2d", [&app] { app.openReplay(0); }, Btn::Primary, Icon::Map, true,
                 L"Jede Runde von oben: wo alle stehen, wohin sie schauen, Smokes, Mollys, Flashes und Kills");
        bx += b0 + 8;
        float b1 = c.buttonWidth(L"Demo im Ordner zeigen", Icon::Folder);
        c.button(RectF(bx, y, b1, 34), L"Demo im Ordner zeigen", "mshow", [demo] { showInExplorer(demo); }, Btn::Secondary, Icon::Folder,
                 fileExists(demo), fileExists(demo) ? L"" : L"Die Demo-Datei ist nicht mehr da");
        bx += b1 + 8;
        std::string cs = m.checksum;
        float b2 = c.buttonWidth(L"Auswertung löschen", Icon::Trash);
        c.button(RectF(bx, y, b2, 34), L"Auswertung löschen", "mdel", [&app, cs] { app.deleteMatch(cs); }, Btn::Ghost, Icon::Trash);
        if (!m.shareCode.empty()) c.text(L"Share-Code: " + W(m.shareCode), F::Small, c.th.text3, RectF(bx + b2 + 16, y, 420, 34));
    }
    y += 50;
    y += c.tabs(x, y, cw, {L"Übersicht", L"Scoreboard", L"Runden", L"Waffen", L"Duelle", L"Utility", L"Momente"}, app.matchTab, "mtab",
                [&app](int i) {
                    app.matchTab = i;
                    app.invalidate();
                }) +
         22;
    switch (app.matchTab) {
        case 0: y += tabOverview(c, app, m, x, y, cw); break;
        case 1: y += tabScoreboard(c, app, m, x, y, cw); break;
        case 2: y += tabRounds(c, app, m, x, y, cw); break;
        case 3: y += tabWeapons(c, app, m, x, y, cw); break;
        case 4: y += tabDuels(c, app, m, x, y, cw); break;
        case 5: y += tabUtility(c, app, m, x, y, cw); break;
        default: y += tabMoments(c, app, m, x, y, cw); break;
    }
    return y + 24;
}

}  // namespace fdw
