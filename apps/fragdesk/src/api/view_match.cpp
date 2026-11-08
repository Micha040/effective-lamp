// FragDesk – Handy-App: ein Match (wie page_match.cpp). Pro Anfrage ein Bereich plus der Kopf des Matches; die
// Heatmap (Tab 7) liefert view_heat.cpp, die 2D-Ansicht view_replay.cpp. Ohne die Knöpfe, die nur am PC gehen
// (Aufnehmen, In CS2 ansehen, Neu auswerten, Ordner).
//
//   GET /api/v1/match/<Prüfsumme>?tab=<0–6>&player=<SteamID>
//
// tab: 0 Übersicht, 1 Scoreboard, 2 Runden, 3 Waffen, 4 Duelle, 5 Utility, 6 Momente (Nummern wie in FragDesk).
// player: wessen Werte Übersicht, Runden, Waffen und Duelle zeigen („Werte von …“); fehlt er oder spielte er nicht
// mit, ist es wie in FragDesk der eigene Spieler, sonst der erste.
//
// Tabellen haben eine gemeinsame Form, damit die App sie einheitlich zeichnet und wie FragDesk sortiert:
//   {"cols": [{"t": Titel, "a": "l"|"c" (sonst rechtsbündig), "tip": Erklärung}],
//    "rows": [{"c": [Zelle …], "hl": hervorgehoben, "bar": "win"|"loss", "player": SteamID (Tippen: dieser Spieler)}],
//    "sortKey", "sortCol", "sortDesc": nur bei sortierbaren Tabellen (Standard wie in FragDesk)}
// Zelle: Text oder {"t", "s": Sortierwert, "tone", "color", "b": fett, "sm": klein, "sub": zweite Zeile, "tip",
// "icon", "it": Farbe des Symbols}. Ohne "s" wird nach dem Text sortiert – wie ui::sortRows.
#include <algorithm>
#include <cmath>
#include <map>

#include "views.h"

namespace fdapi {

namespace {

// ---- Tabellen (wie ui::Col, ui::Cell und ui::Row in FragDesk) ---------------------------------------------------
struct Col {
    const char* title;
    char align = 'r';   // 'l' links, 'c' mittig, 'r' rechts
    const char* tip = "";
};

struct Cell {
    std::string text;
    double sort = NAN;        // NaN: nach dem Text sortieren
    const char* tone = "";    // Farbe: good, bad, dim, muted, t, ct, acc (sonst Textfarbe)
    std::string color;        // eigene Farbe (#rrggbb), z. B. Premier
    bool bold = false, small = false;
    std::string sub, tip;     // zweite Zeile; Erklärung beim Tippen
    const char* icon = "";    // Symbol vor dem Text (Name wie in ui.js)
    const char* iconTone = "";

    Cell() = default;
    Cell(std::string t) : text(std::move(t)) {}
    Cell(std::string t, double s) : text(std::move(t)), sort(s) {}
};

struct Row {
    std::vector<Cell> cells;
    bool highlight = false;
    const char* bar = "";   // "win" / "loss": farbiger Streifen links
    std::string player;     // Tippen: Werte dieses Spielers
};

void writeCell(JsonW& j, const Cell& c) {
    if (std::isnan(c.sort) && !*c.tone && c.color.empty() && !c.bold && !c.small && c.sub.empty() && c.tip.empty() && !*c.icon) {
        j.str(c.text);
        return;
    }
    j.obj().kv("t", c.text);
    if (!std::isnan(c.sort)) j.kn("s", c.sort);
    if (*c.tone) j.kv("tone", c.tone);
    if (!c.color.empty()) j.kv("color", c.color);
    if (c.bold) j.kb("b", true);
    if (c.small) j.kb("sm", true);
    if (!c.sub.empty()) j.kv("sub", c.sub);
    if (!c.tip.empty()) j.kv("tip", c.tip);
    if (*c.icon) j.kv("icon", c.icon).kv("it", c.iconTone);
    j.end();
}

// sortKey leer: nicht sortierbar (Runden, Killfeed)
void writeTable(JsonW& j, const std::vector<Col>& cols, const std::vector<Row>& rows, const char* sortKey = "", int sortCol = -1,
                bool sortDesc = true) {
    j.obj();
    if (*sortKey) j.kv("sortKey", sortKey).ki("sortCol", sortCol).kb("sortDesc", sortDesc);
    j.key("cols").arr();
    for (const Col& c : cols) {
        j.obj().kv("t", c.title);
        if (c.align != 'r') j.kv("a", std::string(1, c.align));
        if (*c.tip) j.kv("tip", c.tip);
        j.end();
    }
    j.end().key("rows").arr();
    for (const Row& r : rows) {
        j.obj().key("c").arr();
        for (const Cell& cell : r.cells) writeCell(j, cell);
        j.end();
        if (r.highlight) j.kb("hl", true);
        if (*r.bar) j.kv("bar", r.bar);
        if (!r.player.empty()) j.kv("player", r.player);
        j.end();
    }
    j.end().end();
}

// ---- Hilfen -------------------------------------------------------------------------------------------------------
double V(const char* id, const fd::Counters& c) { return fd::statValue(id, c); }

Cell num(double v) { return Cell(fd::fmtInt(v), v); }

Cell statCell(const char* id, const fd::Counters& c) {
    const fd::Stat* s = fd::findStat(id);
    double v = s ? s->expr.eval(c) : NAN;
    return Cell(s ? fd::formatStat(v, s->def->fmt) : std::string("–"), v);
}

std::string clockAt(const fd::Match& m, int round, int tick) {
    for (const auto& r : m.rounds)
        if (r.n == round) return fd::fmtClock((tick - r.freezeEnd) / m.tickrate);
    return std::string();
}

std::string trimEnd(std::string s) {   // FragDesk hängt an Aufzählungen ein Leerzeichen an
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

const char* sideTone(const std::string& side) { return side == "T" ? "t" : "ct"; }

// Wie focus() in FragDesk: der gewählte Spieler, sonst ich, sonst der erste
const fd::PlayerInfo* focusOf(const fd::Match& m, const std::string& wanted, const std::string& me) {
    if (!wanted.empty()) {
        if (const fd::PlayerInfo* p = m.player(wanted)) return p;
    }
    if (const fd::PlayerInfo* p = m.player(me)) return p;
    return m.players.empty() ? nullptr : &m.players[0];
}

void writeClip(JsonW& j, const std::map<std::string, std::string>& clipOf, const fd::Match& m, const fd::HighlightInfo& h) {
    auto it = clipOf.find(m.checksum + "/" + h.id);
    if (it != clipOf.end()) j.kv("clip", it->second);
    else j.key("clip").null();
}

// ---- Kopf (Map, Modus, Datum, Ergebnis) und „Werte von …“ -----------------------------------------------------------
void writeHead(JsonW& j, const fd::Match& m, const std::string& me) {
    const fd::PlayerInfo* p = m.player(me);
    std::string meta = fd::modeName(m.mode) + " · " + dateTimeText(matchEpoch(m)) + " · " + fd::fmtDuration(m.duration) + " · " +
                       std::to_string(m.rounds.size()) + " Runden" + (m.overtimes ? " (Verlängerung)" : "");
    std::string score = std::to_string(m.scoreA) + " : " + std::to_string(m.scoreB);
    const char* tone = "";
    if (p) {
        score = std::to_string(m.scoreOf(p->team)) + " : " + std::to_string(m.scoreOf(p->team == "A" ? "B" : "A"));
        tone = p->result == "win" ? "win" : p->result == "loss" ? "loss" : "draw";
    }
    j.key("head")
        .obj()
        .kv("map", fd::mapName(m.map))
        .kv("mapColor", hexColor(fd::mapColor(m.map)))
        .kv("meta", meta)
        .kv("score", score)
        .kv("result", tone)
        .kv("resultText", p ? fd::resultName(p->result) : std::string())
        .kv("share", m.shareCode.empty() ? std::string() : "Share-Code: " + m.shareCode)
        .end();
}

void writeFocus(JsonW& j, const fd::Match& m, const fd::PlayerInfo& p, const std::string& me) {
    j.key("focus").obj().kv("id", p.steamId).kv("label", "Werte von " + p.name).end();
    j.key("players").arr();
    for (const auto& pl : m.players) {
        j.obj().kv("id", pl.steamId).kv("label", pl.name + (pl.steamId == me ? "  (du)" : "") + "  – Team " + pl.team);
        if (pl.steamId == me) j.kb("me", true);
        j.end();
    }
    j.end();
}

// ---- Übersicht ----------------------------------------------------------------------------------------------------
void tabOverview(JsonW& j, const fd::Match& m, const fd::PlayerInfo& p, const std::map<std::string, std::string>& clipOf) {
    fd::Counters cnt = fd::matchCounters(p, p.all);
    struct K {
        std::string label, value, sub, tone, color;
        const char* hint;   // Kennzahl, deren Erklärung beim Tippen erscheint
    };
    std::string kda = fd::fmtInt(p.stat("kills")) + " / " + fd::fmtInt(p.stat("deaths")) + " / " +
                      fd::fmtInt(p.stat("assists") + p.stat("flashAssists"));
    std::vector<K> ks = {
        {"Rating 2.0", fd::fmtDec(p.rating, 2), "Rating 1.0: " + fd::fmtDec(p.rating1, 2), ratingTone(p.rating), "", "rating"},
        {"K / D / A", kda, "K/D " + statText("kd", cnt) + " · " + statText("kdDiff", cnt), "", "", ""},
        {"ADR", fd::fmtDec(p.adr, 1), "Utility " + statText("utilDmgPerRound", cnt) + " pro Runde", "", "", "adr"},
        {"KAST", fd::fmtPct(p.kast, 0), "Überlebt " + statText("survival", cnt), "", "", "kast"},
        {"Headshots", statText("hsPct", cnt), statText("hsKills", cnt) + " Kopfschuss-Kills", "", "", "hsPct"},
        {"Eröffnungsduelle", fd::fmtInt(p.stat("openKills")) + " : " + fd::fmtInt(p.stat("openDeaths")), "gewonnen " + statText("openSuccess", cnt), "",
         "", "openSuccess"},
        {"Clutches", statText("clutchWins", cnt) + " / " + statText("clutches", cnt), "Kills im Clutch: " + statText("clutchKills", cnt), "", "",
         "clutches"},
        {"Treffsicherheit", statText("accuracy", cnt), "Erster Schuss " + statText("firstShotAcc", cnt), "", "", "accuracy"},
        {"Multikills", statText("multiRounds", cnt), "3K " + statText("k3", cnt) + " · 4K " + statText("k4", cnt) + " · Ass " + statText("k5", cnt), "",
         "", "multiRounds"},
        {"Geblendete Gegner", statText("enemiesFlashed", cnt), "Ø " + statText("blindTime", cnt) + " · Flash-Assists " + statText("flashAssists", cnt),
         "", "", "enemiesFlashed"},
    };
    if (p.rankType == 11 && (p.rankNew > 0 || p.rankOld > 0)) {
        int v = p.rankNew > 0 ? p.rankNew : p.rankOld;
        std::string sub =
            p.rankOld > 0 && p.rankNew > 0 ? "vorher " + fd::fmtRating(p.rankOld) + " (" + fd::fmtSigned(p.rankNew - p.rankOld, 0) + ")" : "";
        ks.insert(ks.begin(), K{"Premier-Rating", fd::fmtRating(v), sub, "", hexColor(fd::premierColor(v)), ""});
    }
    j.key("overview").obj().key("kpis").arr();
    for (const K& k : ks) {
        const fd::Stat* s = *k.hint ? fd::findStat(k.hint) : nullptr;
        j.obj().kv("label", k.label).kv("value", k.value).kv("sub", k.sub).kv("hint", s && s->def->hint ? s->def->hint : "").kv("tone", k.tone);
        if (!k.color.empty()) j.kv("color", k.color);
        j.end();
    }
    j.end();

    // Rundenleiste: g = Hälfte (0, 1, dann je 3 Runden Verlängerung) – zwischen zwei Hälften kommt eine Lücke
    j.key("rounds").arr();
    for (const auto& r : m.rounds) {
        const fd::PlayerRound* pr = p.round(r.n);
        bool won = pr ? pr->won : false;
        std::string tip = "Runde " + std::to_string(r.n) + " (" + (pr ? pr->side : std::string()) + ") – " + (won ? "gewonnen" : "verloren") + "\n" +
                          fd::reasonName(r.reason);
        if (pr) {
            tip += "\n" + std::to_string(pr->k) + " Kills, " + std::to_string(pr->dmg) + " Schaden";
            if (pr->dead)
                tip += "\nGestorben bei " + clockAt(m, r.n, pr->deathTick) + (pr->killerWeapon.empty() ? std::string() : " (" + pr->killerWeapon + ")");
            if (pr->clutch) tip += "\nClutch 1v" + std::to_string(pr->clutch) + (pr->clutchWon ? " gewonnen" : " verloren");
            tip += "\n" + fd::econName(pr->econ) + ", Ausrüstung " + fd::fmtMoney(pr->equip);
        }
        int half = r.n <= 12 ? 0 : r.n <= 24 ? 1 : 2 + (r.n - 25) / 3;
        j.obj()
            .ki("n", r.n)
            .kb("won", won)
            .kb("dead", pr && pr->dead)
            .ki("k", pr ? pr->k : 0)
            .kb("clutch", pr && pr->clutchWon)
            .kv("side", pr ? pr->side : std::string())
            .ki("g", half)
            .kv("tip", tip)
            .end();
    }
    j.end();

    // Momente dieses Spielers (höchstens 4, Reihenfolge wie im Match)
    j.key("moments").arr();
    int shown = 0;
    for (const auto& h : m.highlights) {
        if (h.player != p.steamId) continue;
        if (shown++ >= 4) break;
        j.obj()
            .kv("match", m.checksum)
            .kv("id", h.id)
            .kv("title", fd::highlightTitle(h))
            .kv("meta", "Runde " + std::to_string(h.round) + " · " + fd::fmtDec(h.seconds(m.tickrate), 0) + " s")
            .kb("clutch", h.clutch > 0);
        writeClip(j, clipOf, m, h);
        j.end();
    }
    j.end().end();
}

// ---- Scoreboard ---------------------------------------------------------------------------------------------------
void tabScoreboard(JsonW& j, const fd::Match& m, const std::string& me) {
    bool premier = false;
    for (const auto& p : m.players)
        if (p.rankType == 11 && (p.rankNew > 0 || p.rankOld > 0)) premier = true;
    bool outdated = m.schema < fd::kMatchSchema;   // ohne Crosshair-Placement und Reaktionszeit
    j.key("scoreboard").obj();
    if (outdated)
        j.key("notice")
            .obj()
            .kv("title", "Mit einer älteren FragDesk-Version ausgewertet")
            .kv("text", "Diesem Match fehlen die Werte für Crosshair-Placement und Reaktionszeit (CP, REAKT.) – dafür das Match in FragDesk am PC "
                        "neu auswerten.")
            .end();
    else j.key("notice").null();
    j.key("teams").arr();
    for (int t = 0; t < 2; ++t) {
        const fd::TeamInfo& team = m.teams[t];
        int score = m.scoreOf(team.letter), other = m.scoreOf(team.letter == "A" ? "B" : "A");
        std::string name = team.name;
        if (name.empty() || name == "Team A" || name == "Team B") name = "Team " + team.letter;
        bool mine = false;
        for (const auto& p : m.players)
            if (p.team == team.letter && p.steamId == me) mine = true;
        if (mine) name += " (dein Team)";
        // Alle Spalten wie in FragDesk (am Handy blättert die Tabelle waagerecht, statt Spalten wegzulassen)
        std::vector<Col> cols = {
            {"SPIELER", 'l'},
            {"K"},
            {"D"},
            {"A"},
            {"+/−"},
            {"ADR"},
            {"KAST"},
            {"HS"},
            {"RATING"},
            {"CP", 'r', "Crosshair-Placement: Ø Winkel zwischen Fadenkreuz und Kopf des Gegners, wenn er sichtbar wird (kleiner ist besser)"},
            {"REAKT.", 'r', "Reaktionszeit: Ø Zeit vom Sichtbarwerden eines Gegners bis zum ersten Treffer"},
            {"EK", 'r', "Eröffnungskills"},
            {"ET", 'r', "Eröffnungstode"},
            {"MK", 'r', "Multikill-Runden (2+ Kills)"},
            {"CL", 'r', "Clutches gewonnen / gespielt"},
            {"GB", 'r', "Geblendete Gegner"},
            {"UD", 'r', "Utility-Schaden"},
            {"MVP"},
        };
        if (premier) cols.push_back({"PREMIER"});
        std::vector<Row> rows;
        for (const auto& p : m.players) {
            if (p.team != team.letter) continue;
            fd::Counters cnt = fd::matchCounters(p, p.all);
            Row row;
            Cell nm(p.name);
            nm.bold = true;
            if (!p.crosshair.empty()) nm.tip = "Fadenkreuz-Code: " + p.crosshair;
            row.cells.push_back(nm);
            row.cells.push_back(num(p.stat("kills")));
            row.cells.push_back(num(p.stat("deaths")));
            row.cells.push_back(num(p.stat("assists") + p.stat("flashAssists")));
            double diff = p.stat("kills") - p.stat("deaths");
            Cell d(fd::fmtSigned(diff, 0), diff);
            d.tone = diff > 0 ? "good" : diff < 0 ? "bad" : "dim";
            row.cells.push_back(d);
            row.cells.push_back(Cell(fd::fmtDec(p.adr, 1), p.adr));
            row.cells.push_back(Cell(fd::fmtPct(p.kast, 0), p.kast));
            double hs = V("hsPct", cnt);
            row.cells.push_back(Cell(fd::formatStat(hs, fd::Fmt::Pct0), hs));
            Cell rat(fd::fmtDec(p.rating, 2), p.rating);
            rat.tone = ratingTone(p.rating);
            rat.bold = true;
            row.cells.push_back(rat);
            double cp = V("crosshair", cnt), rt = V("reaction", cnt);
            Cell cpc(fd::formatStat(cp, fd::Fmt::Deg1), std::isnan(cp) ? 1e9 : cp);   // ohne Wert ans Ende
            cpc.tip = std::isnan(cp) ? (outdated ? "Erst nach dem Neu-Auswerten in FragDesk am PC" : "Keine Begegnung gemessen")
                                     : fd::fmtInt(V("sightings", cnt)) + " Begegnungen gemessen · am Kopf: " +
                                           fd::formatStat(V("crosshairGood", cnt), fd::Fmt::Pct0);
            row.cells.push_back(cpc);
            Cell rtc(fd::formatStat(rt, fd::Fmt::Ms), std::isnan(rt) ? 1e9 : rt);
            if (!std::isnan(rt)) rtc.tip = fd::fmtInt(cnt.get("ttdN")) + " erste Treffer innerhalb von 2 Sekunden";
            row.cells.push_back(rtc);
            row.cells.push_back(num(p.stat("openKills")));
            row.cells.push_back(num(p.stat("openDeaths")));
            row.cells.push_back(num(V("multiRounds", cnt)));
            double cw = V("clutchWins", cnt), ca = V("clutches", cnt);
            row.cells.push_back(Cell(fd::fmtInt(cw) + "/" + fd::fmtInt(ca), cw * 100 + ca));
            row.cells.push_back(num(p.stat("enemiesFlashed")));
            row.cells.push_back(num(p.stat("utilDmg")));
            row.cells.push_back(num(p.mvps));
            if (premier) {
                Cell pr("–");
                if (p.rankType == 11 && (p.rankNew > 0 || p.rankOld > 0)) {
                    int v = p.rankNew > 0 ? p.rankNew : p.rankOld;
                    pr.text = fd::fmtRating(v);
                    pr.color = hexColor(fd::premierColor(v));
                    pr.bold = true;
                    pr.sort = v;
                    if (p.rankOld > 0 && p.rankNew > 0 && p.rankOld != p.rankNew) pr.sub = fd::fmtSigned(p.rankNew - p.rankOld, 0);
                }
                row.cells.push_back(pr);
            }
            row.highlight = p.steamId == me;
            row.player = p.steamId;
            rows.push_back(row);
        }
        j.obj()
            .kv("score", std::to_string(score))
            .kv("tone", score > other ? "win" : score < other ? "loss" : "draw")
            .kv("name", name)
            .kv("sub", "Start als " + team.startSide + " · 1. Hälfte " + std::to_string(team.scoreFirstHalf) + " · 2. Hälfte " +
                           std::to_string(team.scoreSecondHalf));
        j.key("table");
        writeTable(j, cols, rows, "sb", 8, true);
        j.end();
    }
    j.end().end();
}

// ---- Runden -------------------------------------------------------------------------------------------------------
void tabRounds(JsonW& j, const fd::Match& m, const fd::PlayerInfo& p) {
    std::vector<Col> cols = {
        {"#"},
        {"SEITE", 'c'},
        {"ERGEBNIS", 'l'},
        {"STAND", 'c'},
        {"KAUF", 'l', "Kaufart deines Teams und dein Ausrüstungswert"},
        {"K"},
        {"SCHADEN"},
        {"TOD", 'l', "Zeitpunkt nach der Kaufphase und Waffe des Gegners"},
        {"", 'l'},
    };
    std::vector<Row> rows;
    for (const auto& r : m.rounds) {
        const fd::PlayerRound* pr = p.round(r.n);
        Row row;
        row.cells.push_back(Cell(std::to_string(r.n), r.n));
        Cell side(pr ? pr->side : "–");
        side.tone = pr && pr->side == "T" ? "t" : "ct";
        side.bold = side.small = true;
        row.cells.push_back(side);
        bool won = pr && pr->won;
        Cell res(fd::reasonName(r.reason));
        res.icon = r.reason == "bomb" ? "bomb" : r.reason == "defuse" ? "shield" : r.reason == "time" ? "clock" : "crosshair";
        res.iconTone = won ? "good" : "bad";
        res.sub = won ? "gewonnen" : "verloren";
        row.cells.push_back(res);
        int mine = p.team == "A" ? r.scoreA : r.scoreB, theirs = p.team == "A" ? r.scoreB : r.scoreA;
        row.cells.push_back(Cell(std::to_string(mine) + ":" + std::to_string(theirs)));
        Cell buy(fd::econName(p.team == "A" ? r.econA : r.econB));
        if (pr) buy.sub = fd::fmtMoney(pr->equip) + " Ausrüstung";
        row.cells.push_back(buy);
        Cell k(pr && pr->k ? std::to_string(pr->k) : "–", pr ? pr->k : 0);
        if (pr && pr->k >= 3) {
            k.bold = true;
            k.tone = "acc";
        }
        row.cells.push_back(k);
        row.cells.push_back(Cell(pr ? std::to_string(pr->dmg) : "–", pr ? pr->dmg : 0));
        Cell death("überlebt");
        death.tone = "muted";
        if (pr && pr->dead) {
            std::string killer = m.playerName(pr->killer);
            death.text = clockAt(m, r.n, pr->deathTick) + " – " + (killer.empty() ? "Welt" : killer);
            death.sub = pr->killerWeapon + (pr->traded ? " · getradet" : "");
            death.tone = "";
        }
        row.cells.push_back(death);
        std::string tags;
        if (pr) {
            if (pr->open == "K") tags += "Eröffnung ";
            if (pr->open == "D") tags += "Erster Tod ";
            if (pr->clutch) tags += "1v" + std::to_string(pr->clutch) + (pr->clutchWon ? "✓ " : " ");
            if (pr->plant) tags += "Plant ";
            if (pr->defuse) tags += "Defuse ";
        }
        Cell tg(trimEnd(tags));
        tg.small = true;
        tg.tone = "dim";
        row.cells.push_back(tg);
        row.bar = won ? "win" : "loss";
        rows.push_back(row);
    }
    j.key("rounds").obj().key("table");
    writeTable(j, cols, rows);
    j.end();
}

// ---- Waffen -------------------------------------------------------------------------------------------------------
const char* weaponType(const std::string& t) {
    return t == "rifle"         ? "Gewehr"
           : t == "sniper"      ? "Scharfschützengewehr"
           : t == "smg"         ? "Maschinenpistole"
           : t == "pistol"      ? "Pistole"
           : t == "shotgun"     ? "Schrotflinte"
           : t == "machine_gun" ? "Maschinengewehr"
           : t == "grenade"     ? "Granate"
           : t == "melee"       ? "Messer"
                                : "Ausrüstung";
}

void tabWeapons(JsonW& j, const fd::PlayerInfo& p) {
    std::vector<Col> cols = {
        {"WAFFE", 'l'},
        {"KILLS"},
        {"HS", 'r', "Headshot-Quote der Kills"},
        {"SCHADEN"},
        {"SCHÜSSE"},
        {"TREFFER", 'r', "Anteil der Schüsse, die getroffen haben"},
        {"1. SCHUSS", 'r', "Treffsicherheit des ersten Schusses"},
        {"SPRAY", 'r', "Treffsicherheit ab der 4. Kugel"},
        {"BEWEGT", 'r', "Anteil der Schüsse in Bewegung"},
        {"KOPF", 'r', "Kopftreffer unter allen Treffern"},
        {"Ø DIST.", 'r', "Durchschnittliche Kill-Distanz"},
    };
    std::vector<Row> rows;
    for (const auto& wp : p.weapons) {
        if (wp.c.get("kills") == 0 && wp.c.get("shots") == 0 && wp.c.get("dmg") == 0) continue;
        Row row;
        Cell n(wp.name);
        n.bold = true;
        n.sub = weaponType(wp.type);
        row.cells.push_back(n);
        row.cells.push_back(statCell("kills", wp.c));
        row.cells.push_back(statCell("hsPct", wp.c));
        row.cells.push_back(Cell(fd::fmtInt(wp.c.get("dmg")), wp.c.get("dmg")));
        row.cells.push_back(statCell("shots", wp.c));
        row.cells.push_back(statCell("accuracy", wp.c));
        row.cells.push_back(statCell("firstShotAcc", wp.c));
        row.cells.push_back(statCell("sprayAcc", wp.c));
        row.cells.push_back(statCell("movingPct", wp.c));
        row.cells.push_back(statCell("headHitPct", wp.c));
        row.cells.push_back(statCell("avgDist", wp.c));
        rows.push_back(row);
    }
    j.key("weapons").obj().key("table");
    writeTable(j, cols, rows, "mweapons", 1, true);
    j.kv("empty", "Keine Waffe benutzt");

    // Trefferbild (Anteile aller Treffer) und „Gestorben durch“
    const fd::Counters& cnt = p.all;
    struct Z {
        const char *id, *name, *stat;
    } zones[] = {{"head", "Kopf", "headHitPct"},       {"neck", "Hals", "hitNeckPct"}, {"chest", "Brust", "hitChestPct"},
                 {"stomach", "Bauch", "hitStomachPct"}, {"arms", "Arme", "hitArmsPct"}, {"legs", "Beine", "hitLegsPct"}};
    j.key("zones").arr();
    for (const Z& z : zones) {
        double v = V(z.stat, cnt);
        j.obj().kv("id", z.id).kv("name", z.name).kv("value", fd::fmtPct(v, 1)).kn("v", v).end();
    }
    j.end();
    double mx = p.deathsBy.empty() ? 1 : p.deathsBy[0].second;
    j.key("deaths").arr();
    for (size_t i = 0; i < p.deathsBy.size() && i < 6; ++i)
        j.obj()
            .kv("label", p.deathsBy[i].first)
            .kv("value", std::to_string(p.deathsBy[i].second) + "×")
            .kn("frac", mx > 0 ? p.deathsBy[i].second / mx : 0)
            .end();
    j.end().kv("deathsEmpty", "Kein einziger Tod").end();
}

// ---- Duelle -------------------------------------------------------------------------------------------------------
void tabDuels(JsonW& j, const fd::Match& m, const fd::PlayerInfo& p) {
    std::vector<Col> cols = {
        {"GEGNER", 'l'},
        {"KILLS"},
        {"TODE"},
        {"SALDO"},
        {"AUSGETEILT", 'r', "Schaden, den du diesem Gegner zugefügt hast"},
        {"ERHALTEN", 'r', "Schaden, den dieser Gegner dir zugefügt hat"},
        {"ERÖFFNUNG", 'r', "Eröffnungsduelle gegen diesen Gegner: gewonnen : verloren"},
    };
    std::vector<Row> rows;
    for (const auto& o : p.opponents) {
        const fd::PlayerInfo* op = m.player(o.steamId);
        if (!op || op->team == p.team) continue;
        Row row;
        Cell n(op->name);
        n.bold = true;
        row.cells.push_back(n);
        row.cells.push_back(Cell(std::to_string(o.k), o.k));
        row.cells.push_back(Cell(std::to_string(o.d), o.d));
        Cell d(fd::fmtSigned(o.k - o.d, 0), o.k - o.d);
        d.tone = o.k > o.d ? "good" : o.k < o.d ? "bad" : "dim";
        d.bold = true;
        row.cells.push_back(d);
        row.cells.push_back(Cell(std::to_string(o.dmgDealt), o.dmgDealt));
        row.cells.push_back(Cell(std::to_string(o.dmgTaken), o.dmgTaken));
        row.cells.push_back(Cell(std::to_string(o.openK) + " : " + std::to_string(o.openD), o.openK - o.openD));
        rows.push_back(row);
    }
    j.key("duels").obj().key("table");
    writeTable(j, cols, rows, "duels", 3, true);
    j.kv("empty", "Keine Duelle mit Gegnern");

    // Killfeed: alle Kills des Matches, die des gezeigten Spielers hervorgehoben
    std::vector<Col> kc = {{"RUNDE"}, {"ZEIT"}, {"KILLER", 'l'}, {"WAFFE", 'l'}, {"OPFER", 'l'}};
    std::vector<Row> kr;
    for (const auto& k : m.kills) {
        Row row;
        row.cells.push_back(Cell(std::to_string(k.round), k.round));
        row.cells.push_back(Cell(clockAt(m, k.round, k.tick), k.tick));
        Cell killer(k.killer.empty() ? "Welt" : m.playerName(k.killer));
        killer.tone = sideTone(k.killerSide);
        killer.bold = true;
        row.cells.push_back(killer);
        std::string flags;
        if (k.headshot) flags += "Kopfschuss ";
        if (k.penetrated) flags += "Wallbang ";
        if (k.noscope) flags += "Noscope ";
        if (k.smoke) flags += "Smoke ";
        if (k.killerBlind) flags += "geblendet ";
        if (k.killerAir) flags += "Sprung ";
        if (k.trade) flags += "Trade ";
        Cell w(k.weapon);
        w.sub = trimEnd(flags);
        if (k.headshot) {
            w.icon = "crosshair";
            w.iconTone = "acc";
        }
        row.cells.push_back(w);
        Cell victim(m.playerName(k.victim));
        victim.tone = sideTone(k.victimSide);
        row.cells.push_back(victim);
        row.highlight = k.killer == p.steamId || k.victim == p.steamId;
        kr.push_back(row);
    }
    j.key("kills");
    writeTable(j, kc, kr);
    j.end();
}

// ---- Utility ------------------------------------------------------------------------------------------------------
void tabUtility(JsonW& j, const fd::Match& m, const std::string& meId) {
    const fd::PlayerInfo* me = m.player(meId);
    std::vector<Col> cols = {
        {"SPIELER", 'l'},
        {"FLASHES"},
        {"GEBLENDET", 'r', "Geblendete Gegner"},
        {"Ø ZEIT", 'r', "Ø Blendzeit pro Gegner"},
        {"WIRKSAM", 'r', "Flashes, die einen Gegner mindestens 1 s blenden"},
        {"F-ASSISTS", 'r', "Flash-Assists: geblendeter Gegner wurde von einem Mitspieler getötet"},
        {"TEAM", 'r', "Mitspieler geblendet"},
        {"HE", 'r', "HE geworfen"},
        {"HE-DMG"},
        {"MOLLY", 'r', "Molotovs geworfen"},
        {"FEUER", 'r', "Feuerschaden"},
        {"SMOKES"},
        {"UD/R", 'r', "Utility-Schaden pro Runde"},
    };
    std::vector<Row> rows;
    for (const auto& p : m.players) {
        fd::Counters cnt = fd::matchCounters(p, p.all);
        Row row;
        Cell n(p.name);
        n.bold = true;
        n.sub = "Team " + p.team + (me && p.team == me->team ? " (dein Team)" : "");
        row.cells.push_back(n);
        for (const char* id : {"flashThrown", "enemiesFlashed", "blindTime", "flashEffective", "flashAssists", "teamFlashed", "heThrown", "heDmg",
                               "fireThrown", "fireDmg", "smokeThrown", "utilDmgPerRound"})
            row.cells.push_back(statCell(id, cnt));
        row.highlight = p.steamId == meId;
        rows.push_back(row);
    }
    j.key("utility").obj().key("table");
    writeTable(j, cols, rows, "util", 2, true);
    j.end();
}

// ---- Momente ------------------------------------------------------------------------------------------------------
void tabMoments(JsonW& j, ViewCtx& c, const fd::Match& m, const std::map<std::string, std::string>& clipOf) {
    j.key("moments").obj().kv("blocked", c.host.mediaBlocked ? c.host.mediaBlocked() : std::string());
    j.key("items").arr();
    for (const auto& h : m.highlights) {
        const fd::PlayerInfo* p = m.player(h.player);
        std::string meta = "Runde " + std::to_string(h.round) + " · " + h.side + " · " + fd::fmtDec(h.seconds(m.tickrate), 0) + " s";
        if (!h.weapons.empty()) {
            meta += " · ";
            for (size_t i = 0; i < h.weapons.size(); ++i) meta += (i ? ", " : "") + h.weapons[i];
        }
        j.obj()
            .kv("id", h.id)
            .kv("title", fd::highlightTitle(h))
            .kv("meta", meta)
            .kv("who", p ? p->name : std::string())   // „Alle Spieler“: Name vor meta
            .kb("mine", h.player == c.me)
            .ki("stars", std::max(1, std::min(5, h.score / 40 + 1)))
            .kb("clutch", h.clutch > 0)
            .ki("round", h.round)   // 2D-Ansicht an dieser Stelle
            .ki("tick", momentTick(h, m.tickrate));
        writeClip(j, clipOf, m, h);
        j.end();
    }
    j.end();
    j.key("emptyMine")
        .obj()
        .kv("title", "Keine Momente")
        .kv("text", "Für dich wurde in diesem Match nichts Besonderes erkannt. Schau bei „Alle Spieler“ nach.")
        .end();
    j.key("emptyAll").obj().kv("title", "Keine Momente").kv("text", "In diesem Match wurde nichts Besonderes erkannt.").end();
    j.end();
}

}  // namespace

void viewMatch(ViewCtx& c, Response& res, const std::string& checksum) {
    fd::Match m;
    if (checksum.empty() || !c.src.detail(checksum, m)) {
        res.error(404, "Das Match gibt es nicht (mehr).");
        return;
    }
    std::string t = c.req.param("tab");
    int tab = t.size() == 1 && t[0] >= '0' && t[0] <= '6' ? t[0] - '0' : 0;
    JsonW j;
    j.obj().kv("id", m.checksum).kv("title", matchTitle(m, c.me)).ki("tab", tab);
    writeHead(j, m, c.me);

    // Übersicht, Runden, Waffen und Duelle zeigen die Werte eines Spielers
    const fd::PlayerInfo* p = nullptr;
    if (tab == 0 || tab == 2 || tab == 3 || tab == 4) {
        p = focusOf(m, c.req.param("player"), c.me);
        if (p) writeFocus(j, m, *p, c.me);
        else j.key("focus").null();
    }
    std::map<std::string, std::string> clipOf;   // "Prüfsumme/Highlight" -> Video
    if (tab == 0 || tab == 6)
        for (const auto& ci : c.src.clips()) clipOf[ci.match + "/" + ci.highlight] = ci.id;

    switch (tab) {
        case 0:
            if (p) tabOverview(j, m, *p, clipOf);
            break;
        case 1: tabScoreboard(j, m, c.me); break;
        case 2:
            if (p) tabRounds(j, m, *p);
            break;
        case 3:
            if (p) tabWeapons(j, *p);
            break;
        case 4:
            if (p) tabDuels(j, m, *p);
            break;
        case 5: tabUtility(j, m, c.me); break;
        default: tabMoments(j, c, m, clipOf); break;
    }
    res.body = j.end().take();
}

}  // namespace fdapi
