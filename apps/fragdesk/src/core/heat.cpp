#include "heat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "format.h"

namespace fd {

namespace {

struct Reader {
    const std::string& b;
    size_t o = 0;
    bool bad = false;

    bool need(size_t n) {
        if (bad || n > b.size() - o) {
            bad = true;
            return false;
        }
        return true;
    }
    uint8_t u8() { return need(1) ? uint8_t(b[o++]) : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(uint8_t(b[o]) | (uint8_t(b[o + 1]) << 8));
        o += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | uint8_t(b[o + i]);
        o += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32(), hi = u32();
        return lo | (hi << 32);
    }
    int16_t i16() { return int16_t(u16()); }
    int i32() { return int(int32_t(u32())); }
    float f32() {
        uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string str() {
        uint16_t n = u16();
        if (!need(n)) return {};
        std::string s = b.substr(o, n);
        o += n;
        return s;
    }
    Vec3s vec() {
        Vec3s v;
        v.x = i16();
        v.y = i16();
        v.z = i16();
        return v;
    }
};

constexpr uint32_t kMaxCount = 5'000'000;

// Runde zu einem Tick: Index der Runde, in deren Spielzeit er fällt (−1 = keine). live: nur bis zum Rundenende
// (Aufenthalt), sonst bis kurz danach (Kills nach dem Rundenende, die letzten Granaten).
int roundAt(const std::vector<RoundInfo>& rs, int tick, double tr, bool live) {
    auto it = std::upper_bound(rs.begin(), rs.end(), tick, [](int t, const RoundInfo& r) { return t < r.start; });
    if (it == rs.begin()) return -1;
    const RoundInfo& r = *(it - 1);
    int from = std::max(r.freezeEnd, r.start);
    int to = live ? r.end : (r.endOfficial > r.end ? r.endOfficial : r.end + int(5 * tr));
    if (tick < from || tick > to) return -1;
    return int(it - rs.begin()) - 1;
}

std::string clockText(double secs) { return fmtClock(std::floor(std::max(0.0, secs))); }   // wie die Uhr der 2D-Ansicht

const char* nadeName(uint8_t kind) {
    switch (kind) {
        case GrSmoke: return "Smoke";
        case GrFlash: return "Flash";
        case GrHE: return "HE-Granate";
        case GrMolotov: return "Molotov";
        case GrIncendiary: return "Brandgranate";
        default: return "Granate";
    }
}

int nadeLayer(uint8_t kind) {
    switch (kind) {
        case GrSmoke: return HlSmoke;
        case GrFlash: return HlFlash;
        case GrHE: return HlHE;
        case GrMolotov:
        case GrIncendiary: return HlMolotov;
        default: return -1;   // Decoys zeigt die Heatmap nicht
    }
}

void blur(std::vector<float>& g, int n, float sigma) {
    int r = std::max(1, int(std::ceil(sigma * 3)));
    std::vector<float> k(size_t(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[size_t(i + r)] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
    for (float& v : k) v /= sum;
    std::vector<float> tmp(g.size(), 0.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                int xx = x + i;
                if (xx >= 0 && xx < n) s += g[size_t(y) * n + xx] * k[size_t(i + r)];
            }
            tmp[size_t(y) * n + x] = s;
        }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                int yy = y + i;
                if (yy >= 0 && yy < n) s += tmp[size_t(yy) * n + x] * k[size_t(i + r)];
            }
            g[size_t(y) * n + x] = s;
        }
}

}  // namespace

bool parseHeat(const std::string& bytes, Heat& out, std::string& err) {
    out = Heat();
    Reader r{bytes};
    if (bytes.size() < 8 || bytes.compare(0, 4, "FDHM") != 0) {
        err = "keine Heatmap-Daten";
        return false;
    }
    r.o = 4;
    int version = r.u16();
    r.u16();
    if (version != kHeatVersion) {
        err = "andere Version (" + std::to_string(version) + ")";
        return false;
    }
    out.tickrate = r.f32();
    if (!(out.tickrate > 1 && out.tickrate < 1000)) out.tickrate = 64;
    out.map = r.str();
    out.players.resize(r.u16());
    for (auto& p : out.players) {
        uint64_t id = r.u64();
        p.steamId = id ? std::to_string(id) : std::string();
        p.name = r.str();
    }
    out.weapons.resize(r.u16());
    for (auto& w : out.weapons) w = r.str();

    uint32_t nk = r.u32();
    if (nk > kMaxCount) r.bad = true;
    if (!r.bad) out.kills.reserve(nk);
    for (uint32_t i = 0; i < nk && !r.bad; ++i) {
        HeatKill k;
        k.tick = r.i32();
        k.killer = r.u8();
        k.victim = r.u8();
        k.flags = r.u8();
        k.weapon = r.u8();
        k.killerPos = r.vec();
        k.victimPos = r.vec();
        out.kills.push_back(k);
    }
    uint32_t nn = r.u32();
    if (nn > kMaxCount) r.bad = true;
    if (!r.bad) out.nades.reserve(nn);
    for (uint32_t i = 0; i < nn && !r.bad; ++i) {
        HeatNade g;
        g.tick = r.i32();
        g.land = r.i32();
        g.kind = r.u8();
        g.thrower = r.u8();
        g.flags = r.u8();
        g.from = r.vec();
        g.to = r.vec();
        out.nades.push_back(g);
    }
    uint32_t nf = r.u32(), fbytes = r.u32();
    if (r.bad || nf > kMaxCount || !r.need(fbytes)) {
        err = "Abtastungen abgeschnitten";
        return false;
    }
    size_t fend = r.o + fbytes;
    out.frames.reserve(nf);
    for (uint32_t i = 0; i < nf && !r.bad; ++i) {
        HeatFrame f;
        f.tick = r.i32();
        f.count = r.u8();
        f.first = uint32_t(out.samples.size());
        for (int k = 0; k < f.count && !r.bad; ++k) {
            HeatSample s;
            s.player = r.u8();
            s.flags = r.u8();
            s.pos = r.vec();
            out.samples.push_back(s);
        }
        out.frames.push_back(f);
    }
    if (r.bad || r.o != fend || !r.need(4) || bytes.compare(r.o, 4, "END!") != 0) {
        err = "Datei unvollständig";
        return false;
    }
    return true;
}

HeatMatchInfo heatMatchInfo(const Match& m, const std::string& me, const std::string& label) {
    HeatMatchInfo info;
    info.checksum = m.checksum;
    info.label = label;
    info.tickrate = m.tickrate > 0 ? m.tickrate : 64;
    info.rounds = m.rounds;
    std::sort(info.rounds.begin(), info.rounds.end(), [](const RoundInfo& a, const RoundInfo& b) { return a.start < b.start; });
    if (!me.empty())
        if (const PlayerInfo* p = m.player(me)) info.myTeam = p->team;
    return info;
}

void addMatchHeat(HeatSet& set, const HeatMatchInfo& info, const Heat& h, const RadarMap& map, const std::string& me) {
    const int mi = int(set.matches.size());
    set.matches.push_back({info.checksum, info.label});
    if (!info.myTeam.empty()) set.hasMe = true;
    const double tr = h.tickrate > 0 ? h.tickrate : info.tickrate;
    auto who = [&](uint8_t player, bool ct, const RoundInfo& r) -> uint8_t {
        if (!me.empty() && player < h.players.size() && h.players[player].steamId == me) return WhoMe;
        std::string letter = (ct ? "CT" : "T") == r.sideA ? "A" : "B";
        if (info.myTeam.empty()) return letter == "A" ? WhoTeam : WhoEnemy;
        return letter == info.myTeam ? WhoTeam : WhoEnemy;
    };
    auto uv = [&](const Vec3s& p, float& u, float& v) {
        radarUV(map, p.x, p.y, u, v);
        return u > -0.05f && u < 1.05f && v > -0.05f && v < 1.05f;
    };
    auto name = [&](uint8_t i) -> std::string { return i < h.players.size() ? h.players[i].name : std::string("?"); };
    auto head = [&](const RoundInfo& r, int tick) {
        std::string s = info.label.empty() ? std::string() : info.label + "\n";
        return s + "Runde " + std::to_string(r.n) + " · " + clockText((tick - std::max(r.freezeEnd, r.start)) / tr);
    };

    // Aufenthalt
    set.points.reserve(set.points.size() + h.samples.size());
    for (const auto& f : h.frames) {
        int ri = roundAt(info.rounds, f.tick, tr, true);
        if (ri < 0) continue;
        const RoundInfo& r = info.rounds[size_t(ri)];
        for (uint32_t k = 0; k < f.count; ++k) {
            const HeatSample& s = h.samples[f.first + k];
            float u, v;
            if (!uv(s.pos, u, v)) continue;
            HeatPoint p;
            p.u = uint16_t(std::clamp(u, 0.0f, 1.0f) * 65535);
            p.v = uint16_t(std::clamp(v, 0.0f, 1.0f) * 65535);
            bool ct = s.flags & HsCT;
            p.side = ct ? SideCT : SideT;
            p.who = who(s.player, ct, r);
            p.lower = radarLower(map, s.pos.z);
            set.points.push_back(p);
        }
    }
    // Kills und Tode
    for (const auto& k : h.kills) {
        int ri = roundAt(info.rounds, k.tick, tr, false);
        if (ri < 0) continue;
        const RoundInfo& r = info.rounds[size_t(ri)];
        bool kct = k.flags & HkKillerCT, vct = k.flags & HkVictimCT;
        bool world = k.killer == kNoPlayer || k.killer == k.victim;
        std::string weapon = k.weapon < h.weapons.size() ? h.weapons[k.weapon] : std::string();
        std::string what = world ? name(k.victim) + " stirbt" : name(k.killer) + " tötet " + name(k.victim);
        if (!weapon.empty() || (k.flags & HkHeadshot))
            what += " (" + weapon + ((k.flags & HkHeadshot) ? (weapon.empty() ? "Kopfschuss" : ", Kopfschuss") : "") + ")";
        std::string tip = head(r, k.tick) + "\n" + what;
        HeatMark m;
        m.match = mi;
        m.round = r.n;
        m.tick = k.tick;
        m.tip = tip;
        if (!world && kct != vct && uv(k.killerPos, m.u, m.v)) {   // Teamkills zählen nicht als Kill
            m.fu = m.u;
            m.fv = m.v;
            m.layer = HlKills;
            m.side = kct ? SideCT : SideT;
            m.who = who(k.killer, kct, r);
            m.lower = m.fromLower = radarLower(map, k.killerPos.z);
            set.marks.push_back(m);
        }
        if (uv(k.victimPos, m.u, m.v)) {
            m.fu = m.u;
            m.fv = m.v;
            m.layer = HlDeaths;
            m.side = vct ? SideCT : SideT;
            m.who = who(k.victim, vct, r);
            m.lower = m.fromLower = radarLower(map, k.victimPos.z);
            set.marks.push_back(m);
        }
    }
    // Granaten
    for (const auto& g : h.nades) {
        int layer = nadeLayer(g.kind);
        if (layer < 0) continue;
        int ri = roundAt(info.rounds, g.land, tr, false);
        if (ri < 0) continue;
        const RoundInfo& r = info.rounds[size_t(ri)];
        HeatMark m;
        if (!uv(g.to, m.u, m.v)) continue;
        bool ct = g.flags & HnCT;
        m.thrown = !(g.flags & HnNoThrow) && uv(g.from, m.fu, m.fv);
        if (!m.thrown) {
            m.fu = m.u;
            m.fv = m.v;
        }
        m.layer = uint8_t(layer);
        m.side = ct ? SideCT : SideT;
        m.who = who(g.thrower, ct, r);
        m.lower = radarLower(map, g.to.z);
        m.fromLower = m.thrown ? radarLower(map, g.from.z) : m.lower;
        m.match = mi;
        m.round = r.n;
        m.tick = g.tick;
        m.tip = head(r, g.land) + "\n" + nadeName(g.kind) + " von " + name(g.thrower);
        set.marks.push_back(m);
    }
}

int heatDensity(const HeatSet& set, HeatLayer layer, const HeatFilter& f, bool lower, int n, float sigma, std::vector<float>& out) {
    out.assign(size_t(n) * size_t(n), 0.0f);
    int count = 0;
    auto add = [&](float u, float v) {
        int x = int(u * float(n)), y = int(v * float(n));
        if (x < 0 || y < 0 || x >= n || y >= n) return;
        out[size_t(y) * size_t(n) + size_t(x)] += 1;
        ++count;
    };
    if (layer == HlPresence) {
        for (const auto& p : set.points)
            if (p.lower == lower && f.match(p.who, p.side)) add(p.u / 65535.0f, p.v / 65535.0f);
    } else {
        for (const auto& m : set.marks)
            if (m.layer == layer && m.lower == lower && f.match(m.who, m.side)) add(m.u, m.v);
    }
    if (!count) return 0;
    blur(out, n, sigma);
    std::vector<float> nz;
    nz.reserve(out.size());
    for (float v : out)
        if (v > 1e-6f) nz.push_back(v);
    if (nz.empty()) return count;
    size_t k = std::min(nz.size() - 1, size_t(double(nz.size()) * 0.995));
    std::nth_element(nz.begin(), nz.begin() + long(k), nz.end());
    float ref = nz[k];
    if (ref <= 0) return count;
    for (float& v : out) v = std::min(1.0f, v / ref);
    return count;
}

}  // namespace fd
