// FragDesk – Tests für den plattformunabhängigen Teil (Linux, g++). Aufruf: sh tests/run.sh [Ordner mit fdanalyze-JSON]
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/cs2con.h"
#include "../src/core/format.h"
#include "../src/core/heat.h"
#include "../src/core/model.h"
#include "../src/core/radar.h"
#include "../src/core/replay.h"
#include "../src/core/stats.h"
#include "../src/core/vdf.h"

using namespace fd;

static int failures = 0, checks = 0;

static void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FEHLER %s\n", what.c_str());
    }
}

static void near(double a, double b, double eps, const std::string& what) {
    check(std::fabs(a - b) <= eps, what + " = " + std::to_string(a) + " (erwartet " + std::to_string(b) + ")");
}

static std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testExpr() {
    Counters c;
    c.set(Keys::id("kills"), 20);
    c.set(Keys::id("deaths"), 10);
    c.set(Keys::id("rounds"), 25);
    Expr e;
    check(e.compile("kills / deaths"), "Formel kompiliert");
    near(e.eval(c), 2.0, 1e-12, "kills/deaths");
    check(e.compile("100 * (kills - deaths) / rounds + -2"), "Klammern und Vorzeichen");
    near(e.eval(c), 38.0, 1e-12, "Klammerformel");
    check(e.compile("max(0, deaths - kills) + min(3, 5)"), "max/min");
    near(e.eval(c), 3.0, 1e-12, "max/min");
    check(e.compile("kills / unbekannt") && std::isnan(e.eval(c)), "Division durch 0 ergibt NaN");
    check(!e.compile("kills +"), "unvollständige Formel abgelehnt");
    check(!e.compile("foo(1, 2)"), "unbekannte Funktion abgelehnt");
    check(!e.compile("1 2"), "Text nach dem Ende abgelehnt");

    // Alle Kennzahlen müssen kompilieren und eindeutig sein
    size_t defs = allStats().size();
    check(defs >= 150, "mindestens 150 Kennzahlen (" + std::to_string(defs) + ")");
    for (size_t i = 0; i < defs; ++i)
        for (size_t j = i + 1; j < defs; ++j)
            if (std::string(allStats()[i].def->id) == allStats()[j].def->id)
                check(false, std::string("doppelte Kennzahl ") + allStats()[i].def->id);
    for (const auto& g : statGroups())
        for (const char* id : g.stats) check(findStat(id) != nullptr, std::string("Gruppe ") + g.id + " kennt " + id);
    std::printf("  %zu Kennzahlen in %zu Gruppen\n", defs, statGroups().size());
}

static void testCountersMerge() {
    Counters a, b;
    a.set(Keys::id("kills"), 3);
    a.set(Keys::id("distMax"), 40);
    b.set(Keys::id("kills"), 2);
    b.set(Keys::id("distMax"), 25);
    a.merge(b);
    near(a.get("kills"), 5, 0, "Summe");
    near(a.get("distMax"), 40, 0, "Maximum bei *Max");
}

static void testFormat() {
    check(fmtInt(12345) == "12.345", "Tausender");
    check(fmtInt(999) == "999", "kleine Zahl");
    check(fmtDec(1.235, 2) == "1,24" || fmtDec(1.235, 2) == "1,23", "Komma");
    check(fmtDec(-0.004, 2) == "0,00", "negative Null");
    check(fmtPct(45.4, 0) == "45\xC2\xA0%", "Prozent");
    check(fmtSigned(3, 0) == "+3", "Vorzeichen plus");
    check(fmtSigned(-2.5, 1) == "\xE2\x88\x92" "2,5", "Vorzeichen minus");
    check(fmtSigned(0, 0) == "0", "Null ohne Vorzeichen");
    check(formatStat(NAN, Fmt::Dec2) == "\xE2\x80\x93", "NaN als Strich");
    check(fmtMoney(4350) == "4.350\xC2\xA0$", "Geld");
    check(fmtRating(21595) == "21.595", "Rating");
    check(fmtClock(67) == "1:07", "Uhrzeit");
    check(mapName("de_mirage") == "Mirage" && mapName("de_newmap") == "Newmap", "Map-Namen");
    check(modeName("competitive") == "Wettkampf", "Modus");
    int64_t t = 0;
    check(parseIsoUtc("2026-11-06T21:13:05Z", t) && t == 1793999585, "ISO-Datum UTC " + std::to_string(t));
    check(parseIsoUtc("2026-11-06T22:13:05+01:00", t) && t == 1793999585, "ISO-Datum mit Zeitzone");
    check(!parseIsoUtc("gestern", t), "ungültiges Datum");

    HighlightInfo h;
    h.kills = 3;
    h.tags = {"3k", "noscope", "quick"};
    check(highlightTitle(h) == "3 Kills \xC2\xB7 Noscope, blitzschnell", "Highlight-Titel: " + highlightTitle(h));
    h.kills = 2;
    h.clutch = 2;
    h.tags = {"2k", "clutch"};
    check(highlightTitle(h) == "1v2-Clutch mit 2 Kills", "Clutch-Titel: " + highlightTitle(h));
    h.kills = 1;
    h.clutch = 0;
    h.tags = {"knife"};
    check(highlightTitle(h) == "Messer-Kill", "Einzel-Kill-Titel: " + highlightTitle(h));
}

static void testVdf() {
    const char* login = R"("users"
{
	"76561198000000001"
	{
		"AccountName"		"micha"
		"PersonaName"		"Micha \"040\""
		"MostRecent"		"1"
	}
	"76561198000000002"
	{
		"AccountName"		"zweit"
		"PersonaName"		"Zweitkonto"
		"MostRecent"		"0"
	}
}
)";
    VdfNode root;
    check(parseVdf(login, root), "loginusers.vdf");
    const VdfNode* users = root.find("USERS");
    check(users && users->children.size() == 2, "zwei Konten");
    if (users && users->children.size() == 2) {
        check(users->children[0].get("personaname") == "Micha \"040\"", "Escapes und Groß-/Kleinschreibung");
        check(users->children[0].get("MostRecent") == "1", "MostRecent");
    }
    const char* libs = R"("libraryfolders"
{
	"0" { "path" "C:\\Program Files (x86)\\Steam" "apps" { "228980" "1" } }
	"1" { "path" "D:\\SteamLibrary" "apps" { "730" "4500000" } } // Kommentar
}
)";
    check(parseVdf(libs, root), "libraryfolders.vdf");
    const VdfNode* lf = root.find("libraryfolders");
    check(lf && lf->children.size() == 2 && lf->children[1].get("path") == "D:\\SteamLibrary", "Bibliothekspfad");
    check(lf && lf->children[1].find("apps") && lf->children[1].find("apps")->find("730"), "CS2 in Bibliothek 1");
    check(!parseVdf("\"a\" { \"b\" ", root), "unvollständige Datei abgelehnt");

    // cs2_video.txt für den eigenen Einstellungsordner der Aufnahmen
    const std::string video = "\"video.cfg\"\n{\n\t\"Version\"\t\t\"16\"\n\t\"setting.fullscreen\"\t\t\"1\"\n"
                              "\t\"setting.defaultres\"\t\t\"2560\"\n\t\"setting.defaultresheight\"\t\t\"1440\"\n}\n";
    std::string v = setVdfValue(video, "setting.fullscreen", "0");
    v = setVdfValue(v, "setting.defaultres", "1920");
    v = setVdfValue(v, "setting.nowindowborder", "1");   // fehlt: wird ergänzt
    check(parseVdf(v, root), "geänderte cs2_video.txt bleibt lesbar");
    const VdfNode* vc = root.find("video.cfg");
    check(vc && vc->get("setting.fullscreen") == "0" && vc->get("setting.defaultres") == "1920", "Werte ersetzt");
    check(vc && vc->get("setting.defaultresheight") == "1440" && vc->get("Version") == "16", "andere Werte unverändert");
    check(vc && vc->get("setting.nowindowborder") == "1" && vc->children.size() == 5, "fehlender Wert ergänzt");
    check(setVdfValue(v, "setting.defaultres", "1920") == v, "gleicher Wert ändert nichts");
}

static void testSideless() {
    check(findStat("winrate") && statSideless(*findStat("winrate")), "Siegquote nur fürs ganze Match");
    check(findStat("matches") && statSideless(*findStat("matches")), "Matches nur gesamt");
    check(findStat("mvpPerMatch") && statSideless(*findStat("mvpPerMatch")), "MVPs nur gesamt");
    check(findStat("kd") && !statSideless(*findStat("kd")), "K/D nach Seite");
    check(findStat("adr") && !statSideless(*findStat("adr")), "ADR nach Seite");
    check(sourceName("perfectworld") == "Perfect World" && sourceName("esl") == "ESL" && sourceName("neu") == "Neu", "Namen der Quellen");
}

static const char* kMiniMatch = R"({"schema":1,"analyzer":"1.0.0","csda":"1.13.0","checksum":"abc",
"demo":{"path":"C:\\d\\m.dem","file":"m.dem","size":1000,"modified":"2026-11-06T20:00:00Z"},
"match":{"source":"valve","mode":"premier","map":"de_mirage","date":"2026-11-06T20:00:00Z","duration":1800,"tickrate":64,
 "tickCount":100000,"serverName":"x","maxRounds":24,"overtimes":0,"scoreA":13,"scoreB":11,"winner":"A"},
"teams":[{"letter":"A","name":"Team A","score":13,"scoreFirstHalf":7,"scoreSecondHalf":6,"startSide":"CT"},
         {"letter":"B","name":"Team B","score":11,"scoreFirstHalf":5,"scoreSecondHalf":6,"startSide":"T"}],
"rounds":[{"n":1,"start":0,"freezeEnd":1000,"end":5000,"endOfficial":5400,"winner":"A","winnerSide":"CT","reason":"elimination",
 "scoreA":1,"scoreB":0,"sideA":"CT","econA":"pistol","econB":"pistol","equipA":4000,"equipB":4000,"spentA":0,"spentB":0,
 "moneyA":4000,"moneyB":4000,"durationMs":70000,"firstKill":0}],
"players":[{"steamId":"76561198000000001","name":"Micha","team":"A","slot":3,"result":"win","rankType":11,"rankOld":15000,
 "rankNew":15120,"wins":40,"mvps":3,"score":50,"rating":1.25,"rating1":1.2,"kast":75,"adr":90,
 "stats":{"kills":20,"deaths":15,"rounds":24,"dmg":2160,"hsKills":10,"kastRounds":18,"k1":8,"k2":3,"k3":2,"distMax":42.5},
 "statsT":{"kills":8,"rounds":12},"statsCT":{"kills":12,"rounds":12},
 "weapons":[{"name":"AK-47","type":"rifle","stats":{"kills":12,"shots":200,"shotsHit":50}}],
 "deathsBy":{"AWP":5,"AK-47":7},
 "rounds":[{"n":1,"side":"CT","won":true,"k":2,"dmg":200,"kast":true,"open":"K","equip":800,"spent":700,"money":800,"econ":"pistol","alive":60}],
 "opponents":[{"steamId":"76561198000000009","k":3,"d":1,"dmgDealt":300,"dmgTaken":100,"openK":1,"openD":0}]},
 {"steamId":"76561198000000009","name":"Gegner","team":"B","slot":7,"result":"loss","rankType":11,"rankOld":0,"rankNew":0,
 "stats":{"kills":15,"deaths":20,"rounds":24},"statsT":{},"statsCT":{},"weapons":[],"deathsBy":{},"rounds":[],"opponents":[]}],
"kills":[{"t":2000,"r":1,"k":"76561198000000001","v":"76561198000000009","ks":"CT","vs":"T","w":"AK-47","wt":"rifle","hs":true,"dist":12.5}],
"clutches":[],
"highlights":[{"id":"x-r1","p":"76561198000000001","r":1,"side":"CT","score":45,"kills":3,"tags":["3k"],"weapons":["AK-47"],
 "segments":[[1500,2200],[3000,3300]],"killTicks":[2000,2100,3200]}]})";

static void testModel() {
    Match m;
    std::string err;
    check(parseMatch(kMiniMatch, m, err), "Mini-Match lesen: " + err);
    check(m.map == "de_mirage" && m.scoreA == 13 && m.rounds.size() == 1 && m.players.size() == 2, "Match-Felder");
    const PlayerInfo* me = m.player("76561198000000001");
    check(me && me->rankNew == 15120 && me->weapons.size() == 1 && me->deathsBy.size() == 2, "Spieler-Felder");
    if (me) {
        check(me->deathsBy[0].first == "AK-47", "Tode nach Waffe sortiert");
        check(me->rounds.size() == 1 && me->rounds[0].open == "K", "Spieler-Runde");
        check(me->opponents.size() == 1 && me->opponents[0].k == 3, "Gegner");
        near(me->all.get("kills"), 20, 0, "Zähler gelesen");
    }
    check(m.kills.size() == 1 && m.kills[0].headshot, "Kills");
    check(m.highlights.size() == 1 && m.highlights[0].segments.size() == 2, "Highlights");
    near(m.highlights[0].seconds(64), 1000.0 / 64, 1e-9, "Highlight-Länge");

    Match slim;
    check(parseMatch(kMiniMatch, slim, err, false) && slim.kills.empty() && slim.players[0].rounds.empty() && !slim.highlights.empty(),
          "Bibliotheksmodus ohne Details");
    check(!parseMatch("{\"schema\":3}", slim, err), "unbekanntes Schema abgelehnt");
    std::string v2 = kMiniMatch;
    v2.replace(v2.find("\"schema\":1"), 10, "\"schema\":2");
    check(parseMatch(v2, slim, err) && slim.schema == 2, "Schema 2 gelesen");
    check(!parseMatch("kaputt", slim, err), "kaputtes JSON abgelehnt");

    // Karriere über zwei Kopien
    Match m2 = m;
    m2.date = "2026-11-07T20:00:00Z";
    m2.map = "de_nuke";
    m2.mode = "faceit";
    for (auto& p : m2.players)
        if (p.steamId == "76561198000000001") p.result = "loss", p.rankType = -1;
    std::vector<const Match*> all = {&m2, &m};
    Career c = buildCareer(all, "76561198000000001", Filter());
    check(c.matches == 2 && c.wins == 1 && c.losses == 1, "Karriere: Ergebnis");
    near(c.all.get("kills"), 40, 0, "Karriere: Kills");
    near(statValue("kd", c.all), 40.0 / 30.0, 1e-9, "Karriere: K/D");
    near(statValue("winrate", c.all), 50, 1e-9, "Karriere: Siegquote");
    near(statValue("adr", c.all), 90, 1e-9, "Karriere: ADR");
    near(c.all.get("distMax"), 42.5, 0, "Karriere: Maximum");
    check(c.points.size() == 2 && c.points[0].match == &m, "Zeitreihe chronologisch");
    check(c.premierCurrent == 15120 && c.premierBest == 15120, "Premier-Rating");
    check(c.maps.size() == 2 && c.maps["de_nuke"].losses == 1, "Maps");
    check(c.weapons["AK-47"].second.get("kills") == 24, "Waffen summiert");
    Filter f;
    f.mode = "faceit";
    check(buildCareer(all, "76561198000000001", f).matches == 1, "Filter Modus");
    f = Filter();
    f.lastN = 1;
    Career last = buildCareer(all, "76561198000000001", f);
    check(last.matches == 1 && last.points[0].match == &m2, "Filter letzte N");
    check(buildCareer(all, "123", Filter()).matches == 0, "fremder Spieler");
}

// Echte Ausgaben von fdanalyze (optional): jede Datei lesen, Plausibilität prüfen
static void testRealFiles(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    std::vector<Match> matches;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json") continue;
        Match m;
        std::string err;
        if (!parseMatch(readAll(std::string(dir) + "/" + name), m, err)) {
            check(false, name + ": " + err);
            continue;
        }
        check(!m.players.empty() && !m.rounds.empty(), name + ": Spieler und Runden");
        for (const auto& p : m.players) {
            double r = statValue("rating", matchCounters(p, p.all));
            check(std::fabs(r - p.rating) < 0.011, name + "/" + p.name + ": Rating-Formel " + std::to_string(r) + " vs " + std::to_string(p.rating));
            near(statValue("adr", p.all), p.adr, 0.011, name + "/" + p.name + ": ADR");
            near(statValue("kast", p.all), p.kast, 0.011, name + "/" + p.name + ": KAST");
        }
        std::printf("  %s: %s %d:%d, %zu Spieler, %zu Kills, %zu Highlights\n", name.c_str(), mapName(m.map).c_str(), m.scoreA,
                    m.scoreB, m.players.size(), m.kills.size(), m.highlights.size());
        matches.push_back(std::move(m));
    }
    closedir(d);
    if (matches.empty()) return;
    std::vector<const Match*> ptrs;
    for (const auto& m : matches) ptrs.push_back(&m);
    const PlayerInfo& p = matches[0].players[0];
    Career c = buildCareer(ptrs, p.steamId, Filter());
    std::printf("  Karriere %s: %d Matches, Rating %s, K/D %s, ADR %s, HS %s, Treffsicherheit %s, Erster Schuss %s\n", p.name.c_str(),
                c.matches, formatStat(statValue("rating", c.all), Fmt::Dec2).c_str(), formatStat(statValue("kd", c.all), Fmt::Dec2).c_str(),
                formatStat(statValue("adr", c.all), Fmt::Dec1).c_str(), formatStat(statValue("hsPct", c.all), Fmt::Pct0).c_str(),
                formatStat(statValue("accuracy", c.all), Fmt::Pct1).c_str(), formatStat(statValue("firstShotAcc", c.all), Fmt::Pct1).c_str());
    int nan = 0;
    for (const auto& s : allStats())
        if (std::isnan(s.expr.eval(c.all))) ++nan;
    std::printf("  %d von %zu Kennzahlen ohne Wert (fehlende Ereignisse)\n", nan, allStats().size());
    for (const auto& h : matches[0].highlights) {
        if (&h - &matches[0].highlights[0] >= 5) break;
        std::printf("  Moment R%d %s: %s (%.0f s)\n", h.round, matches[0].playerName(h.player).c_str(), highlightTitle(h).c_str(),
                    h.seconds(matches[0].tickrate));
    }
}

// ---- 2D-Ansicht ---------------------------------------------------------------------------------------------
namespace {
struct Bytes {
    std::string b;
    void u8(int v) { b.push_back(char(uint8_t(v))); }
    void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8(int((v >> (8 * i)) & 0xFF)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
    void str(const std::string& s) { u16(int(s.size())); b += s; }
    void vec(int x, int y, int z) { u16(x & 0xFFFF); u16(y & 0xFFFF); u16(z & 0xFFFF); }
};

// gleiche Daten wie TestReplayEncode in analyzer/replay_test.go
std::string sampleReplay() {
    Bytes f;   // Frames
    f.u32(100); f.u8(0); f.u8(2);
    f.u8(0); f.vec(-100, 200, 30); f.u8(64); f.u8(100); f.u8(1); f.u8(RpCT | RpKit);
    f.u8(1); f.vec(500, -500, 0); f.u8(192); f.u8(35); f.u8(1); f.u8(RpBomb);
    f.u32(108); f.u8(1); f.vec(1, 2, 3); f.u8(1);
    f.u8(0); f.vec(-90, 210, 30); f.u8(70); f.u8(100); f.u8(1); f.u8(RpCT);
    Bytes b;
    b.b = "FDRP"; b.u16(1); b.u16(0); b.f32(128); b.u16(8); b.str("de_mirage");
    b.u16(2); b.u64(76561198000000001ull); b.str("Testspieler"); b.u64(0); b.str("Bot Ich");
    b.u16(2); b.str(""); b.str("AK-47");
    b.u32(2); b.u32(uint32_t(f.b.size())); b.b += f.b;
    b.u32(1); b.u8(GrSmoke); b.u8(0); b.u8(1); b.u16(2); b.u32(100); b.vec(1, 1, 1); b.u32(108); b.vec(50, 60, 5);
    b.u32(1); b.u8(FxFire); b.u8(1); b.u8(0); b.u32(120); b.u32(1400); b.vec(10, 20, 0); b.u8(3);
    b.u16(0); b.u16(0); b.u16(10); b.u16(0); b.u16(0); b.u16(10);
    b.u32(1); b.u32(104); b.u8(0); b.u8(1); b.u8(255); b.u8(1); b.u8(KfHeadshot | KfSmoke); b.vec(-100, 200, 30); b.vec(500, -500, 0);
    b.u32(1); b.u32(103); b.u8(0); b.u8(1);
    b.u32(1); b.u32(200); b.u8(BombPlanted); b.u8(1); b.vec(7, 8, 9);
    b.b += "END!";
    return b.b;
}
}  // namespace

static void testReplay() {
    Replay r;
    std::string err;
    std::string bytes = sampleReplay();
    check(parseReplay(bytes, r, err), "Replay lesen: " + err);
    check(r.tickrate == 128 && r.interval == 8 && r.map == "de_mirage", "Replay-Kopf");
    check(r.players.size() == 2 && r.players[0].steamId == "76561198000000001" && r.players[1].steamId.empty() && r.players[1].name == "Bot Ich",
          "Replay-Spieler");
    check(r.frames.size() == 2 && r.samples.size() == 3 && r.frames[1].bomb == 1 && r.frames[1].bombPos.y == 2, "Replay-Frames");
    check(r.tracks.size() == 1 && r.tracks[0].points.size() == 2 && r.tracks[0].ct, "Replay-Flugbahn");
    check(r.effects.size() == 1 && r.effects[0].hull.size() == 3 && r.effects[0].end == 1400, "Replay-Wirkung");
    check(r.kills.size() == 1 && r.kills[0].flags == (KfHeadshot | KfSmoke) && r.kills[0].victimPos.x == 500 && r.kills[0].assister == kNoPlayer,
          "Replay-Kill");
    check(r.shots.size() == 1 && r.bomb.size() == 1 && r.bomb[0].pos.z == 9, "Replay-Schüsse/Bombe");
    check(replayWeapon(r, 1) == "AK-47" && replayWeapon(r, 9).empty(), "Replay-Waffen");
    check(frameAt(r, 50) == 0 && frameAt(r, 100) == 0 && frameAt(r, 107.9) == 0 && frameAt(r, 108) == 1 && frameAt(r, 1e9) == 1, "frameAt");

    std::vector<PlayerAt> ps;
    playersAt(r, 104, ps);   // halb zwischen den Frames
    check(ps.size() == 2, "playersAt: zwei Spieler");
    if (ps.size() == 2) {
        near(ps[0].x, -95, 0.01, "playersAt: x verschoben");
        near(ps[0].y, 205, 0.01, "playersAt: y verschoben");
        near(ps[0].yaw, (64 + 3) * 360.0 / 256, 0.01, "playersAt: Blickrichtung");
        check(ps[1].x == 500 && ps[1].hp == 35, "playersAt: Spieler ohne zweiten Frame bleibt stehen");
    }
    // Blickrichtung über 0°: von 250 (≈351°) nach 6 (≈8°) geht es kurz nach rechts, nicht einmal herum
    Replay w = r;
    w.samples[0].yaw = 250;
    w.samples[2].yaw = 6;
    playersAt(w, 104, ps);
    check(!ps.empty() && (ps[0].yaw > 355 || ps[0].yaw < 5), "playersAt: Blickrichtung über 0° (" + std::to_string(ps.empty() ? 0 : ps[0].yaw) + ")");

    // kaputte Dateien
    Replay bad;
    check(!parseReplay("FDRP", bad, err), "Replay zu kurz");
    check(!parseReplay(bytes.substr(0, bytes.size() - 6), bad, err), "Replay abgeschnitten");
    std::string v2 = bytes;
    v2[4] = 2;
    check(!parseReplay(v2, bad, err) && err.find("Version") != std::string::npos, "Replay andere Version");
    std::string garbage = bytes;
    garbage[60] = char(0xFF);   // Länge der Frames zerstört
    parseReplay(garbage, bad, err);   // darf nicht abstürzen
    check(true, "Replay mit Müll");
}

static void testRadar() {
    int idx = -1;
    const RadarMap* m = radarMap("de_mirage", &idx);
    check(m && idx >= 0 && !m->lower, "Radar Mirage");
    if (m) {
        float u, v;
        radarUV(*m, m->posX, m->posY, u, v);
        check(u == 0 && v == 0, "Radar: linke obere Ecke");
        radarUV(*m, m->posX + m->span / 2, m->posY - m->span, u, v);
        near(u, 0.5, 1e-6, "Radar: Mitte");
        near(v, 1.0, 1e-6, "Radar: unten");
    }
    check(radarMap("workshop/123456/DE_NUKE") && radarMap("de_nuke")->lower && radarLower(*radarMap("de_nuke"), -600) &&
              !radarLower(*radarMap("de_nuke"), 0),
          "Radar Nuke mit unterer Ebene");
    check(radarMap("de_unbekannt") == nullptr, "Radar unbekannte Karte");
    check(radarMapCount() >= 20, "Radar: Karten vorhanden");
}

static void testRealReplays(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 8 || name.substr(name.size() - 7) != ".replay") continue;
        Replay r;
        std::string err;
        bool ok = parseReplay(readAll(std::string(dir) + "/" + name), r, err);
        check(ok, name + ": " + err);
        if (!ok) continue;
        check(r.frames.size() > 1000 && !r.kills.empty() && radarMap(r.map), name + ": Inhalt und Karte");
        std::vector<PlayerAt> ps;
        size_t most = 0;
        for (size_t i = 0; i < r.frames.size(); i += 97) {
            playersAt(r, r.frames[i].tick + r.interval / 2.0, ps);
            most = std::max(most, ps.size());
            for (const auto& p : ps) check(p.player < r.players.size() && p.yaw >= 0 && p.yaw < 360, name + ": Spieler im Frame");
        }
        check(most >= 8, name + ": genug Spieler gleichzeitig");
        std::printf("  %s: %s, %zu Frames, %zu Kills, %zu Granaten\n", name.c_str(), r.map.c_str(), r.frames.size(), r.kills.size(), r.tracks.size());
    }
    closedir(d);
}

// ---- Heatmap ------------------------------------------------------------------------------------------------
// gleiche Daten wie TestHeatEncode in analyzer/extras_test.go (plus Abtastungen in zwei Runden)
static std::string sampleHeat() {
    Bytes f;   // Abtastungen
    f.u32(1500); f.u8(2);
    f.u8(0); f.u8(HsCT); f.vec(-1000, 1000, 0);    // Ich (CT, Team A in Runde 1)
    f.u8(1); f.u8(0); f.vec(0, 0, -600);            // Bot, T, untere Ebene
    f.u32(3000); f.u8(1); f.u8(0); f.u8(HsCT); f.vec(-1000, 1000, 0);
    f.u32(500); f.u8(1); f.u8(0); f.u8(HsCT); f.vec(-1000, 1000, 0);    // Kaufzeit: zählt nicht
    Bytes b;
    b.b = "FDHM"; b.u16(1); b.u16(0); b.f32(64); b.str("de_nuke");
    b.u16(2); b.u64(76561198000000001ull); b.str("Micha"); b.u64(0); b.str("Bot Ich");
    b.u16(2); b.str(""); b.str("AK-47");
    b.u32(2);
    b.u32(2000); b.u8(0); b.u8(1); b.u8(HkKillerCT | HkHeadshot); b.u8(1); b.vec(-1000, 1000, 0); b.vec(0, 0, -600);
    b.u32(9000); b.u8(0); b.u8(1); b.u8(HkKillerCT); b.u8(1); b.vec(0, 0, 0); b.vec(0, 0, 0);       // nach dem Match
    b.u32(3);
    b.u32(1200); b.u32(1300); b.u8(GrSmoke); b.u8(0); b.u8(HnCT); b.vec(-1000, 1000, 0); b.vec(0, 0, 0);
    b.u32(1200); b.u32(1310); b.u8(GrDecoy); b.u8(0); b.u8(HnCT); b.vec(-1000, 1000, 0); b.vec(0, 0, 0);    // Decoy: nicht angezeigt
    b.u32(1250); b.u32(1260); b.u8(GrIncendiary); b.u8(1); b.u8(HnNoThrow); b.vec(5, 5, 5); b.vec(100, 100, 0);
    b.u32(3); b.u32(uint32_t(f.b.size())); b.b += f.b;
    b.b += "END!";
    return b.b;
}

static void testHeat() {
    Heat h;
    std::string err;
    std::string bytes = sampleHeat();
    check(parseHeat(bytes, h, err), "Heatmap lesen: " + err);
    check(h.map == "de_nuke" && h.players.size() == 2 && h.players[0].steamId == "76561198000000001" && h.players[1].steamId.empty() &&
              h.weapons.size() == 2 && h.kills.size() == 2 && h.nades.size() == 3 && h.frames.size() == 3 && h.samples.size() == 4,
          "Heatmap: Inhalt");
    check(h.kills[0].flags == (HkKillerCT | HkHeadshot) && h.kills[0].victimPos.z == -600 && h.nades[2].flags == HnNoThrow,
          "Heatmap: Felder");
    for (size_t cut : {size_t(3), size_t(20), bytes.size() - 5, bytes.size() - 1}) {
        Heat x;
        check(!parseHeat(bytes.substr(0, cut), x, err), "Heatmap abgeschnitten bei " + std::to_string(cut));
    }
    std::string wrong = bytes;
    wrong[4] = 9;
    check(!parseHeat(wrong, h, err) && err.find("Version") != std::string::npos, "Heatmap: andere Version");
    parseHeat(bytes, h, err);

    Match m;
    check(parseMatch(kMiniMatch, m, err), "Mini-Match");
    m.map = "de_nuke";
    RoundInfo r2 = m.rounds[0];
    r2.n = 2;
    r2.start = 5400;
    r2.freezeEnd = 6000;
    r2.end = 8000;
    r2.endOfficial = 8400;
    r2.sideA = "T";
    m.rounds.push_back(r2);
    const RadarMap* map = radarMap("de_nuke");
    check(map && map->lower, "Nuke hat zwei Ebenen");
    if (!map) return;
    HeatSet set;
    HeatMatchInfo info = heatMatchInfo(m, "76561198000000001", "");
    check(info.myTeam == "A" && info.rounds.size() == 2, "Heatmap: mein Team");
    addMatchHeat(set, info, h, *map, "76561198000000001");
    check(set.hasMe && set.matches.size() == 1, "Heatmap: Match");
    check(set.points.size() == 3, "Heatmap: nur Aufenthalt in der Runde (" + std::to_string(set.points.size()) + ")");
    int me = 0, enemy = 0, lower = 0;
    for (const auto& p : set.points) {
        if (p.who == WhoMe) ++me;
        if (p.who == WhoEnemy) ++enemy;
        if (p.lower) ++lower;
    }
    check(me == 2 && enemy == 1 && lower == 1, "Heatmap: Ich/Gegner/Ebene");
    int kills = 0, deaths = 0, smokes = 0, fires = 0;
    for (const auto& mk : set.marks) {
        if (mk.layer == HlKills) {
            ++kills;
            check(mk.who == WhoMe && mk.side == SideCT && mk.round == 1 && mk.tip.find("Micha tötet Bot Ich (AK-47, Kopfschuss)") != std::string::npos,
                  "Heatmap: Kill " + mk.tip);
        }
        if (mk.layer == HlDeaths) {
            ++deaths;
            check(mk.who == WhoEnemy && mk.side == SideT && mk.lower, "Heatmap: Tod unten");
        }
        if (mk.layer == HlSmoke) {
            ++smokes;
            check(mk.thrown && (mk.fu != mk.u || mk.fv != mk.v) && mk.tip.find("Smoke von Micha") != std::string::npos, "Heatmap: Smoke mit Wurf");
        }
        if (mk.layer == HlMolotov) {
            ++fires;
            check(!mk.thrown && mk.fu == mk.u && mk.who == WhoEnemy, "Heatmap: Brandgranate ohne Wurf");
        }
    }
    check(kills == 1 && deaths == 1 && smokes == 1 && fires == 1 && set.marks.size() == 4, "Heatmap: Kills, Tode, Granaten");

    // Dichte: ein Punkt ergibt einen weichen Fleck mit 1 in der Mitte
    std::vector<float> g;
    HeatFilter all;
    int n = heatDensity(set, HlPresence, all, false, 64, 2, g);
    check(n == 2 && g.size() == 64 * 64, "Dichte: zwei Punkte oben");
    float mx = *std::max_element(g.begin(), g.end());
    near(mx, 1.0, 1e-6, "Dichte: Höchstwert 1");
    int lit = 0;
    for (float v : g)
        if (v > 0.01f) ++lit;
    check(lit > 20 && lit < 400, "Dichte: weicher Fleck (" + std::to_string(lit) + " Zellen)");
    HeatFilter onlyEnemy;
    onlyEnemy.who = WhoEnemy;
    check(heatDensity(set, HlPresence, onlyEnemy, false, 64, 2, g) == 0, "Dichte: Filter Gegner oben leer");
    check(heatDensity(set, HlPresence, onlyEnemy, true, 64, 2, g) == 1, "Dichte: Gegner unten");
    HeatFilter t;
    t.sides = SideT;
    check(heatDensity(set, HlKills, t, false, 64, 2, g) == 0 && heatDensity(set, HlKills, all, false, 64, 2, g) == 1, "Dichte: Seite");
}

static void testRealHeat(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 6 || name.substr(name.size() - 5) != ".heat") continue;
        std::string base = std::string(dir) + "/" + name.substr(0, name.size() - 5);
        Heat h;
        std::string err;
        bool ok = parseHeat(readAll(base + ".heat"), h, err);
        check(ok, name + ": " + err);
        Match m;
        if (!ok || !parseMatch(readAll(base + ".json"), m, err)) continue;
        const RadarMap* map = radarMap(h.map);
        check(map != nullptr, name + ": Radar");
        if (!map) continue;
        HeatSet set;
        const std::string me = m.players.empty() ? std::string() : m.players[0].steamId;
        addMatchHeat(set, heatMatchInfo(m, me, "x"), h, *map, me);
        int layers[HlCount] = {};
        for (const auto& mk : set.marks) ++layers[mk.layer];
        check(set.points.size() > h.samples.size() / 2, name + ": Aufenthalt in Runden");
        check(layers[HlKills] >= int(m.kills.size()) * 8 / 10 && layers[HlDeaths] >= int(m.kills.size()) * 9 / 10, name + ": Kills und Tode");
        check(layers[HlSmoke] + layers[HlFlash] + layers[HlMolotov] + layers[HlHE] > 30, name + ": Granaten");
        int inside = 0;
        for (const auto& p : set.points)
            if (p.u > 1000 && p.u < 64535 && p.v > 1000 && p.v < 64535) ++inside;
        check(inside > int(set.points.size()) * 98 / 100, name + ": Positionen liegen auf dem Radar");
        std::vector<float> g;
        int n = heatDensity(set, HlPresence, HeatFilter(), false, 256, 2.5f, g);
        check(n > 1000, name + ": Dichte");
        std::printf("  %s: %zu Aufenthalte, %d Kills, %d Tode, %d Smokes, %d Mollys, %d Flashes, %d HEs\n", name.c_str(), set.points.size(),
                    layers[HlKills], layers[HlDeaths], layers[HlSmoke], layers[HlMolotov], layers[HlFlash], layers[HlHE]);
    }
    closedir(d);
}

static void testCrosshairStats() {
    Counters c;
    c.add(Keys::id("cpN"), 4);
    c.add(Keys::id("cpSum"), 18);
    c.add(Keys::id("cpGood"), 3);
    c.add(Keys::id("ttdN"), 2);
    c.add(Keys::id("ttdSum"), 900);
    near(statValue("crosshair", c), 4.5, 1e-9, "Crosshair-Formel");
    near(statValue("crosshairGood", c), 75, 1e-9, "Fadenkreuz am Kopf");
    near(statValue("reaction", c), 450, 1e-9, "Reaktionszeit-Formel");
    check(std::isnan(statValue("crosshairRifle", c)), "ohne Messung: kein Wert");
    check(formatStat(4.25, Fmt::Deg1) == "4,3\xC2\xB0" || formatStat(4.25, Fmt::Deg1) == "4,2\xC2\xB0", "Grad: " + formatStat(4.25, Fmt::Deg1));
    check(formatStat(452.4, Fmt::Ms) == "452\xC2\xA0ms", "ms: " + formatStat(452.4, Fmt::Ms));
    check(findGroup("crosshair") != nullptr, "Gruppe Crosshair");
    for (const char* id : findGroup("crosshair")->stats) check(findStat(id) != nullptr, std::string("Kennzahl ") + id);
}

// Aufnahme: Zustand von CS2 aus der Konsolenausgabe, Mauszeiger aus dem Bild
static void testCs2Con() {
    const std::string log =
        "Loaded map de_mirage\r\n"
        "ChangeGameUIState: CSGO_GAME_UI_STATE_MAINMENU -> CSGO_GAME_UI_STATE_LOADINGSCREEN\r\n"
        "] demo_pause\n"
        "ChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_STATE_INGAME\n";
    check(lastUiState(log) == "INGAME", "Zustand: letzter Wechsel zählt (" + lastUiState(log) + ")");
    size_t mark = log.size();
    check(lastUiState(log, mark).empty(), "seit der Marke kein Wechsel");
    std::string more = log + "] demo_gototick 5000\nChangeGameUIState: CSGO_GAME_UI_STATE_INGAME -> CSGO_GAME_UI_STATE_LOADINGSCREEN\n";
    check(lastUiState(more, mark) == "LOADINGSCREEN", "Sprung: Ladebildschirm erkannt");
    check(lastUiState(more + "ChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_", mark) == "LOADINGSCREEN",
          "halbe Zeile zählt noch nicht");
    check(lastUiState(more + "ChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_STATE_INGAME\r\n", mark) == "INGAME",
          "Ladebildschirm vorbei");
    check(lastUiState("Damage -> 27 hp\nfoo -> bar UI_STATE_\n").empty(), "Pfeile ohne Zustand zählen nicht");
    check(lastUiState(log, log.size() + 10).empty(), "Marke hinter dem Ende");

    // Mauszeiger: ein Bildschirm 1920x1080, CS2-Fenster 1280x720 bei (100, 100)
    std::vector<IRect> one = {{0, 0, 1920, 1080}};
    IRect win = {100, 100, 1380, 820};
    int x = 0, y = 0;
    check(!cursorParkPoint(win, 1500, 500, 64, one, x, y), "Zeiger neben dem Fenster bleibt");
    // Spitze knapp links über dem Fenster, das Bild des Zeigers ragt hinein: ein Stück nach links
    bool moved = cursorParkPoint(win, 40, 90, 64, one, x, y);
    check(moved && x == 36 && y == 90, "Zeiger ragt ins Fenster: links daneben (" + std::to_string(x) + "," + std::to_string(y) + ")");
    check(!(x < win.right && x + 64 > win.left && y < win.bottom && y + 64 > win.top), "verschoben: Zeiger samt Bild außerhalb");
    check(cursorParkPoint(win, 1300, 500, 64, one, x, y) && x == 1380 && y == 500, "nächster Platz: rechts daneben");
    check(cursorParkPoint(win, 700, 800, 64, one, x, y) && x == 700 && y == 820, "nächster Platz: darunter");
    // Fenster füllt den einzigen Bildschirm: in die Ecke
    IRect full = {0, 0, 1920, 1080};
    check(cursorParkPoint(full, 900, 500, 64, one, x, y) && x == 1919 && y == 1079, "kein Platz: Ecke rechts unten");
    // zweiter Bildschirm rechts, etwas tiefer: dorthin
    std::vector<IRect> two = {{0, 0, 1920, 1080}, {1920, 200, 3840, 1280}};
    check(cursorParkPoint(full, 1900, 500, 64, two, x, y) && x >= 1920 && two[1].contains(x, y), "zweiter Bildschirm");
    check(cursorParkPoint(full, 1900, 100, 64, two, x, y) && two[1].contains(x, y), "zweiter Bildschirm (Zeile außerhalb)");
}

int main(int argc, char** argv) {
    testExpr();
    testCountersMerge();
    testFormat();
    testVdf();
    testSideless();
    testModel();
    testReplay();
    testRadar();
    testHeat();
    testCrosshairStats();
    testCs2Con();
    if (argc > 1) {
        testRealFiles(argv[1]);
        testRealReplays(argv[1]);
        testRealHeat(argv[1]);
    }
    std::printf("%d Prüfungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
