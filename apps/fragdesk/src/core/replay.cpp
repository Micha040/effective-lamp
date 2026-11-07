#include "replay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

// Obergrenzen gegen kaputte Dateien (ein Match hat weit weniger)
constexpr uint32_t kMaxCount = 5'000'000;

}  // namespace

bool parseReplay(const std::string& bytes, Replay& out, std::string& err) {
    out = Replay();
    Reader r{bytes};
    if (bytes.size() < 8 || bytes.compare(0, 4, "FDRP") != 0) {
        err = "keine 2D-Daten";
        return false;
    }
    r.o = 4;
    int version = r.u16();
    r.u16();
    if (version != kReplayVersion) {
        err = "andere Version (" + std::to_string(version) + ")";
        return false;
    }
    out.tickrate = r.f32();
    if (!(out.tickrate > 1 && out.tickrate < 1000)) out.tickrate = 64;
    out.interval = std::max(1, int(r.u16()));
    out.map = r.str();

    uint16_t np = r.u16();
    out.players.resize(np);
    for (auto& p : out.players) {
        uint64_t id = r.u64();
        p.steamId = id ? std::to_string(id) : std::string();
        p.name = r.str();
    }
    uint16_t nw = r.u16();
    out.weapons.resize(nw);
    for (auto& w : out.weapons) w = r.str();

    uint32_t nf = r.u32(), fbytes = r.u32();
    if (r.bad || nf > kMaxCount || !r.need(fbytes)) {
        err = "Frames abgeschnitten";
        return false;
    }
    size_t fend = r.o + fbytes;
    out.frames.reserve(nf);
    out.samples.reserve(size_t(nf) * 8);
    for (uint32_t i = 0; i < nf && !r.bad; ++i) {
        ReplayFrame f;
        f.tick = r.i32();
        f.bomb = r.u8();
        if (f.bomb) f.bombPos = r.vec();
        f.count = r.u8();
        f.first = uint32_t(out.samples.size());
        for (int k = 0; k < f.count && !r.bad; ++k) {
            ReplaySample s;
            s.player = r.u8();
            s.pos = r.vec();
            s.yaw = r.u8();
            s.hp = r.u8();
            s.weapon = r.u8();
            s.flags = r.u8();
            out.samples.push_back(s);
        }
        // Ticks müssen aufsteigen (sonst funktioniert die Suche nicht): Rücksprünge auslassen
        if (!out.frames.empty() && f.tick <= out.frames.back().tick) {
            out.samples.resize(f.first);
            continue;
        }
        out.frames.push_back(f);
    }
    if (r.bad || r.o != fend) {
        err = "Frames beschädigt";
        return false;
    }

    uint32_t nt = r.u32();
    if (nt > kMaxCount) r.bad = true;
    for (uint32_t i = 0; i < nt && !r.bad; ++i) {
        ReplayTrack t;
        t.kind = r.u8();
        t.thrower = r.u8();
        t.ct = r.u8() == 1;
        uint16_t n = r.u16();
        t.points.reserve(n);
        for (int k = 0; k < n && !r.bad; ++k) {
            int tick = r.i32();
            t.points.emplace_back(tick, r.vec());
        }
        out.tracks.push_back(std::move(t));
    }
    uint32_t ne = r.u32();
    if (ne > kMaxCount) r.bad = true;
    for (uint32_t i = 0; i < ne && !r.bad; ++i) {
        ReplayEffect e;
        e.kind = r.u8();
        e.thrower = r.u8();
        e.ct = r.u8() == 1;
        e.start = r.i32();
        e.end = r.i32();
        e.pos = r.vec();
        uint8_t n = r.u8();
        for (int k = 0; k < n && !r.bad; ++k) {
            int16_t x = r.i16();
            e.hull.emplace_back(x, r.i16());
        }
        out.effects.push_back(std::move(e));
    }
    uint32_t nk = r.u32();
    if (nk > kMaxCount) r.bad = true;
    for (uint32_t i = 0; i < nk && !r.bad; ++i) {
        ReplayKill k;
        k.tick = r.i32();
        k.killer = r.u8();
        k.victim = r.u8();
        k.assister = r.u8();
        k.weapon = r.u8();
        k.flags = r.u8();
        k.killerPos = r.vec();
        k.victimPos = r.vec();
        out.kills.push_back(k);
    }
    uint32_t ns = r.u32();
    if (ns > kMaxCount) r.bad = true;
    out.shots.reserve(r.bad ? 0 : ns);
    for (uint32_t i = 0; i < ns && !r.bad; ++i) {
        ReplayShot s;
        s.tick = r.i32();
        s.player = r.u8();
        s.weapon = r.u8();
        out.shots.push_back(s);
    }
    uint32_t nb = r.u32();
    if (nb > kMaxCount) r.bad = true;
    for (uint32_t i = 0; i < nb && !r.bad; ++i) {
        ReplayBomb b;
        b.tick = r.i32();
        b.kind = r.u8();
        b.player = r.u8();
        b.pos = r.vec();
        out.bomb.push_back(b);
    }
    if (r.bad || !r.need(4) || bytes.compare(r.o, 4, "END!") != 0) {
        err = "Datei unvollständig";
        return false;
    }
    // Ereignisse nach Zeit sortieren (für die Suche beim Abspielen)
    auto byTick = [](const auto& a, const auto& b) { return a.tick < b.tick; };
    std::stable_sort(out.kills.begin(), out.kills.end(), byTick);
    std::stable_sort(out.shots.begin(), out.shots.end(), byTick);
    std::stable_sort(out.bomb.begin(), out.bomb.end(), byTick);
    std::stable_sort(out.effects.begin(), out.effects.end(), [](const ReplayEffect& a, const ReplayEffect& b) { return a.start < b.start; });
    return true;
}

size_t frameAt(const Replay& r, double tick) {
    if (r.frames.empty()) return 0;
    auto it = std::upper_bound(r.frames.begin(), r.frames.end(), tick, [](double t, const ReplayFrame& f) { return t < f.tick; });
    if (it == r.frames.begin()) return 0;
    return size_t(it - r.frames.begin()) - 1;
}

void playersAt(const Replay& r, double tick, std::vector<PlayerAt>& out) {
    out.clear();
    if (r.frames.empty()) return;
    size_t i = frameAt(r, tick);
    const ReplayFrame& a = r.frames[i];
    const ReplayFrame* b = i + 1 < r.frames.size() ? &r.frames[i + 1] : nullptr;
    // Lücke (Pause zwischen Runden, Aufnahme unterbrochen): nicht über mehr als zwei Abstände hinweg schieben
    if (b && b->tick - a.tick > r.interval * 2 + 2) b = nullptr;
    double f = b ? std::clamp((tick - a.tick) / double(b->tick - a.tick), 0.0, 1.0) : 0.0;
    for (int k = 0; k < a.count; ++k) {
        const ReplaySample& s = r.samples[a.first + k];
        PlayerAt p;
        p.player = s.player;
        p.hp = s.hp;
        p.weapon = s.weapon;
        p.flags = s.flags;
        p.x = s.pos.x;
        p.y = s.pos.y;
        p.z = s.pos.z;
        p.yaw = s.yaw * 360.0f / 256.0f;
        if (b) {
            for (int m = 0; m < b->count; ++m) {
                const ReplaySample& t = r.samples[b->first + m];
                if (t.player != s.player) continue;
                float dx = float(t.pos.x - s.pos.x), dy = float(t.pos.y - s.pos.y), dz = float(t.pos.z - s.pos.z);
                if (dx * dx + dy * dy < 600.0f * 600.0f) {   // größere Sprünge sind Neustarts, nicht Bewegung
                    p.x += float(f) * dx;
                    p.y += float(f) * dy;
                    p.z += float(f) * dz;
                    float dyaw = (t.yaw - s.yaw) * 360.0f / 256.0f;
                    if (dyaw > 180) dyaw -= 360;
                    if (dyaw < -180) dyaw += 360;
                    p.yaw = std::fmod(p.yaw + float(f) * dyaw + 360.0f, 360.0f);
                }
                if (f > 0.5) {
                    p.hp = t.hp;
                    p.weapon = t.weapon;
                    p.flags = t.flags;
                }
                break;
            }
        }
        out.push_back(p);
    }
}

const std::string& replayWeapon(const Replay& r, uint8_t idx) {
    static const std::string none;
    return idx < r.weapons.size() ? r.weapons[idx] : none;
}

}  // namespace fd
