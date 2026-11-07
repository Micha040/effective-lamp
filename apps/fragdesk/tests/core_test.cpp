// FragDesk – Tests für den plattformunabhängigen Teil (Linux, g++). Aufruf: sh tests/run.sh [Ordner mit fdanalyze-JSON]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/format.h"
#include "../src/core/model.h"
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
    check(!parseMatch("{\"schema\":2}", slim, err), "unbekanntes Schema abgelehnt");
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

int main(int argc, char** argv) {
    testExpr();
    testCountersMerge();
    testFormat();
    testVdf();
    testSideless();
    testModel();
    if (argc > 1) testRealFiles(argv[1]);
    std::printf("%d Prüfungen, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
