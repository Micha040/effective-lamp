// FragDesk – Match-Daten lesen (siehe model.h)
#include "model.h"

#include <algorithm>
#include <mutex>

namespace fd {

// ---- Keys -----------------------------------------------------------------------------------------------------
namespace {

struct KeyTable {
    std::mutex mu;
    std::unordered_map<std::string, uint16_t> ids;
    std::vector<std::string> names;
    std::vector<bool> isMax;
};

KeyTable& table() {
    static KeyTable t;
    return t;
}

bool endsWith(const std::string& s, const char* suffix) {
    size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

}  // namespace

uint16_t Keys::id(const std::string& name) {
    KeyTable& t = table();
    std::lock_guard<std::mutex> lock(t.mu);
    auto it = t.ids.find(name);
    if (it != t.ids.end()) return it->second;
    uint16_t id = uint16_t(t.names.size());
    t.ids.emplace(name, id);
    t.names.push_back(name);
    t.isMax.push_back(endsWith(name, "Max"));
    return id;
}

int Keys::find(const std::string& name) {
    KeyTable& t = table();
    std::lock_guard<std::mutex> lock(t.mu);
    auto it = t.ids.find(name);
    return it == t.ids.end() ? -1 : int(it->second);
}

const std::string& Keys::name(uint16_t id) {
    KeyTable& t = table();
    std::lock_guard<std::mutex> lock(t.mu);
    static const std::string empty;
    return id < t.names.size() ? t.names[id] : empty;
}

bool Keys::isMax(uint16_t id) {
    KeyTable& t = table();
    std::lock_guard<std::mutex> lock(t.mu);
    return id < t.isMax.size() && t.isMax[id];
}

size_t Keys::count() {
    KeyTable& t = table();
    std::lock_guard<std::mutex> lock(t.mu);
    return t.names.size();
}

double Counters::get(const char* key) const {
    int id = Keys::find(key);
    return id < 0 ? 0.0 : get(uint16_t(id));
}

void Counters::set(uint16_t id, double x) {
    if (id >= v.size()) v.resize(size_t(id) + 1, 0.0);
    v[id] = x;
}

void Counters::merge(const Counters& o) {
    if (o.v.size() > v.size()) v.resize(o.v.size(), 0.0);
    for (size_t i = 0; i < o.v.size(); ++i) {
        if (o.v[i] == 0) continue;
        if (Keys::isMax(uint16_t(i))) v[i] = std::max(v[i], o.v[i]);
        else v[i] += o.v[i];
    }
}

bool Counters::empty() const {
    for (double x : v)
        if (x != 0) return false;
    return true;
}

bool readCounters(const json::Value& obj, Counters& out) {
    out.v.clear();
    if (!obj.isObject()) return false;
    for (const auto& kv : obj.obj)
        if (kv.second.isNumber()) out.set(Keys::id(kv.first), kv.second.num);
    return true;
}

// ---- Hilfen ---------------------------------------------------------------------------------------------------
namespace {

int num(const json::Value& v, const char* key, int def = 0) { return int(v.getNumber(key, def)); }
double dbl(const json::Value& v, const char* key, double def = 0) { return v.getNumber(key, def); }
std::string str(const json::Value& v, const char* key) { return v.getString(key); }
bool flag(const json::Value& v, const char* key) { return v.getBool(key, false); }

void readStrings(const json::Value& arr, std::vector<std::string>& out) {
    out.clear();
    if (!arr.isArray()) return;
    for (const auto& s : arr.arr)
        if (s.isString()) out.push_back(s.str);
}

}  // namespace

const PlayerRound* PlayerInfo::round(int n) const {
    for (const auto& r : rounds)
        if (r.n == n) return &r;
    return nullptr;
}

double HighlightInfo::seconds(double tickrate) const {
    double ticks = 0;
    for (const auto& s : segments) ticks += s.second - s.first;
    return tickrate > 0 ? ticks / tickrate : 0;
}

bool HighlightInfo::hasTag(const char* tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

const PlayerInfo* Match::player(const std::string& steamId) const {
    for (const auto& p : players)
        if (p.steamId == steamId) return &p;
    return nullptr;
}

const TeamInfo* Match::team(const std::string& letter) const {
    for (const auto& t : teams)
        if (t.letter == letter) return &t;
    return nullptr;
}

std::string Match::playerName(const std::string& steamId) const {
    const PlayerInfo* p = player(steamId);
    return p ? p->name : std::string();
}

bool parseMatch(const std::string& text, Match& m, std::string& error, bool details) {
    json::Value root;
    if (!json::parse(text, root, &error)) return false;
    if (!root.isObject()) {
        error = "kein Objekt";
        return false;
    }
    m = Match();
    m.schema = num(root, "schema");
    if (m.schema < 1 || m.schema > kMatchSchema) {
        error = "unbekanntes Format " + std::to_string(m.schema);
        return false;
    }
    m.analyzer = str(root, "analyzer");
    m.checksum = str(root, "checksum");

    const json::Value& demo = root["demo"];
    m.demoPath = str(demo, "path");
    m.demoFile = str(demo, "file");
    m.demoModified = str(demo, "modified");
    m.packed = str(demo, "packed");
    m.demoSize = int64_t(dbl(demo, "size"));

    const json::Value& mi = root["match"];
    m.source = str(mi, "source");
    m.mode = str(mi, "mode");
    m.map = str(mi, "map");
    m.date = str(mi, "date");
    m.serverName = str(mi, "serverName");
    m.shareCode = str(mi, "shareCode");
    m.winner = str(mi, "winner");
    m.duration = dbl(mi, "duration");
    m.tickrate = dbl(mi, "tickrate", 64);
    if (m.tickrate <= 0) m.tickrate = 64;
    m.tickCount = num(mi, "tickCount");
    m.maxRounds = num(mi, "maxRounds");
    m.overtimes = num(mi, "overtimes");
    m.scoreA = num(mi, "scoreA");
    m.scoreB = num(mi, "scoreB");
    m.buildNumber = num(mi, "buildNumber");
    m.ranked = flag(mi, "ranked");

    const json::Value& teams = root["teams"];
    if (teams.isArray()) {
        for (size_t i = 0; i < teams.arr.size() && i < 2; ++i) {
            const json::Value& t = teams.arr[i];
            TeamInfo& ti = m.teams[i];
            ti.letter = str(t, "letter");
            ti.name = str(t, "name");
            ti.startSide = str(t, "startSide");
            ti.score = num(t, "score");
            ti.scoreFirstHalf = num(t, "scoreFirstHalf");
            ti.scoreSecondHalf = num(t, "scoreSecondHalf");
        }
    }

    const json::Value& rounds = root["rounds"];
    if (rounds.isArray()) {
        m.rounds.reserve(rounds.arr.size());
        for (const auto& r : rounds.arr) {
            RoundInfo ri;
            ri.n = num(r, "n");
            ri.start = num(r, "start");
            ri.freezeEnd = num(r, "freezeEnd");
            ri.end = num(r, "end");
            ri.endOfficial = num(r, "endOfficial");
            ri.overtime = num(r, "overtime");
            ri.winner = str(r, "winner");
            ri.winnerSide = str(r, "winnerSide");
            ri.reason = str(r, "reason");
            ri.sideA = str(r, "sideA");
            ri.econA = str(r, "econA");
            ri.econB = str(r, "econB");
            ri.bomb = str(r, "bomb");
            ri.site = str(r, "site");
            ri.planter = str(r, "planter");
            ri.defuser = str(r, "defuser");
            ri.scoreA = num(r, "scoreA");
            ri.scoreB = num(r, "scoreB");
            ri.equipA = num(r, "equipA");
            ri.equipB = num(r, "equipB");
            ri.spentA = num(r, "spentA");
            ri.spentB = num(r, "spentB");
            ri.moneyA = num(r, "moneyA");
            ri.moneyB = num(r, "moneyB");
            ri.durationMs = int64_t(dbl(r, "durationMs"));
            ri.firstKill = num(r, "firstKill", -1);
            m.rounds.push_back(std::move(ri));
        }
    }

    const json::Value& players = root["players"];
    if (players.isArray()) {
        m.players.reserve(players.arr.size());
        for (const auto& p : players.arr) {
            PlayerInfo pi;
            pi.steamId = str(p, "steamId");
            pi.name = str(p, "name");
            pi.team = str(p, "team");
            pi.result = str(p, "result");
            pi.crosshair = str(p, "crosshair");
            pi.slot = num(p, "slot");
            pi.rankType = num(p, "rankType");
            pi.rankOld = num(p, "rankOld");
            pi.rankNew = num(p, "rankNew");
            pi.wins = num(p, "wins");
            pi.mvps = num(p, "mvps");
            pi.score = num(p, "score");
            pi.rating = dbl(p, "rating");
            pi.rating1 = dbl(p, "rating1");
            pi.kast = dbl(p, "kast");
            pi.adr = dbl(p, "adr");
            readCounters(p["stats"], pi.all);
            readCounters(p["statsT"], pi.t);
            readCounters(p["statsCT"], pi.ct);
            const json::Value& ws = p["weapons"];
            if (ws.isArray()) {
                for (const auto& w : ws.arr) {
                    WeaponStats st;
                    st.name = str(w, "name");
                    st.type = str(w, "type");
                    readCounters(w["stats"], st.c);
                    pi.weapons.push_back(std::move(st));
                }
            }
            const json::Value& db = p["deathsBy"];
            if (db.isObject())
                for (const auto& kv : db.obj)
                    if (kv.second.isNumber()) pi.deathsBy.emplace_back(kv.first, int(kv.second.num));
            std::sort(pi.deathsBy.begin(), pi.deathsBy.end(), [](const auto& a, const auto& b) {
                return a.second != b.second ? a.second > b.second : a.first < b.first;
            });
            if (details) {
                const json::Value& prs = p["rounds"];
                if (prs.isArray()) {
                    pi.rounds.reserve(prs.arr.size());
                    for (const auto& r : prs.arr) {
                        PlayerRound pr;
                        pr.n = num(r, "n");
                        pr.side = str(r, "side");
                        pr.won = flag(r, "won");
                        pr.dead = flag(r, "dead");
                        pr.traded = flag(r, "traded");
                        pr.kast = flag(r, "kast");
                        pr.clutchWon = flag(r, "clutchWon");
                        pr.plant = flag(r, "plant");
                        pr.defuse = flag(r, "defuse");
                        pr.k = num(r, "k");
                        pr.hs = num(r, "hs");
                        pr.a = num(r, "a");
                        pr.fa = num(r, "fa");
                        pr.dmg = num(r, "dmg");
                        pr.util = num(r, "util");
                        pr.deathTick = num(r, "deathTick");
                        pr.clutch = num(r, "clutch");
                        pr.equip = num(r, "equip");
                        pr.spent = num(r, "spent");
                        pr.money = num(r, "money");
                        pr.flashed = num(r, "flashed");
                        pr.firstKillTick = num(r, "firstKillTick");
                        pr.alive = dbl(r, "alive");
                        pr.killer = str(r, "killer");
                        pr.killerWeapon = str(r, "killerWeapon");
                        pr.econ = str(r, "econ");
                        pr.open = str(r, "open");
                        pi.rounds.push_back(std::move(pr));
                    }
                }
                const json::Value& opp = p["opponents"];
                if (opp.isArray()) {
                    for (const auto& o : opp.arr) {
                        OpponentStats os;
                        os.steamId = str(o, "steamId");
                        os.k = num(o, "k");
                        os.d = num(o, "d");
                        os.dmgDealt = num(o, "dmgDealt");
                        os.dmgTaken = num(o, "dmgTaken");
                        os.openK = num(o, "openK");
                        os.openD = num(o, "openD");
                        pi.opponents.push_back(std::move(os));
                    }
                }
            }
            m.players.push_back(std::move(pi));
        }
    }

    if (details) {
        const json::Value& kills = root["kills"];
        if (kills.isArray()) {
            m.kills.reserve(kills.arr.size());
            for (const auto& k : kills.arr) {
                KillInfo ki;
                ki.tick = num(k, "t");
                ki.round = num(k, "r");
                ki.killer = str(k, "k");
                ki.victim = str(k, "v");
                ki.assister = str(k, "a");
                ki.killerSide = str(k, "ks");
                ki.victimSide = str(k, "vs");
                ki.weapon = str(k, "w");
                ki.weaponType = str(k, "wt");
                ki.headshot = flag(k, "hs");
                ki.penetrated = num(k, "pen");
                ki.smoke = flag(k, "smoke");
                ki.noscope = flag(k, "noscope");
                ki.killerBlind = flag(k, "kBlind");
                ki.victimBlind = flag(k, "vBlind");
                ki.killerAir = flag(k, "kAir");
                ki.flashAssist = flag(k, "fa");
                ki.trade = flag(k, "trade");
                ki.traded = flag(k, "traded");
                ki.teamKill = flag(k, "tk");
                ki.distance = dbl(k, "dist");
                ki.kx = float(dbl(k, "kx"));
                ki.ky = float(dbl(k, "ky"));
                ki.vx = float(dbl(k, "vx"));
                ki.vy = float(dbl(k, "vy"));
                m.kills.push_back(std::move(ki));
            }
        }
        const json::Value& clutches = root["clutches"];
        if (clutches.isArray()) {
            for (const auto& c : clutches.arr) {
                ClutchInfo ci;
                ci.round = num(c, "r");
                ci.tick = num(c, "t");
                ci.player = str(c, "p");
                ci.side = str(c, "side");
                ci.vs = num(c, "vs");
                ci.won = flag(c, "won");
                ci.survived = flag(c, "survived");
                ci.kills = num(c, "kills");
                m.clutches.push_back(std::move(ci));
            }
        }
    }
    // Highlights auch ohne Details: die Bibliothek zeigt vorgeschlagene Momente über alle Matches
    const json::Value& hl = root["highlights"];
    if (hl.isArray()) {
        for (const auto& h : hl.arr) {
            HighlightInfo hi;
            hi.id = str(h, "id");
            hi.player = str(h, "p");
            hi.side = str(h, "side");
            hi.round = num(h, "r");
            hi.score = num(h, "score");
            hi.kills = num(h, "kills");
            hi.clutch = num(h, "clutch");
            readStrings(h["tags"], hi.tags);
            readStrings(h["weapons"], hi.weapons);
            const json::Value& segs = h["segments"];
            if (segs.isArray())
                for (const auto& s : segs.arr)
                    if (s.isArray() && s.arr.size() == 2 && s.arr[0].isNumber() && s.arr[1].isNumber())
                        hi.segments.emplace_back(int(s.arr[0].num), int(s.arr[1].num));
            const json::Value& kt = h["killTicks"];
            if (kt.isArray())
                for (const auto& t : kt.arr)
                    if (t.isNumber()) hi.killTicks.push_back(int(t.num));
            m.highlights.push_back(std::move(hi));
        }
    }
    return true;
}

}  // namespace fd
