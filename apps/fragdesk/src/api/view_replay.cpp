// FragDesk – Handy-App: 2D-Ansicht einer Runde (wie page_replay.cpp). Anders als die übrigen Ansichten läuft hier nur
// das Nötigste im Thread der Daten (FragDesk: UI-Thread): Runden des Matches (beim ersten Mal aus der Auswertung, dann
// zwischengespeichert) und der Pfad der .replay-Datei. Lesen und Auswerten der Datei (1–3 MB) passiert im Thread der
// Anfrage, immer nur eine Datei zur Zeit; im Speicher bleibt nur die zuletzt gelesene.
//
//   GET /api/v1/replay/<Prüfsumme>?round=<n>[&retry=1]
//
// Antworten: 200 mit den Daten einer Runde (unten), 202 {"pending": Meldung} solange FragDesk die Datei erstellt (die
// App fragt nach "retry" Sekunden wieder), 200 {"missing": Meldung} wenn es keine gibt, 404 ohne Match.
// Meldung: {"title", "text", "retry", "button" (Beschriftung eines Knopfs, der mit retry=1 noch einmal fragt, oder leer)}.
//
// Daten einer Runde – kompakt, Zahlen als ganze Spieleinheiten bzw. Ticks; "Δ" = Abstand zum Wert davor:
//   "id", "title" (Mirage · 13:11), "map" (Mirage), "radar" (writeRadar, null ohne Radarbild), "box" [posX, posY, span]
//   (nur ohne Radarbild: Ausschnitt um alle Positionen, Umrechnung wie beim Radar), "fit" [Mitte u, Mitte v, Seite]
//   (Ausschnitt fürs Handy: wo sich im Match jemand bewegt, in 0..1 des Bildes), "lower": Karte mit zwei Ebenen
//   (dann tragen die Spuren "z"), "tickrate", "interval" (Ticks zwischen zwei Abtastungen),
//   "rounds": [{"n", "win": "ct"|"t", "g": Gruppe (neue nach jedem Seitenwechsel), "tip"}],
//   "round": {"n", "start", "freeze" (Ende der Kaufzeit), "end", "tick" (Startpunkt), "title", "result", "score",
//             "prev", "next" (Rundennummer, 0 = keine)},
//   "players": [{"i", "name", "ct", "me", "x": nicht in der Spielerliste (nur in Kills o. Ä.)}] – CT zuerst,
//   "weapons": Namen (Index 0 = keine),
//   "frames": [t0, Δt, …] – Zeitpunkte der Abtastungen,
//   "tracks": [{"p", "f": erster Frame, "x": [x0, Δx …], "y": […], "z": […] (nur mit "lower"), "a": [Blick0, Δ …]
//             (256 = 360°, Δ zwischen −128 und 127), "hp": [Frame, Wert, …], "w": [Frame, Waffe, …], "fl": [Frame,
//             Flags, …]}] – ein lebender Spieler über aufeinanderfolgende Frames; Frame relativ zu "f", nur Änderungen,
//   "bomb": [Frame, Zustand (0 getragen, 1 liegt, 2 gelegt), x, y, z, …] – nur Änderungen,
//   "nades": [{"k", "p", "ct", "pts": [t, x, y, z, Δt, Δx, Δy, Δz, …]}],
//   "effects": [{"k", "p", "ct", "s", "e" (Ende, schon geschätzt, wenn unbekannt), "x", "y", "z", "hull": [x, y, …]}],
//   "kills": [{"t", "k", "v", "w" (Waffe), "hs", "fl", "kp": [x, y, z], "vp": […], "kct", "vct", "time", "text"}],
//   "shots": [t, Spieler, Δt, Spieler, …],
//   "bombev": [{"t", "k" (1 gelegt, 2 entschärft, 3 explodiert), "x", "y", "z", "time", "text"}].
// Konstanten (Arten, Flags) wie in src/core/replay.h.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>

#include "../core/radar.h"
#include "../core/replay.h"
#include "views.h"

namespace fdapi {

namespace {

constexpr int64_t kMaxReplayBytes = 64 << 20;   // weit mehr als ein Match (1–3 MB)

// ---- Zwischenspeicher: die Runden des zuletzt gefragten Matches und die zuletzt gelesene Datei ---------------------
struct MatchInfo {
    std::string checksum, me, title, map, demo, myTeam;   // myTeam: "A"/"B", leer = nicht mitgespielt
    int gen = 0;
    double tickrate = 64;
    std::vector<fd::RoundInfo> rounds;
};

std::mutex gInfoMu;
std::shared_ptr<const MatchInfo> gInfo;

// eine gelesene Datei samt dem Bereich, in dem sich überhaupt jemand bewegt (für den Ausschnitt der Karte)
struct Loaded {
    fd::Replay r;
    int16_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool any = false;
};

struct ReplayCache {
    std::string path;
    int64_t size = -1, used = 0;
    int gen = 0;
    std::shared_ptr<const Loaded> data;
};

std::mutex gParseMu;   // eine Datei zur Zeit lesen und auswerten (schützt auch gReplay)
ReplayCache gReplay;

int64_t monoSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<const MatchInfo> summarize(const fd::Match& m, int gen, const std::string& me) {
    auto i = std::make_shared<MatchInfo>();
    i->checksum = m.checksum;
    i->me = me;
    i->gen = gen;
    i->title = matchTitle(m, me);
    i->map = fd::mapName(m.map);
    i->demo = m.demoPath;
    i->tickrate = m.tickrate > 0 ? m.tickrate : 64;
    i->rounds = m.rounds;
    if (const fd::PlayerInfo* p = m.player(me)) i->myTeam = p->team;
    return i;
}

// Datei lesen und auswerten – oder die zuletzt gelesene nehmen. broken: Datei da, aber unbrauchbar
std::shared_ptr<const Loaded> loadReplay(Host& host, const std::string& path, int gen, bool& broken) {
    broken = false;
    std::lock_guard<std::mutex> lk(gParseMu);
    int64_t size = -1;
    FileReader rd = host.openFile ? host.openFile(path, size) : nullptr;
    if (!rd) return nullptr;
    if (gReplay.data && gReplay.path == path && gReplay.size == size && gReplay.gen == gen) {
        gReplay.used = monoSeconds();
        return gReplay.data;
    }
    gReplay = ReplayCache();   // die alte zuerst freigeben: nie zwei im Speicher, nur weil eine neue kommt
    if (size <= 0 || size > kMaxReplayBytes) {
        broken = size > 0;
        return nullptr;
    }
    std::string bytes(static_cast<size_t>(size), '\0');
    for (int64_t got = 0; got < size;) {
        int64_t n = rd(got, &bytes[static_cast<size_t>(got)], size - got);
        if (n <= 0) return nullptr;
        got += n;
    }
    auto r = std::make_shared<Loaded>();
    std::string err;
    if (!fd::parseReplay(bytes, r->r, err)) {
        broken = true;
        return nullptr;
    }
    bytes.clear();
    bytes.shrink_to_fit();
    for (const auto& sm : r->r.samples) {
        if (!r->any) {
            r->x0 = r->x1 = sm.pos.x;
            r->y0 = r->y1 = sm.pos.y;
            r->any = true;
        }
        r->x0 = std::min(r->x0, sm.pos.x);
        r->x1 = std::max(r->x1, sm.pos.x);
        r->y0 = std::min(r->y0, sm.pos.y);
        r->y1 = std::max(r->y1, sm.pos.y);
    }
    gReplay.path = path;
    gReplay.size = size;
    gReplay.gen = gen;
    gReplay.used = monoSeconds();
    gReplay.data = r;
    return r;
}

// ---- Texte ------------------------------------------------------------------------------------------------------
// Waffen heißen in der Datei wie in demoinfocs (englisch); die meisten heißen in CS2 auf Deutsch genauso
std::string weaponText(const std::string& w) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"Knife", "Messer"},           {"HE Grenade", "HE-Granate"},     {"Smoke Grenade", "Smoke"},  {"Flashbang", "Flash"},
        {"Incendiary Grenade", "Brandgranate"}, {"Decoy Grenade", "Decoy"}, {"Zeus x27", "Zeus"},    {"World", "Welt"},
        {"Kevlar Vest", "Weste"},      {"Kevlar + Helmet", "Weste + Helm"}, {"Defuse Kit", "Entschärfer"}, {"UNKNOWN", ""},
    };
    for (const auto& n : kNames)
        if (w == n.first) return n.second;
    return w;
}

std::string clockText(double secs) {   // wie in page_replay.cpp: angefangene Sekunden zählen nicht
    long long s = static_cast<long long>(std::floor(std::max(0.0, secs)));
    char b[24];
    snprintf(b, sizeof b, "%lld:%02lld", s / 60, s % 60);
    return b;
}

const std::string& nameOf(const fd::Replay& r, uint8_t i) {
    static const std::string unknown = "?";
    return i < r.players.size() ? r.players[i].name : unknown;
}

// ---- Runde: Ausschnitt und Spieler (wie ReplayView::setRound) ----------------------------------------------------
struct Slice {
    const fd::RoundInfo* round = nullptr;
    int start = 0, freeze = 0, end = 0, tick = 0;
    size_t f0 = 0, f1 = 0;   // Frames [f0, f1)
    struct Member {
        uint8_t player = 0;
        bool ct = false;
    };
    std::vector<Member> roster;
    int8_t side[256];   // 1 CT, 0 T, −1 unbekannt (Spieler der Runde, sonst aus den Kills geschlossen)
};

bool isCT(const Slice& s, uint8_t player, bool fallback) { return s.side[player] < 0 ? fallback : s.side[player] == 1; }

Slice sliceRound(const MatchInfo& mi, const fd::Replay& r, int n) {
    Slice s;
    const auto& rounds = mi.rounds;
    for (const auto& ri : rounds)
        if (ri.n == n) s.round = &ri;
    if (!s.round && !rounds.empty()) s.round = &rounds.front();
    if (!s.round) return s;
    const fd::RoundInfo& ri = *s.round;
    double tr = r.tickrate;
    s.start = ri.start;
    s.freeze = std::max(ri.freezeEnd, ri.start);
    // Ende: kurz vor dem Neustart – ab dem offiziellen Rundenende stehen alle schon am Spawn der nächsten Runde
    s.end = ri.endOfficial > ri.end ? ri.endOfficial - 1 : ri.end + int(4 * tr);
    const fd::RoundInfo* next = s.round + 1 < rounds.data() + rounds.size() ? s.round + 1 : nullptr;
    if (next && next->start > ri.end) s.end = std::min(s.end, next->start - 1);
    if (s.end <= s.start) s.end = s.start + int(tr);
    s.tick = int(std::max(double(s.start), s.freeze - 3 * tr));
    // Frames der Runde: davor und danach stehen alle schon (bzw. noch) am Spawn einer anderen Runde
    auto from = std::lower_bound(r.frames.begin(), r.frames.end(), s.start, [](const fd::ReplayFrame& f, int t) { return f.tick < t; });
    auto to = std::upper_bound(r.frames.begin(), r.frames.end(), s.end, [](int t, const fd::ReplayFrame& f) { return t < f.tick; });
    s.f0 = size_t(from - r.frames.begin());
    s.f1 = std::max(s.f0, size_t(to - r.frames.begin()));
    // Spieler dieser Runde (aus den Positionen), CT zuerst, dann nach Namen
    std::fill(std::begin(s.side), std::end(s.side), int8_t(-1));
    for (size_t i = s.f0; i < r.frames.size() && r.frames[i].tick <= s.end; ++i) {
        const auto& f = r.frames[i];
        for (int k = 0; k < f.count; ++k) {
            const auto& sm = r.samples[f.first + k];
            if (s.side[sm.player] >= 0) continue;
            s.side[sm.player] = (sm.flags & fd::RpCT) ? 1 : 0;
            s.roster.push_back({sm.player, (sm.flags & fd::RpCT) != 0});
        }
    }
    std::sort(s.roster.begin(), s.roster.end(), [&](const Slice::Member& a, const Slice::Member& b) {
        if (a.ct != b.ct) return a.ct;
        return nameOf(r, a.player) < nameOf(r, b.player);
    });
    // wer nicht auf der Karte war (z. B. vor dem ersten Abtasten gestorben): Seite aus den Kills mit Spielern der Runde
    for (const auto& k : r.kills) {
        if (k.tick < s.start || k.tick > s.end || k.killer == fd::kNoPlayer || k.killer == k.victim) continue;
        if (s.side[k.killer] < 0 && s.side[k.victim] >= 0) s.side[k.killer] = int8_t(1 - s.side[k.victim]);
        if (s.side[k.victim] < 0 && s.side[k.killer] >= 0) s.side[k.victim] = int8_t(1 - s.side[k.killer]);
    }
    return s;
}

// ---- JSON -------------------------------------------------------------------------------------------------------
void vec(JsonW& j, const fd::Vec3s& p) { j.arr().integer(p.x).integer(p.y).integer(p.z).end(); }

void writeRounds(JsonW& j, const MatchInfo& mi) {
    j.key("rounds").arr();
    int g = 0;
    for (size_t i = 0; i < mi.rounds.size(); ++i) {
        const fd::RoundInfo& r = mi.rounds[i];
        if (i > 0 && !r.sideA.empty() && r.sideA != mi.rounds[i - 1].sideA) ++g;   // Halbzeit, Verlängerung (auch Wingman)
        bool ct = r.winnerSide == "CT";
        j.obj()
            .ki("n", r.n)
            .kv("win", r.winnerSide.empty() ? "" : ct ? "ct" : "t")
            .ki("g", g)
            .kv("tip", "Runde " + std::to_string(r.n) + ": " + (ct ? "CT" : "T") + " gewinnt – " + fd::reasonName(r.reason))
            .end();
    }
    j.end();
}

void writeRoundHead(JsonW& j, const MatchInfo& mi, const Slice& s) {
    const fd::RoundInfo& r = *s.round;
    int idx = int(s.round - mi.rounds.data()), count = int(mi.rounds.size());
    std::string score;
    if (mi.myTeam == "A" || mi.myTeam == "B") {
        int mine = mi.myTeam == "A" ? r.scoreA : r.scoreB, theirs = mi.myTeam == "A" ? r.scoreB : r.scoreA;
        score = "Stand\u00a0" + std::to_string(mine) + "\u00a0:\u00a0" + std::to_string(theirs);
    } else score = "Stand\u00a0" + std::to_string(r.scoreA) + "\u00a0:\u00a0" + std::to_string(r.scoreB);
    std::string result = r.winnerSide.empty() ? fd::reasonName(r.reason)
                                              : std::string(r.winnerSide == "CT" ? "CT" : "T") + " gewinnt – " + fd::reasonName(r.reason);
    j.key("round")
        .obj()
        .ki("n", r.n)
        .ki("start", s.start)
        .ki("freeze", s.freeze)
        .ki("end", s.end)
        .ki("tick", s.tick)
        .kv("title", "Runde " + std::to_string(r.n) + " von " + std::to_string(count))
        .kv("result", result)
        .kv("score", score)
        .ki("prev", idx > 0 ? mi.rounds[size_t(idx - 1)].n : 0)
        .ki("next", idx + 1 < count ? mi.rounds[size_t(idx + 1)].n : 0)
        .end();
}

// Spuren der lebenden Spieler: je Spieler und zusammenhängender Folge von Frames eine Spur
void writeTracks(JsonW& j, const fd::Replay& r, const Slice& s, bool withZ) {
    struct Track {
        uint8_t player;
        size_t from;
        std::vector<const fd::ReplaySample*> samples;
    };
    std::vector<Track> open, done;
    for (size_t fi = s.f0; fi < s.f1; ++fi) {
        const fd::ReplayFrame& f = r.frames[fi];
        std::vector<bool> seen(open.size(), false);
        for (int k = 0; k < f.count; ++k) {
            const fd::ReplaySample* sm = &r.samples[f.first + k];
            bool found = false;
            for (size_t t = 0; t < open.size(); ++t)
                if (open[t].player == sm->player && open[t].from + open[t].samples.size() == fi - s.f0) {
                    open[t].samples.push_back(sm);
                    seen[t] = true;
                    found = true;
                    break;
                }
            if (!found) {
                open.push_back({sm->player, fi - s.f0, {sm}});
                seen.push_back(true);
            }
        }
        for (size_t t = open.size(); t-- > 0;)   // nicht mehr dabei: fertig
            if (!seen[t]) {
                done.push_back(std::move(open[t]));
                open.erase(open.begin() + std::ptrdiff_t(t));
            }
    }
    for (auto& t : open) done.push_back(std::move(t));
    std::stable_sort(done.begin(), done.end(), [](const Track& a, const Track& b) { return a.player != b.player ? a.player < b.player : a.from < b.from; });

    j.key("tracks").arr();
    for (const Track& t : done) {
        j.obj().ki("p", t.player).ki("f", int64_t(t.from));
        auto deltas = [&](const char* key, auto get) {
            j.key(key).arr();
            int prev = 0;
            for (size_t i = 0; i < t.samples.size(); ++i) {
                int v = get(*t.samples[i]);
                j.integer(i ? v - prev : v);
                prev = v;
            }
            j.end();
        };
        deltas("x", [](const fd::ReplaySample& sm) { return int(sm.pos.x); });
        deltas("y", [](const fd::ReplaySample& sm) { return int(sm.pos.y); });
        if (withZ) deltas("z", [](const fd::ReplaySample& sm) { return int(sm.pos.z); });
        j.key("a").arr();
        for (size_t i = 0; i < t.samples.size(); ++i) {
            int v = t.samples[i]->yaw;
            if (i) {
                int d = (v - t.samples[i - 1]->yaw + 256) % 256;
                v = d >= 128 ? d - 256 : d;
            }
            j.integer(v);
        }
        j.end();
        auto changes = [&](const char* key, auto get) {
            j.key(key).arr();
            for (size_t i = 0; i < t.samples.size(); ++i) {
                int v = get(*t.samples[i]);
                if (i == 0 || v != get(*t.samples[i - 1])) j.integer(int64_t(i)).integer(v);
            }
            j.end();
        };
        changes("hp", [](const fd::ReplaySample& sm) { return int(sm.hp); });
        changes("w", [](const fd::ReplaySample& sm) { return int(sm.weapon); });
        changes("fl", [](const fd::ReplaySample& sm) { return int(sm.flags); });
        j.end();
    }
    j.end();
}

void writeRound(JsonW& j, const MatchInfo& mi, const Loaded& L, const Slice& s, const std::string& version) {
    const fd::Replay& r = L.r;
    const fd::RadarMap* radar = fd::radarMap(r.map);
    j.obj().kv("id", mi.checksum).kv("title", mi.title).kv("map", mi.map);
    j.key("radar");
    writeRadar(j, r.map, version);
    bool lower = radar && radar->lower;
    if (!radar) {   // ohne Radarbild: Ausschnitt um alle Positionen (wie ReplayView::prepareMap)
        double span = L.any ? double(std::max(L.x1 - L.x0, L.y1 - L.y0)) + 600 : 4096;
        double cx = L.any ? (L.x0 + L.x1) / 2.0 : 0, cy = L.any ? (L.y0 + L.y1) / 2.0 : 0;
        j.key("box").arr().num(std::round(cx - span / 2)).num(std::round(cy + span / 2)).num(std::round(span)).end();
        j.key("fit").arr().num(0.5).num(0.5).num(1).end();
    } else {
        // Ausschnitt fürs Handy: Radarbilder haben breite leere Ränder – nur der Bereich, in dem sich im ganzen Match
        // jemand bewegt hat (plus Rand), quadratisch; als Mitte und Seite im Bild (0..1)
        double u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (L.any) {
            u0 = (L.x0 - 400 - radar->posX) / radar->span;
            u1 = (L.x1 + 400 - radar->posX) / radar->span;
            v0 = (radar->posY - L.y1 - 400) / radar->span;
            v1 = (radar->posY - L.y0 + 400) / radar->span;
        }
        double side = std::clamp(std::max(u1 - u0, v1 - v0), 0.3, 1.0);
        double cu = std::clamp((u0 + u1) / 2, side / 2, 1 - side / 2), cv = std::clamp((v0 + v1) / 2, side / 2, 1 - side / 2);
        j.key("fit").arr().num(std::round(cu * 1e4) / 1e4).num(std::round(cv * 1e4) / 1e4).num(std::round(side * 1e4) / 1e4).end();
    }
    j.kb("lower", lower).kn("tickrate", r.tickrate).ki("interval", r.interval);
    writeRounds(j, mi);
    writeRoundHead(j, mi, s);

    // Spieler: die der Runde, dazu alle, die sonst in Kills vorkommen
    std::vector<bool> listed(256, false), extra(256, false);
    for (const auto& m : s.roster) listed[m.player] = true;
    for (const auto& k : r.kills) {
        if (k.tick < s.start || k.tick > s.end) continue;
        for (uint8_t p : {k.killer, k.victim})
            if (p != fd::kNoPlayer && p < r.players.size() && !listed[p]) extra[p] = true;
    }
    j.key("players").arr();
    auto writePlayer = [&](uint8_t p, bool ct, bool x) {
        if (p >= r.players.size()) return;
        const fd::ReplayPlayer& rp = r.players[p];
        j.obj().ki("i", p).kv("name", rp.name).kb("ct", ct);
        if (!mi.me.empty() && rp.steamId == mi.me) j.kb("me", true);
        if (x) j.kb("x", true);
        j.end();
    };
    for (const auto& m : s.roster) writePlayer(m.player, m.ct, false);
    for (int p = 0; p < 256; ++p)
        if (extra[p]) writePlayer(uint8_t(p), isCT(s, uint8_t(p), false), true);
    j.end();

    j.key("weapons").arr();
    for (const auto& w : r.weapons) j.str(weaponText(w));
    j.end();

    // Zeitpunkte der Abtastungen
    j.key("frames").arr();
    for (size_t fi = s.f0; fi < s.f1; ++fi) j.integer(fi == s.f0 ? r.frames[fi].tick : r.frames[fi].tick - r.frames[fi - 1].tick);
    j.end();
    writeTracks(j, r, s, lower);

    // Bombe (liegt, gelegt): nur Änderungen
    j.key("bomb").arr();
    for (size_t fi = s.f0; fi < s.f1; ++fi) {
        const fd::ReplayFrame& f = r.frames[fi];
        bool changed = fi == s.f0;
        if (!changed) {
            const fd::ReplayFrame& p = r.frames[fi - 1];
            changed = f.bomb != p.bomb || f.bombPos.x != p.bombPos.x || f.bombPos.y != p.bombPos.y || f.bombPos.z != p.bombPos.z;
        }
        if (changed) j.integer(int64_t(fi - s.f0)).integer(f.bomb).integer(f.bombPos.x).integer(f.bombPos.y).integer(f.bombPos.z);
    }
    j.end();

    // Granaten im Flug (die vor dieser Runde enden, sind beim Neustart verschwunden)
    j.key("nades").arr();
    for (const auto& g : r.tracks) {
        if (g.points.empty() || g.points.front().first > s.end || g.points.back().first < s.start) continue;
        j.obj().ki("k", g.kind).ki("p", g.thrower).kb("ct", g.ct).key("pts").arr();
        int pt = 0, px = 0, py = 0, pz = 0;
        for (size_t i = 0; i < g.points.size(); ++i) {
            const auto& p = g.points[i];
            if (i) j.integer(p.first - pt).integer(p.second.x - px).integer(p.second.y - py).integer(p.second.z - pz);
            else j.integer(p.first).integer(p.second.x).integer(p.second.y).integer(p.second.z);
            pt = p.first;
            px = p.second.x;
            py = p.second.y;
            pz = p.second.z;
        }
        j.end().end();
    }
    j.end();

    // Wirkung: Dauer wie in page_replay.cpp (unbekanntes Ende geschätzt, Flash und HE ein Augenblick)
    j.key("effects").arr();
    for (const auto& e : r.effects) {
        if (e.start < s.start || e.start > s.end) continue;
        double dur = e.end > e.start ? (e.end - e.start) / r.tickrate
                     : e.kind == fd::FxSmoke ? 20
                     : e.kind == fd::FxFire  ? 7
                     : e.kind == fd::FxDecoy ? 15
                                             : 0.6;
        if (e.kind == fd::FxFlash || e.kind == fd::FxHE) dur = 0.6;
        j.obj()
            .ki("k", e.kind)
            .ki("p", e.thrower)
            .kb("ct", e.ct)
            .ki("s", e.start)
            .ki("e", e.start + int64_t(std::lround(dur * r.tickrate)))
            .ki("x", e.pos.x)
            .ki("y", e.pos.y)
            .ki("z", e.pos.z);
        if (e.hull.size() >= 3) {
            j.key("hull").arr();
            for (const auto& h : e.hull) j.integer(h.first).integer(h.second);
            j.end();
        }
        j.end();
    }
    j.end();

    // Kills mit fertigem Text für Killliste und Zeitleiste
    j.key("kills").arr();
    for (const auto& k : r.kills) {
        if (k.tick < s.start || k.tick > s.end) continue;
        bool world = k.killer == fd::kNoPlayer || k.killer == k.victim;
        bool vct = isCT(s, k.victim, true), kct = world ? !vct : isCT(s, k.killer, !vct);
        std::string wep = weaponText(fd::replayWeapon(r, k.weapon)), hs = (k.flags & fd::KfHeadshot) ? "Headshot" : "";
        std::string text = world ? nameOf(r, k.victim) + " stirbt" : nameOf(r, k.killer) + " tötet " + nameOf(r, k.victim);
        if (!wep.empty() || !hs.empty()) text += " (" + wep + (wep.empty() || hs.empty() ? "" : ", ") + hs + ")";
        j.obj().ki("t", k.tick).ki("k", world ? fd::kNoPlayer : k.killer).ki("v", k.victim).kv("w", wep);
        if (!hs.empty()) j.kb("hs", true);
        if (k.flags) j.ki("fl", k.flags);
        j.key("kp");
        vec(j, k.killerPos);
        j.key("vp");
        vec(j, k.victimPos);
        j.kb("kct", kct).kb("vct", vct).kv("time", k.tick < s.freeze ? "–" : clockText((k.tick - s.freeze) / r.tickrate)).kv("text", text).end();
    }
    j.end();

    // Schüsse (für den Mündungsblitz)
    j.key("shots").arr();
    int last = 0;
    bool first = true;
    for (const auto& sh : r.shots) {
        if (sh.tick < s.start || sh.tick > s.end) continue;
        j.integer(first ? sh.tick : sh.tick - last).integer(sh.player);
        last = sh.tick;
        first = false;
    }
    j.end();

    j.key("bombev").arr();
    for (const auto& b : r.bomb) {
        if (b.tick < s.start || b.tick > s.end) continue;
        std::string text = b.kind == fd::BombPlanted ? "Bombe gelegt" : b.kind == fd::BombDefused ? "Bombe entschärft" : "Bombe explodiert";
        if (b.player != fd::kNoPlayer && b.kind != fd::BombExploded) text += " von " + nameOf(r, b.player);
        j.obj()
            .ki("t", b.tick)
            .ki("k", b.kind)
            .ki("x", b.pos.x)
            .ki("y", b.pos.y)
            .ki("z", b.pos.z)
            .kv("time", b.tick < s.freeze ? "–" : clockText((b.tick - s.freeze) / r.tickrate))
            .kv("text", text)
            .end();
    }
    j.end();
    j.end();
}

// Meldung statt Daten: 202 {"pending": …} (die App fragt nach retry Sekunden wieder) bzw. 200 {"missing": …};
// button: Beschriftung des Knopfs, der mit retry=1 noch einmal fragt (leer = keiner)
void message(Response& res, bool pending, const std::string& title, const std::string& text, int retry, const char* button = "") {
    JsonW j;
    j.obj().key(pending ? "pending" : "missing").obj().kv("title", title).kv("text", text).ki("retry", retry).kv("button", button).end().end();
    res.status = pending ? 202 : 200;
    res.body = j.take();
}

const char* kImpossible = "2D-Ansicht nicht möglich";

// Ohne (brauchbare) Datei: FragDesk erstellt sie (prepareReplay); der eigene Server sagt, woran es liegt – mit der
// Markierung, die FragDesk hochlädt, wenn es keine 2D-Daten geben kann
void noReplay(Host& host, Response& res, const MatchInfo& mi, const std::string& marker, bool broken, bool retry) {
    if (!host.prepareReplay) {
        std::string reason;
        if (!marker.empty() && readAll(host, marker, reason, 4096)) reason = reason.find("\"failed\"") != std::string::npos ? "failed" : "demo";
        if (broken) return message(res, false, kImpossible, "Die 2D-Daten dieses Matches auf dem Server sind beschädigt.", 0);
        if (reason == "demo")
            return message(res, false, kImpossible, "Die Demo dieses Matches ist auf dem PC nicht mehr da – ohne sie lassen sich keine 2D-Daten erstellen.",
                           0);
        if (reason == "failed") return message(res, false, kImpossible, "FragDesk konnte aus der Demo dieses Matches keine 2D-Daten erstellen.", 0);
        return message(res, false, "2D-Ansicht noch nicht da",
                       "Die 2D-Daten dieses Matches lädt FragDesk am PC noch hoch – sobald FragDesk dort offen ist und CS2 nicht läuft. "
                       "Für ältere Matches erstellt FragDesk sie dafür aus der Demo (Einstellungen › Handy-App › „2D-Ansicht hochladen“ "
                       "muss an sein).",
                       0, "Nochmal nachsehen");
    }
    ReplayState st = host.prepareReplay(mi.checksum, mi.demo, broken, retry);
    switch (st.kind) {
        case ReplayState::Working:
            return message(res, true, "2D-Daten werden erstellt …",
                           "FragDesk liest die Demo einmal komplett ein (Positionen, Granaten, Kills). Das dauert ein paar Sekunden und "
                           "passiert pro Match nur einmal.",
                           2);
        case ReplayState::Queued:
            return message(res, true, "2D-Daten werden erstellt …", "Gleich – FragDesk erstellt gerade noch die 2D-Daten eines anderen Matches.", 3);
        case ReplayState::WaitCs2:
            return message(res, true, "Wird erstellt, sobald CS2 zu ist",
                           "Solange CS2 läuft, liest FragDesk keine Demos – damit dein Spiel nicht ruckelt. Lass diese Seite offen, dann geht "
                           "es danach von selbst weiter.",
                           15);
        case ReplayState::NoDemo:
            return message(res, false, kImpossible, "Die Demo-Datei ist nicht mehr da – ohne sie lassen sich keine 2D-Daten erstellen.", 0,
                           "Erneut versuchen");
        case ReplayState::Failed: break;
    }
    return message(res, false, kImpossible, st.error.empty() ? std::string("Unbekannter Fehler") : st.error, 0, "Erneut versuchen");
}

}  // namespace

void viewReplay(Host& host, const Request& req, Response& res, const std::string& checksum) {
    if (checksum.empty() || checksum.size() > 64 || checksum.find_first_of("/\\.:") != std::string::npos)
        return res.error(404, "Das Match gibt es nicht (mehr).");
    // im Thread der Daten nur das Nötigste: Pfade und (beim ersten Mal) die Runden
    std::string path, marker;
    std::shared_ptr<const MatchInfo> info;
    int gen = 0;
    bool ok = host.withSource([&](Source& src) {
        gen = src.generation();
        std::string me = src.me();
        path = src.matchFile(checksum, "replay");
        marker = path.empty() ? src.matchFile(checksum, "noreplay") : std::string();
        {
            std::lock_guard<std::mutex> lk(gInfoMu);
            if (gInfo && gInfo->checksum == checksum && gInfo->gen == gen && gInfo->me == me) info = gInfo;
        }
        if (info) return;
        fd::Match m;
        if (!src.detail(checksum, m)) return;
        info = summarize(m, gen, me);
        std::lock_guard<std::mutex> lk(gInfoMu);
        gInfo = info;
    });
    if (!ok) return res.error(503, "FragDesk ist gerade nicht bereit.");
    if (!info) return res.error(404, "Das Match gibt es nicht (mehr).");
    if (info->rounds.empty()) return res.error(404, "Dieses Match hat keine Runden.");
    bool retry = req.param("retry") == "1";
    if (path.empty()) return noReplay(host, res, *info, marker, false, retry);

    // im Thread der Anfrage: Datei lesen (oder die zuletzt gelesene nehmen) und die Runde herausschneiden
    bool broken = false;
    std::shared_ptr<const Loaded> r = loadReplay(host, path, gen, broken);
    if (!r) return noReplay(host, res, *info, std::string(), broken, retry);
    int n = 0;
    std::string rp = req.param("round");
    if (!rp.empty() && rp.size() <= 4 && rp.find_first_not_of("0123456789") == std::string::npos) n = std::stoi(rp);
    Slice s = sliceRound(*info, r->r, n);
    JsonW j;
    writeRound(j, *info, *r, s, host.version);
    res.body = j.take();
}

int momentTick(const fd::HighlightInfo& h, double tickrate) {
    double tr = tickrate > 0 ? tickrate : 64;
    if (!h.segments.empty()) return h.segments.front().first + int(std::lround(2 * tr));
    return h.killTicks.empty() ? 0 : h.killTicks.front();
}

bool dropIdleReplay(int64_t idleSeconds) {
    std::lock_guard<std::mutex> lk(gParseMu);
    if (!gReplay.data || monoSeconds() - gReplay.used < idleSeconds) return false;
    gReplay = ReplayCache();
    std::lock_guard<std::mutex> li(gInfoMu);
    gInfo.reset();
    return true;
}

}  // namespace fdapi
