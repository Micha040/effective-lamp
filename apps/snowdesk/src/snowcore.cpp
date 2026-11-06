// SnowDesk – Schnee-Simulation. Siehe snowcore.h.
#include "snowcore.h"

#include <algorithm>
#include <cmath>

namespace snow {

namespace {
const float kPi = 3.14159265358979f;
const float kBaseRatePer1000Dip = 8.0f;    // sichtbare Flocken pro Sekunde je 1000 DIP Bildschirmbreite bei Menge 1
const float kVirtualPer1000Dip = 600.0f;   // unsichtbare Schnee-Portionen pro Sekunde je 1000 DIP (gleichmäßiges Wachsen)
const float kDepositSigmaDip = 9.0f;       // so breit verteilt sich eine Portion auf der Schneedecke
const float kSettleTau = 1.6f;             // s: frisch gelandeter Schnee sinkt weich in die Decke ein
const float kFillBoost = 1.05f;            // gleicht das sanfte Abbremsen kurz vor voll aus (≈ 92 % nach fillMinutes)
const float kSoftCapFrom = 0.75f;          // ab 75 % der Maximalhöhe wächst die Decke langsamer
const float kModMin = 0.84f;               // natürliche Wellen: Maximalhöhe schwankt zwischen 84 und 100 %
const float kNoseBase = 0.3f;              // runde Enden: Dicke ganz am Rand (Anteil der Banddicke)
const float kNoseLength = 0.9f;            // Länge der Rundung (Anteil der Banddicke)
const float kSpillShare = 0.2f;            // Anteil des Überstands an den Enden, der als Bröckchen herunterrieselt
const int kMaxResting = 600;
// Regen & Wasser
const float kRainPer1000Dip = 140.0f;      // Regenspuren pro Sekunde je 1000 DIP Breite bei Regen 1 (mäßig)
const float kEvapPerSec = 0.0028f;         // Verdunsten ohne Regen (Tropfen-Einheiten pro Sekunde)
const float kSlideVol = 0.75f;             // ab dieser Größe (+ Zufall je Tropfen) rollt er zur nächsten Ecke
const float kMeltDip2PerVol = 260.0f;      // so viel geschmolzener Schnee (DIP²) ergibt einen Tropfen

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothf(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// Profil der runden Enden: am Rand kNoseBase·T, nach der Länge L volle Banddicke T (Viertelkreis)
inline float noseProfile(float T, float d, float L) {
    float t = 1.0f - d / L;
    return T * (kNoseBase + (1.0f - kNoseBase) * std::sqrt(std::max(0.0f, 1.0f - t * t)));
}
}  // namespace

World::World(uint64_t seed) : rng_(seed ? seed * 0x9E3779B97F4A7C15ull : 0x2545F4914F6CDD1Dull) {
    windPhase1_ = rnd(0.0f, 6.28f);
    windPhase2_ = rnd(0.0f, 6.28f);
    gustTimer_ = rnd(4.0f, 12.0f);
}

float World::rnd() {
    // xorshift64*
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    uint64_t r = rng_ * 0x2545F4914F6CDD1Dull;
    return (float)((double)(r >> 40) * (1.0 / 16777216.0));
}

float World::rnd(float a, float b) { return a + (b - a) * rnd(); }

// ------------------------------------------------------------------ Einstellungen

void World::configure(int widthPx, int heightPx, float dpiScale) {
    float scale = dpiScale > 0.1f ? dpiScale : 1.0f;
    bool changed = widthPx != width_ || heightPx != height_ || std::fabs(scale - scale_) > 0.001f;
    width_ = widthPx;
    height_ = heightPx;
    scale_ = scale;
    if (changed) {
        parts_.clear();
        pending_.clear();
        piles_.clear();
        restingCount_ = 0;
    }
    recalc();
}

void World::setParams(const Params& p) {
    Params q = p;
    q.amount = clampf(q.amount, 0.0f, 6.0f);
    q.flakeSize = clampf(q.flakeSize, 0.3f, 3.0f);
    q.fallSpeed = clampf(q.fallSpeed, 0.2f, 3.0f);
    q.wind = clampf(q.wind, -1.0f, 1.0f);
    q.rain = clampf(q.rain, 0.0f, 4.0f);
    q.fog = clampf(q.fog, 0.0f, 1.0f);
    q.meltDipPerMin = clampf(q.meltDipPerMin, 0.0f, 120.0f);
    q.maxPileDip = clampf(q.maxPileDip, 0.0f, 200.0f);
    q.fillMinutes = clampf(q.fillMinutes, 0.1f, 120.0f);
    q.crystalShare = clampf(q.crystalShare, 0.0f, 1.0f);
    if (q.releaseStyle < ReleaseMix || q.releaseStyle > ReleaseGentle) q.releaseStyle = ReleaseMix;
    float oldMax = maxPilePx_;
    p_ = q;
    recalc();
    if (std::fabs(maxPilePx_ - oldMax) > 0.01f) {
        for (Pile& pile : piles_) {
            bool absorb = maxPilePx_ <= 0.5f || (!pile.floor && pile.top < dip(3.0f));
            if (absorb && !pile.absorb) {
                // "nichts bleibt liegen": vorhandener Schnee fällt herunter (Boden: verschwindet)
                if (pile.floor) {
                    for (int i = 0; i < pile.n; ++i) {
                        lostMass_ += (pile.h[i] + pile.pend[i]) * cell_;
                        pile.h[i] = pile.pend[i] = 0.0f;
                    }
                    lostMass_ += pile.spillAcc[0] + pile.spillAcc[1];
                    pile.spillAcc[0] = pile.spillAcc[1] = 0.0f;
                } else {
                    releaseCells(pile, 0, pile.n, 0.0f);
                    lostMass_ += pile.spillAcc[0] + pile.spillAcc[1];
                    pile.spillAcc[0] = pile.spillAcc[1] = 0.0f;
                }
            }
            pile.absorb = absorb;
            if (!absorb && maxPilePx_ < oldMax) trimToCap(pile);   // kleinere Maximalhöhe: Überstand fällt herunter
        }
        flushPending();
    }
}

void World::recalc() {
    cell_ = std::max(1.0f, std::round(2.0f * scale_));
    maxPilePx_ = p_.maxPileDip * scale_;
    gravity_ = dip(1500.0f);
    // Dickenzuwachs so, dass eine Kante bei Menge 1 nach fillMinutes (fast) voll ist
    growth_ = maxPilePx_ > 0.5f ? maxPilePx_ / (p_.fillMinutes * 60.0f) * kFillBoost : 0.0f;
}

float World::spawnRate() const {
    if (!snowing_ || thawing_ || width_ <= 0) return 0.0f;
    return kBaseRatePer1000Dip * (width_ / scale_) / 1000.0f * p_.amount;
}

float World::rainRate() const {
    if (!snowing_ || thawing_ || width_ <= 0 || p_.rain <= 0.0f) return 0.0f;
    return kRainPer1000Dip * (width_ / scale_) / 1000.0f * p_.rain;
}

// ------------------------------------------------------------------ Flocken

World::Particle World::makeSnowParticle(float y, bool fadeIn) {
    Particle q{};
    float r = rnd();
    float depth;
    if (r < 0.45f) depth = rnd(0.0f, 0.35f);
    else if (r < 0.86f) depth = rnd(0.35f, 0.8f);
    else depth = rnd(0.8f, 1.0f);
    q.depth = depth;
    q.y = y;
    q.fadeIn = fadeIn ? 0.0f : 1.0f;
    q.seed = rnd();
    bool crystal = depth >= 0.4f && rnd() < p_.crystalShare;
    float sizeDip, fallDip;
    if (crystal) {
        q.kind = KindCrystal;
        sizeDip = rnd(8.0f, 13.0f) * lerpf(0.85f, 1.15f, depth);
        q.alpha = rnd(0.8f, 0.95f);
        q.soft = 0.15f;
        fallDip = rnd(34.0f, 52.0f);
        q.rot = rnd(0.0f, 2.0f * kPi);
        q.rotSpeed = rnd(0.15f, 0.6f) * (rnd() < 0.5f ? -1.0f : 1.0f);
    } else if (depth < 0.35f) {
        q.kind = KindSoft;
        sizeDip = rnd(1.6f, 2.6f);
        q.alpha = rnd(0.4f, 0.6f);
        q.soft = 0.35f;
        fallDip = rnd(24.0f, 34.0f);
    } else if (depth < 0.8f) {
        q.kind = KindSoft;
        sizeDip = rnd(2.6f, 4.4f);
        q.alpha = rnd(0.7f, 0.9f);
        q.soft = 0.4f;
        fallDip = rnd(38.0f, 58.0f);
    } else {
        q.kind = KindSoft;
        sizeDip = rnd(5.0f, 9.0f);
        q.alpha = rnd(0.5f, 0.75f);
        q.soft = rnd(0.75f, 1.0f);
        fallDip = rnd(62.0f, 92.0f);
    }
    q.size = std::max(1.0f, dip(sizeDip * p_.flakeSize));
    q.fall = dip(fallDip * p_.fallSpeed);
    float swayDip = depth < 0.35f ? rnd(4.0f, 8.0f) : (depth < 0.8f ? rnd(6.0f, 12.0f) : rnd(8.0f, 16.0f));
    q.swayAmp = dip(swayDip);
    q.swayFreq = 2.0f * kPi * rnd(0.25f, 0.8f);
    q.swayPhase = rnd(0.0f, 2.0f * kPi);
    q.mass = 0.0f;   // sichtbare Flocken sind Dekoration; die Schneedecke wächst aus den unsichtbaren Portionen
    return q;
}

void World::spawnFlake(float y, bool fadeIn) {
    Particle q = makeSnowParticle(y, fadeIn);
    // Bei Wind auch seitlich "vor" dem Bildschirm starten, damit die Dichte überall gleich bleibt
    float margin = std::fabs(windNow_) * (height_ / std::max(dip(20.0f), q.fall));
    margin = std::min(margin, (float)width_);
    if (windNow_ >= 0) q.x = rnd(-margin, (float)width_);
    else q.x = rnd(0.0f, (float)width_ + margin);
    pending_.push_back(q);
}

void World::prefill() {
    if (width_ <= 0 || height_ <= 0) return;
    float floorTop = (float)height_;
    for (const Pile& p : piles_) {
        if (p.floor) floorTop = std::min(floorTop, p.top);
    }
    float avgFall = dip(48.0f * p_.fallSpeed);
    int count = (int)(spawnRate() * (floorTop / avgFall));
    count = std::min(count, 3000);
    for (int i = 0; i < count; ++i) {
        Particle q = makeSnowParticle(rnd(0.0f, floorTop), true);
        q.x = rnd(0.0f, (float)width_);
        pending_.push_back(q);
    }
    flushPending();
}

void World::emitCrumb(float x, float y, float mass, float vx, float vy, uint64_t ignoreId) {
    if (mass <= 0) return;
    Particle q{};
    q.kind = KindCrumb;
    q.ignoreId = ignoreId;
    q.x = x;
    q.y = y;
    q.vx = vx;
    q.vy = vy;
    q.mass = mass;
    q.size = std::max(dip(1.6f), std::sqrt(mass) * 1.1f);
    q.alpha = rnd(0.85f, 0.95f);
    q.soft = 0.3f;
    q.depth = 0.7f;
    q.rot = rnd(0.0f, 2.0f * kPi);
    q.rotSpeed = rnd(-2.0f, 2.0f);
    q.seed = rnd();
    q.fadeIn = 1.0f;
    pending_.push_back(q);   // Masse stammt aus einer Schneedecke (dort schon gezählt)
}

void World::emitClump(float x, float y, float mass, float vx, float vy, uint64_t ignoreId) {
    if (mass <= 0) return;
    Particle q{};
    q.kind = KindClump;
    q.ignoreId = ignoreId;
    q.x = x;
    q.y = y;
    q.vx = vx;
    q.vy = vy;
    q.mass = mass;
    q.size = std::max(dip(3.0f), std::sqrt(mass) * 1.15f);
    q.alpha = rnd(0.92f, 0.98f);
    q.soft = 0.22f;
    q.depth = 0.8f;
    q.rot = rnd(0.0f, 2.0f * kPi);
    q.rotSpeed = rnd(0.5f, 2.0f) * (rnd() < 0.5f ? -1.0f : 1.0f);
    q.seed = rnd();
    q.fadeIn = 1.0f;
    pending_.push_back(q);
}

void World::emitPuffs(float x, float y, float mass) {
    float strength = clampf(std::sqrt(mass) / dip(5.0f), 0.3f, 1.8f);
    int count = (int)clampf(std::round(std::sqrt(mass) / dip(2.2f)), 2.0f, 12.0f);
    for (int i = 0; i < count; ++i) {
        Particle q{};
        q.kind = KindPuff;
        float angle = rnd(-kPi * 0.95f, -kPi * 0.05f);   // nach oben/seitlich
        float speed = dip(rnd(40.0f, 140.0f)) * strength;
        q.x = x + rnd(-1.0f, 1.0f) * dip(3.0f);
        q.y = y - dip(1.0f);
        q.vx = std::cos(angle) * speed;
        q.vy = std::sin(angle) * speed;
        q.size = dip(rnd(2.5f, 5.5f)) * clampf(strength, 0.6f, 1.4f);
        q.alpha = rnd(0.35f, 0.6f);
        q.soft = 1.0f;
        q.depth = 0.8f;
        q.maxLife = q.life = rnd(0.35f, 0.75f);
        q.seed = rnd();
        q.fadeIn = 1.0f;
        pending_.push_back(q);
    }
}

void World::flushPending() {
    if (pending_.empty()) return;
    const size_t maxParticles = 8000;
    for (Particle& q : pending_) {
        if (parts_.size() >= maxParticles) {
            lostMass_ += q.mass;
            if (q.resting) --restingCount_;
            continue;
        }
        parts_.push_back(q);
    }
    pending_.clear();
}

// ------------------------------------------------------------------ Plattformen & Schneedecken

bool World::isAbsorbing(const PlatformIn& in) const {
    if (maxPilePx_ <= 0.5f) return true;               // "nichts bleibt liegen"
    return !in.floor && in.top < dip(3.0f);           // maximiert/oben am Rand: kein Platz für Schnee
}

float World::capOf(const Pile& p) const {
    return std::max(0.0f, std::min(maxPilePx_, p.top - 1.0f));
}

void World::initShape(Pile& p) const {
    p.drop.assign(p.n, 0.0f);
    p.mod.assign(p.n, 1.0f);
    if (p.corner > 0.5f) {
        float r = p.corner;
        for (int i = 0; i < p.n; ++i) {
            float x = p.left + (i + 0.5f) * cell_;
            float d = std::min(x - p.left, p.right - x);
            if (d < r) {
                float t = r - d;
                p.drop[i] = r - std::sqrt(std::max(0.0f, r * r - t * t));
            }
        }
    }
    // Natürliche Wellen der Schneedecke: fest je Fenster, an Bildschirmkoordinaten gebunden (bleibt bei Größenänderung)
    uint64_t s = (p.id + 1) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    auto next = [&s]() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return (float)((double)((s * 0x2545F4914F6CDD1Dull) >> 40) * (1.0 / 16777216.0));
    };
    float l1 = dip(150.0f + 140.0f * next()), l2 = dip(55.0f + 45.0f * next());
    float ph1 = 2.0f * kPi * next(), ph2 = 2.0f * kPi * next();
    for (int i = 0; i < p.n; ++i) {
        float x = p.left + (i + 0.5f) * cell_;
        float w = 0.5f + 0.5f * (0.62f * std::sin(2.0f * kPi * x / l1 + ph1) + 0.38f * std::sin(2.0f * kPi * x / l2 + ph2));
        p.mod[i] = kModMin + (1.0f - kModMin) * w;
    }
}

World::Pile World::makePile(const PlatformIn& in) const {
    Pile p;
    p.id = in.id;
    p.floor = in.floor;
    p.left = in.left;
    p.right = in.right;
    p.top = in.top;
    p.corner = in.floor ? 0.0f : std::max(0.0f, in.corner);
    p.n = std::max(1, (int)std::ceil((in.right - in.left) / cell_));
    p.h.assign(p.n, 0.0f);
    p.pend.assign(p.n, 0.0f);
    p.absorb = isAbsorbing(in);
    p.empty = true;
    p.spillAcc[0] = p.spillAcc[1] = 0.0f;
    p.settle = 0.0f;
    p.meltWater = 0.0f;
    p.settling = false;
    initShape(p);
    return p;
}

int World::cellOf(const Pile& p, float x) const {
    int i = (int)std::floor((x - p.left) / cell_);
    return i < 0 ? 0 : (i >= p.n ? p.n - 1 : i);
}

float World::surfaceAt(const Pile& p, float x) const {
    int i = cellOf(p, x);
    return p.top + p.drop[i] - p.h[i];
}

void World::setPlatforms(const std::vector<PlatformIn>& platforms) {
    std::vector<Pile> next;
    next.reserve(platforms.size());
    std::vector<char> used(piles_.size(), 0);
    size_t unchanged = 0;   // nur bei echten Änderungen neu zeichnen (wird im Ruhezustand alle 0,5 s aufgerufen)

    for (const PlatformIn& in : platforms) {
        if (in.right - in.left < 4.0f) continue;
        int found = -1;
        for (size_t k = 0; k < piles_.size(); ++k) {
            if (!used[k] && piles_[k].id == in.id && piles_[k].floor == in.floor) {
                found = (int)k;
                break;
            }
        }
        if (found < 0) {
            next.push_back(makePile(in));
            continue;
        }
        used[found] = 1;
        Pile& old = piles_[found];
        float dTop = std::fabs(in.top - old.top);
        float dL = in.left - old.left, dR = in.right - old.right;
        bool absorb = isAbsorbing(in);

        if (dTop <= 1.5f && std::fabs(dL) <= 1.5f && std::fabs(dR) <= 1.5f && absorb == old.absorb) {
            next.push_back(std::move(old));          // unverändert (1-Pixel-Zittern ignorieren)
            ++unchanged;
            continue;
        }
        if (old.floor) {
            // Boden (Taskleiste): Schnee bleibt liegen, wird nur an die neue Lage angepasst
            Pile np = makePile(in);
            for (int k = 0; k < std::min(np.n, old.n); ++k) {
                np.h[k] = old.h[k];
                np.pend[k] = old.pend[k];
            }
            for (int k = np.n; k < old.n; ++k) lostMass_ += (old.h[k] + old.pend[k]) * cell_;
            np.empty = false;
            if (np.absorb) {
                for (int k = 0; k < np.n; ++k) {
                    lostMass_ += (np.h[k] + np.pend[k]) * cell_;
                    np.h[k] = np.pend[k] = 0.0f;
                }
            } else {
                trimToCap(np);
            }
            next.push_back(std::move(np));
            continue;
        }
        if (dTop > 1.5f || std::fabs(dL - dR) <= 1.5f || absorb != old.absorb) {
            releaseCells(old, 0, old.n, 0.0f);        // verschoben: alles fällt
            releaseBeads(old, old.left - 1.0f, old.right + 1.0f);
            lostMass_ += old.spillAcc[0] + old.spillAcc[1];
            next.push_back(makePile(in));
            next.back().settle = 0.35f;               // während des Ziehens bleibt nichts liegen
            continue;
        }
        // Nur die Breite hat sich geändert: Schnee ohne Halt fällt, der Rest bleibt an seiner Stelle
        Pile np = makePile(in);
        int a = 0;
        while (a < old.n && old.left + (a + 0.5f) * cell_ < np.left) ++a;
        int b = old.n;
        while (b > a && old.left + (b - 0.5f) * cell_ >= np.right) --b;
        if (a > 0) releaseCells(old, 0, a, 0.0f);
        if (b < old.n) releaseCells(old, b, old.n, 0.0f);
        for (int k = a; k < b; ++k) {
            float x = old.left + (k + 0.5f) * cell_;
            int c = cellOf(np, x);
            np.h[c] += old.h[k];
            np.pend[c] += old.pend[k];
        }
        np.spillAcc[0] = old.spillAcc[0];
        np.spillAcc[1] = old.spillAcc[1];
        np.empty = false;
        // Tropfen: an einer verschobenen Ecke hängende fallen, liegende ohne Halt auch, der Rest bleibt
        {
            const float endL = np.left + std::max(np.corner * 0.6f, dip(2.0f)), endR = np.right - std::max(np.corner * 0.6f, dip(2.0f));
            for (const Bead& b : old.beads) {
                const float r = beadRadius(b.vol);
                if (b.hang >= 0) {
                    const bool moved = b.hang == 0 ? std::fabs(dL) > 1.5f : std::fabs(dR) > 1.5f;
                    if (!moved) {
                        np.beads.push_back(b);
                        np.beads.back().x = b.hang == 0 ? np.left : np.right;
                    } else {
                        emitDrop(hangX(old, b.hang, r), hangY(old, r, 1.0f), b.vol, 0.0f, dip(10.0f), old.id);
                    }
                } else if (b.x > endL && b.x < endR) {
                    np.beads.push_back(b);
                } else {
                    emitDrop(b.x, edgeY(old, b.x) - r * 0.8f, b.vol, 0.0f, dip(10.0f), old.id);
                }
            }
            np.meltWater = old.meltWater;
        }
        trimToCap(np);            // neue Ecken/Wellen: Überstand fällt herunter
        next.push_back(std::move(np));
    }

    for (size_t k = 0; k < piles_.size(); ++k) {
        if (used[k]) continue;
        Pile& p = piles_[k];
        if (!p.floor) {
            releaseCells(p, 0, p.n, 0.0f);   // Fenster weg/minimiert: alles fällt
            releaseBeads(p, p.left - 1.0f, p.right + 1.0f);
        } else {
            for (int i = 0; i < p.n; ++i) lostMass_ += (p.h[i] + p.pend[i]) * cell_;
        }
        lostMass_ += p.spillAcc[0] + p.spillAcc[1];
    }
    const bool same = unchanged == piles_.size() && unchanged == next.size();
    piles_.swap(next);
    flushPending();
    if (!same) ++version_;
}

// Überstand über der (gewellten) Maximalhöhe fällt als Bröckchen herunter (Boden: verschwindet)
void World::trimToCap(Pile& p) {
    if (p.absorb) return;
    float cap = capOf(p);
    int i = 0;
    while (i < p.n) {
        int j = std::min(p.n, i + 8);
        float mass = 0.0f;
        for (int k = i; k < j; ++k) {
            float c = cap * p.mod[k];
            if (p.h[k] > c) {
                mass += (p.h[k] - c) * cell_;
                p.h[k] = c;
            }
        }
        if (mass > 0.0f) {
            if (p.floor) {
                lostMass_ += mass;
            } else {
                float x = p.left + (i + j) * 0.5f * cell_;
                int mid = std::min(p.n - 1, (i + j) / 2);
                emitCrumb(x, p.top + p.drop[mid] - p.h[mid] - dip(1.0f), mass, rnd(-1.0f, 1.0f) * dip(12.0f), 0.0f, p.id);
            }
        }
        i = j;
    }
}

void World::releaseCells(Pile& p, int from, int to, float vxCarry) {
    for (int k = from; k < to; ++k) {
        lostMass_ += p.pend[k] * cell_;   // noch nicht sichtbar eingesunkener Schnee verschwindet einfach
        p.pend[k] = 0.0f;
    }
    int i = from;
    while (i < to) {
        int w = (int)rnd(4.0f, 11.0f);
        int j = std::min(to, i + std::max(1, w));
        float mass = 0;
        for (int k = i; k < j; ++k) mass += p.h[k];
        mass *= cell_;
        if (mass > 0.02f) releaseChunk(p, i, j, mass, vxCarry);
        else lostMass_ += mass;
        for (int k = i; k < j; ++k) p.h[k] = 0.0f;
        i = j;
    }
    releaseResting(p.id, p.left + from * cell_, p.left + to * cell_);
}

// Flocken, die gerade auf diesem Stück Schneedecke liegen, fallen mit herunter
void World::releaseResting(uint64_t pileId, float x0, float x1) {
    if (restingCount_ <= 0) return;
    auto fall = [&](Particle& q) {
        if (!q.resting || q.restId != pileId || q.x < x0 || q.x >= x1) return;
        q.resting = 0;
        q.restId = 0;
        q.ignoreId = pileId;
        q.vx = 0.0f;
        q.vy = 0.0f;
        --restingCount_;
    };
    for (Particle& q : parts_) fall(q);
    for (Particle& q : pending_) fall(q);
}

void World::releaseChunk(const Pile& p, int from, int to, float mass, float vxCarry) {
    float x0 = p.left + from * cell_;
    float x1 = p.left + to * cell_;
    float avgH = mass / std::max(cell_, (x1 - x0));
    float avgDrop = 0;
    for (int k = from; k < to; ++k) avgDrop += p.drop[k];
    avgDrop /= std::max(1, to - from);
    float cx = (x0 + x1) * 0.5f;
    float cy = p.top + avgDrop - avgH * 0.5f;
    float s2 = scale_ * scale_;
    uint64_t src = p.id;                       // heruntergefallener Schnee landet nicht wieder auf demselben Fenster
    auto vx = [&](float spread) { return vxCarry + rnd(-1.0f, 1.0f) * dip(spread); };
    auto vy = [&]() { return dip(rnd(-8.0f, 18.0f)); };
    // Die Schneedecke bricht nicht als starre Linie ab: jedes Stück löst sich einen Augenblick später
    struct Hold {
        World& w;
        size_t first;
        ~Hold() {
            float base = w.rnd(0.0f, 0.06f);
            for (size_t i = first; i < w.pending_.size(); ++i) {
                Particle& q = w.pending_[i];
                if (q.kind == KindClump || q.kind == KindCrumb) q.life = base + w.rnd(0.0f, 0.14f);
            }
        }
    } hold{*this, pending_.size()};

    int style = p_.releaseStyle;
    if (style == ReleaseClump) {
        if (mass >= 8.0f * s2) emitClump(cx, cy, mass, vx(12.0f), vy(), src);
        else emitCrumb(cx, cy, mass, vx(12.0f), vy(), src);
    } else if (style == ReleaseCrumble) {
        int k = (int)clampf(std::round(mass / (6.0f * s2)), 1.0f, 10.0f);
        for (int i = 0; i < k; ++i) emitCrumb(rnd(x0, x1), cy + rnd(-1.0f, 1.0f) * avgH * 0.4f, mass / k, vx(20.0f), vy(), src);
    } else if (style == ReleaseGentle) {
        int k = (int)clampf(std::round(mass / (3.0f * s2)), 1.0f, 12.0f);
        for (int i = 0; i < k; ++i) {
            Particle q = makeSnowParticle(cy + rnd(-1.0f, 1.0f) * avgH * 0.4f, false);
            q.kind = KindSoft;
            q.x = rnd(x0, x1);
            q.depth = 0.6f;
            q.size = std::max(dip(2.0f), std::min(dip(6.0f), std::sqrt(mass / k) * 0.9f));
            q.alpha = rnd(0.75f, 0.9f);
            q.soft = 0.45f;
            q.mass = mass / k;
            q.vy = -dip(rnd(2.0f, 10.0f));
            q.ignoreId = src;
            pending_.push_back(q);
        }
    } else {
        // Mischung: große Brocken als Klumpen (+ ein, zwei Brösel), kleine zerbröseln
        if (mass >= 30.0f * s2) {
            float clumpMass = mass * 0.75f;
            emitClump(cx, cy, clumpMass, vx(10.0f), vy(), src);
            int k = (int)clampf(std::round((mass - clumpMass) / (6.0f * s2)), 1.0f, 3.0f);
            for (int i = 0; i < k; ++i) emitCrumb(rnd(x0, x1), cy + rnd(-1.0f, 1.0f) * avgH * 0.4f, (mass - clumpMass) / k, vx(20.0f), vy(), src);
        } else {
            int k = (int)clampf(std::round(mass / (6.0f * s2)), 1.0f, 4.0f);
            for (int i = 0; i < k; ++i) emitCrumb(rnd(x0, x1), cy, mass / k, vx(16.0f), vy(), src);
        }
    }
}

// Schnee-Portion auf die Decke legen: weich verteilt (Glockenkurve), sinkt dann in kSettleTau ein
void World::deposit(Pile& p, float x, float mass, float sigma) {
    if (mass <= 0.0f) return;
    if (p.absorb) {
        lostMass_ += mass;
        return;
    }
    sigma = std::max(sigma, cell_);
    float reach = 2.5f * sigma;
    int a = cellOf(p, x - reach), b = cellOf(p, x + reach);
    float inv = 1.0f / sigma;
    float wsum = 0.0f;
    for (int i = a; i <= b; ++i) {
        float d = (p.left + (i + 0.5f) * cell_ - x) * inv;
        wsum += std::exp(-0.5f * d * d);
    }
    if (wsum <= 1e-6f) {
        p.pend[cellOf(p, x)] += mass / cell_;
    } else {
        float k = mass / (wsum * cell_);
        for (int i = a; i <= b; ++i) {
            float d = (p.left + (i + 0.5f) * cell_ - x) * inv;
            p.pend[i] += k * std::exp(-0.5f * d * d);
        }
    }
    p.empty = false;
}

// Unsichtbare Schnee-Portionen auf den Flugbahnen der Flocken (gerade Linie mit aktuellem Wind): die Decke wächst
// gleichmäßig, Fenster im Windschatten bekommen weniger ab.
void World::depositVirtual(float dt) {
    if (growth_ <= 0.0f || p_.amount <= 0.0f || piles_.empty() || p_.wetSnow) return;
    float rate = kVirtualPer1000Dip * (width_ / scale_) / 1000.0f;   // Portionen pro Sekunde über die Breite
    float maxSlope = std::fabs(windNow_) * 1.25f / (dip(26.0f) * p_.fallSpeed);
    float ext = std::min((float)width_ * 2.0f, maxSlope * height_);
    float rateExt = rate * (width_ + ext) / width_;
    virtAcc_ += rateExt * dt;
    int n = (int)virtAcc_;
    virtAcc_ -= n;
    if (n <= 0) return;
    float massEach = growth_ * p_.amount * width_ / rate;
    float sigma = dip(kDepositSigmaDip);
    for (int k = 0; k < n; ++k) {
        float depth = rnd();
        float fall = dip(lerpf(26.0f, 80.0f, depth)) * p_.fallSpeed;
        float slope = windNow_ * lerpf(0.55f, 1.25f, depth) / fall;
        float x0 = windNow_ >= 0.0f ? rnd(-ext, (float)width_) : rnd(0.0f, (float)width_ + ext);
        int best = -1;
        float bestTop = 1e30f, bx = 0.0f;
        for (size_t i = 0; i < piles_.size(); ++i) {
            const Pile& p = piles_[i];
            if (p.settle > 0.0f || p.top >= bestTop) continue;
            float x = x0 + slope * p.top;
            if (x < p.left || x >= p.right) continue;
            best = (int)i;
            bestTop = p.top;
            bx = x;
        }
        spawnedMass_ += massEach;
        if (best < 0) {
            lostMass_ += massEach;   // fällt neben den Bildschirm
            continue;
        }
        Pile& p = piles_[best];
        landedMass_ += massEach;
        float m = massEach * p.mod[cellOf(p, bx)];   // natürliche Wellen schon beim Wachsen
        lostMass_ += massEach - m;
        deposit(p, bx, m, sigma);
    }
}

// Dicke des Schneebands kurz hinter einem Ende (für die Rundung dort)
float World::bandThickness(const Pile& p, bool rightSide) const {
    float cap = std::max(capOf(p), 2.0f * cell_);
    int half = p.n / 2;
    int c0 = (int)(cap / cell_);
    int c1 = std::min(half, (int)(2.0f * cap / cell_) + 1);
    if (c1 - c0 < 2) {   // schmales Fenster: Mitte nehmen
        c0 = std::max(0, half - 2);
        c1 = std::min(p.n, half + 2);
    }
    double s = 0.0;
    int cnt = 0;
    for (int c = c0; c < c1; ++c) {
        int i = rightSide ? p.n - 1 - c : c;
        s += p.h[i];
        ++cnt;
    }
    return cnt ? (float)(s / cnt) : 0.0f;
}

// Frisch gelandeten Schnee einsinken lassen, Maximalhöhe (gewellt) und runde Enden einhalten
void World::settlePile(Pile& p, float dt) {
    if (p.empty) {
        p.settling = false;
        return;
    }
    if (p.absorb) {
        for (int i = 0; i < p.n; ++i) {
            lostMass_ += (p.h[i] + p.pend[i]) * cell_;
            p.h[i] = p.pend[i] = 0.0f;
        }
        p.empty = true;
        p.settling = false;
        return;
    }
    float capBase = capOf(p);
    float TL = 0, TR = 0, LL = 0, LR = 0;
    if (!p.floor) {
        TL = bandThickness(p, false);
        TR = bandThickness(p, true);
        LL = kNoseLength * TL + 0.5f * cell_;
        LR = kNoseLength * TR + 0.5f * cell_;
    }
    float k = 1.0f - std::exp(-dt / kSettleTau);
    bool any = false;
    p.settling = false;
    for (int i = 0; i < p.n; ++i) {
        float allowed = capBase * p.mod[i];
        int side = -1;
        if (!p.floor) {
            float dL = (i + 0.5f) * cell_, dR = (p.n - i - 0.5f) * cell_;
            if (dL < LL) {
                float a = noseProfile(TL, dL, LL);
                if (a < allowed) {
                    allowed = a;
                    side = 0;
                }
            }
            if (dR < LR) {
                float a = noseProfile(TR, dR, LR);
                if (a < allowed) {
                    allowed = a;
                    side = 1;
                }
            }
        }
        float pd = p.pend[i];
        if (pd > 0.0f) {
            if (pd > 1e-3f) p.settling = true;
            float a = pd < 1e-4f ? pd : pd * k;
            p.pend[i] = pd - a;
            // kurz vor der Maximalhöhe wächst die Decke immer langsamer (weich statt abgeschnitten)
            float r = allowed > 0.01f ? p.h[i] / allowed : 1.0f;
            float eff = 1.0f - smoothf((r - kSoftCapFrom) / (1.0f - kSoftCapFrom));
            float add = a * eff;
            p.h[i] += add;
            float rejected = (a - add) * cell_;
            if (side >= 0) {
                p.spillAcc[side] += rejected * kSpillShare;
                lostMass_ += rejected * (1.0f - kSpillShare);
            } else {
                lostMass_ += rejected;
            }
        }
        if (p.h[i] > allowed) {
            float ex = (p.h[i] - allowed) * cell_;
            p.h[i] = allowed;
            if (side >= 0) p.spillAcc[side] += ex;
            else lostMass_ += ex;
        }
        if (p.h[i] > 1e-5f || p.pend[i] > 1e-7f) any = true;
    }
    // Überstand an den Enden: ab und zu rieselt ein Bröckchen herunter
    if (!p.floor) {
        float thr = 12.0f * scale_ * scale_;
        for (int s = 0; s < 2; ++s) {
            if (p.spillAcc[s] < thr) continue;
            int edge = s == 0 ? 0 : p.n - 1;
            float x = s == 0 ? p.left + dip(1.0f) : p.right - dip(1.0f);
            float y = p.top + p.drop[edge] - p.h[edge] - dip(1.0f);
            float dir = s == 0 ? -1.0f : 1.0f;
            emitCrumb(x, y, p.spillAcc[s], dir * dip(rnd(8.0f, 20.0f)), dip(rnd(0.0f, 10.0f)), p.id);
            p.spillAcc[s] = 0.0f;
        }
    }
    p.empty = !any && p.spillAcc[0] <= 0.0f && p.spillAcc[1] <= 0.0f;
}

bool World::land(Particle& q, float prevContact) {
    float contact = q.y + q.size * 0.35f;
    if (contact < 0.0f) return false;
    int best = -1;
    float bestSurf = 1e30f;
    for (size_t k = 0; k < piles_.size(); ++k) {
        const Pile& p = piles_[k];
        if (q.x < p.left || q.x >= p.right) continue;
        if (p.settle > 0.0f || (q.ignoreId != 0 && q.ignoreId == p.id && !p.floor)) continue;
        float s = surfaceAt(p, q.x);
        if (prevContact <= s + 0.5f && contact >= s && s < bestSurf) {
            best = (int)k;
            bestSurf = s;
        }
    }
    if (best < 0) return false;
    Pile& p = piles_[best];
    landedMass_ += q.mass;
    if (q.kind == KindClump) {
        deposit(p, q.x, q.mass, std::max(dip(5.0f), q.size * 0.7f));
        if (!p.absorb) emitPuffs(q.x, bestSurf, q.mass);
        return true;
    }
    if (q.kind == KindCrumb) {
        deposit(p, q.x, q.mass, std::max(dip(4.0f), q.size * 0.6f));
        if (q.mass > 10.0f * scale_ * scale_ && !p.absorb) emitPuffs(q.x, bestSurf, q.mass * 0.4f);
        return true;
    }
    // Flocke (oder sanft abgefallener Schnee): Masse in die Decke, die Flocke selbst bleibt kurz liegen und schmilzt ein
    deposit(p, q.x, q.mass, std::max(dip(kDepositSigmaDip), q.size));
    if (!p.absorb && restingCount_ < kMaxResting) {
        Particle r = q;
        r.resting = 1;
        r.restId = p.id;
        r.mass = 0.0f;
        r.vx = r.vy = 0.0f;
        r.y = bestSurf - q.size * 0.22f;
        r.maxLife = r.life = p_.wetSnow ? rnd(0.25f, 0.5f) : rnd(0.6f, 1.4f);
        pending_.push_back(r);
        ++restingCount_;
    }
    if (p_.wetSnow && !p.floor) addWater(p, q.x, 0.06f);   // nasser Schnee wird zu Wasser
    return true;
}

// ------------------------------------------------------------------ Regen & Wasser

void World::spawnRain() {
    Particle q{};
    q.kind = KindRain;
    const float depth = rnd();   // 0 = weit weg (klein, blass, langsam), 1 = nah
    q.depth = depth;
    q.fadeIn = 1.0f;
    q.seed = rnd();
    const bool fine = p_.drizzle;
    q.vy = fine ? dip(lerpf(330.0f, 560.0f, depth)) : dip(lerpf(1050.0f, 1650.0f, depth));
    q.vx = windNow_ * (fine ? 1.6f : 4.0f) * lerpf(0.85f, 1.15f, depth);
    const float heavy = clampf(p_.rain, 0.3f, 2.5f);
    q.size = fine ? dip(lerpf(4.0f, 8.0f, depth)) : dip(lerpf(13.0f, 30.0f, depth)) * (0.85f + 0.12f * heavy);   // Länge der Spur
    q.soft = fine ? dip(lerpf(0.7f, 1.0f, depth)) : dip(lerpf(0.8f, 1.5f, depth));                            // Breite
    q.alpha = lerpf(0.14f, 0.42f, depth);
    q.rot = std::atan2(q.vx, q.vy);
    q.water = fine ? 0.08f : 0.22f;
    const float slope = q.vx / q.vy;
    const float margin = std::min(std::fabs(slope) * height_, (float)width_);
    q.x = slope >= 0.0f ? rnd(-margin, (float)width_) : rnd(0.0f, (float)width_ + margin);
    q.y = -rnd(0.0f, q.vy / 50.0f);   // über ein Bild verteilt starten (keine Streifen am oberen Rand)
    pending_.push_back(q);
}

// Regenspur oder fallender Tropfen trifft eine Kante: Spritzer, auf Fenstern sammelt sich das Wasser
bool World::landWater(Particle& q, float prevY) {
    if (q.y < 0.0f) return false;
    const bool rain = q.kind == KindRain;
    const bool nearDrop = !rain || q.depth >= 0.42f;   // ferne Regentropfen fallen hinter den Fenstern vorbei
    int best = -1;
    float bestSurf = 1e30f;
    for (size_t k = 0; k < piles_.size(); ++k) {
        const Pile& p = piles_[k];
        if (q.x < p.left || q.x >= p.right) continue;
        if (!p.floor && (!nearDrop || p.settle > 0.0f || (q.ignoreId != 0 && q.ignoreId == p.id))) continue;
        float sf = surfaceAt(p, q.x);
        if (prevY <= sf + 0.5f && q.y >= sf && sf < bestSurf) {
            best = (int)k;
            bestSurf = sf;
        }
    }
    if (best < 0) return false;
    Pile& p = piles_[best];
    const bool snowy = !p.empty && p.h[cellOf(p, q.x)] > 1.0f;
    if (!snowy) {   // in Schnee spritzt nichts
        int n = 0;
        if (rain) n = rnd() < (p_.drizzle ? 0.12f : 0.45f) ? 1 + (int)(rnd() * 2.0f) : 0;
        else n = 2 + (int)(rnd() * 2.0f);
        if (n > 0) emitSplash(q.x, bestSurf, n, rain ? (p_.drizzle ? 0.35f : 0.8f) : 1.0f);
    }
    if (!p.floor && !p.absorb && (!rain || rnd() < 0.55f)) addWater(p, q.x, rain ? q.water : q.water * 0.8f);
    return true;
}

void World::emitDrop(float x, float y, float vol, float vx, float vy, uint64_t ignoreId) {
    if (vol <= 0.02f) return;
    Particle q{};
    q.kind = KindDrop;
    q.x = x;
    q.y = y;
    q.vx = vx;
    q.vy = vy;
    q.water = vol;
    q.size = 2.0f * beadRadius(vol);
    q.soft = 1.18f;   // im Fallen leicht gestreckt
    q.alpha = 0.9f;
    q.depth = 1.0f;
    q.seed = rnd();
    q.fadeIn = 1.0f;
    q.ignoreId = ignoreId;
    pending_.push_back(q);
}

void World::emitSplash(float x, float y, int n, float strength) {
    for (int i = 0; i < n; ++i) {
        Particle q{};
        q.kind = KindSplash;
        q.x = x + rnd(-1.0f, 1.0f) * dip(1.5f);
        q.y = y - dip(0.5f);
        q.vx = rnd(-1.0f, 1.0f) * dip(60.0f) * strength;
        q.vy = -dip(rnd(50.0f, 140.0f)) * strength;
        q.size = dip(rnd(1.1f, 1.9f));
        q.alpha = rnd(0.35f, 0.6f);
        q.soft = 0.5f;
        q.depth = 1.0f;
        q.maxLife = q.life = rnd(0.16f, 0.32f);
        q.fadeIn = 1.0f;
        pending_.push_back(q);
    }
}

float World::beadRadius(float vol) const { return dip(1.2f + 2.0f * std::cbrt(std::max(vol, 0.0f))); }

int World::maxBeads(const Pile& p) const { return std::max(2, std::min(30, (int)((p.right - p.left) / dip(30.0f)))); }

float World::hangX(const Pile& p, int side, float r) const { return side == 0 ? p.left - r * 0.62f : p.right + r * 0.62f; }

float World::hangY(const Pile& p, float r, float stretch) const { return p.top + r * (0.2f + 0.6f * stretch); }

float World::edgeY(const Pile& p, float x) const { return p.top + p.drop[cellOf(p, x)]; }

void World::addWater(Pile& p, float x, float vol) {
    if (p.floor || p.absorb || vol <= 0.0f) return;
    if (!p.empty && p.h[cellOf(p, x)] > 1.0f) {   // auf Schnee: versickert, ein Teil läuft darunter zu den Ecken
        p.meltWater += vol * 0.5f;
        return;
    }
    const float endL = p.left + std::max(p.corner * 0.6f, dip(2.0f)), endR = p.right - std::max(p.corner * 0.6f, dip(2.0f));
    if (endR - endL < dip(8.0f)) return;
    x = clampf(x, endL + dip(1.0f), endR - dip(1.0f));
    const float r = beadRadius(vol);
    int best = -1, resting = 0;
    float bestD = 1e30f;
    for (size_t i = 0; i < p.beads.size(); ++i) {
        const Bead& b = p.beads[i];
        if (b.hang >= 0) continue;
        ++resting;
        float d = std::fabs(b.x - x);
        if (d < beadRadius(b.vol) + r && d < bestD) {
            best = (int)i;
            bestD = d;
        }
    }
    if (best < 0 && resting >= maxBeads(p)) {   // voll: in den nächsten Tropfen
        for (size_t i = 0; i < p.beads.size(); ++i) {
            const Bead& b = p.beads[i];
            if (b.hang >= 0) continue;
            float d = std::fabs(b.x - x);
            if (best < 0 || d < bestD) {
                best = (int)i;
                bestD = d;
            }
        }
    }
    if (best >= 0) {
        Bead& b = p.beads[best];
        float nv = b.vol + vol;
        if (b.vx == 0.0f) b.x = (b.x * b.vol + x * vol) / nv;
        b.vol = nv;
    } else {
        p.beads.push_back(Bead{x, vol, 0.0f, rnd(), -1});
    }
}

void World::addHanging(Pile& p, int side, float vol) {
    if (p.floor || p.absorb || vol <= 0.0f) return;
    for (Bead& b : p.beads) {
        if (b.hang == side) {
            b.vol += vol;
            return;
        }
    }
    p.beads.push_back(Bead{side == 0 ? p.left : p.right, vol, 0.0f, rnd(), side});
}

// Tropfen auf einer Kante: verdunsten (ohne Regen), große rollen zur nächsten Ecke und sammeln unterwegs ein,
// an der Ecke hängen sie, wachsen und tropfen ab
void World::stepBeads(Pile& p, float dt, bool raining) {
    if (p.floor) return;
    if (p.meltWater > 0.25f) {   // Schmelzwasser kommt an einer Ecke an
        addHanging(p, rnd() < 0.5f ? 0 : 1, p.meltWater);
        p.meltWater = 0.0f;
    }
    if (p.beads.empty()) return;
    const float endL = p.left + std::max(p.corner * 0.6f, dip(2.0f)), endR = p.right - std::max(p.corner * 0.6f, dip(2.0f));
    const float evap = raining ? 0.0f : kEvapPerSec * dt;
    float arrive[2] = {0.0f, 0.0f};
    for (Bead& b : p.beads) {
        b.vol -= evap;
        if (b.vol <= 0.0f) continue;
        if (b.hang >= 0) {
            const float thr = 1.4f + 1.4f * b.seed;   // so groß wird der Tropfen, bevor er abreißt
            if (b.vol >= thr) {
                const float r = beadRadius(b.vol), st = 1.0f + 0.22f * std::min(b.vol, 2.5f);
                emitDrop(hangX(p, b.hang, r), hangY(p, r, st) + r * 0.3f, b.vol * 0.85f, 0.0f, dip(15.0f), p.id);
                b.vol *= 0.15f;   // ein kleiner Rest bleibt hängen
                b.seed = rnd();
            }
            continue;
        }
        if (!p.empty && p.h[cellOf(p, b.x)] > 1.0f) {   // eingeschneit: versickert
            p.meltWater += b.vol * 0.5f;
            b.vol = 0.0f;
            continue;
        }
        if (b.vx == 0.0f && b.vol >= kSlideVol + 0.8f * b.seed) {   // jeder Tropfen hält etwas anders
            float dir = (b.x - p.left) < (p.right - b.x) ? -1.0f : 1.0f;   // zur näheren Ecke
            if (std::fabs(windNow_) > dip(35.0f)) dir = windNow_ > 0.0f ? 1.0f : -1.0f;   // bei kräftigem Wind mit dem Wind
            b.vx = dir * dip(4.0f);
        }
        if (b.vx != 0.0f) {
            const float dir = b.vx > 0.0f ? 1.0f : -1.0f;
            const float a = dip(26.0f) * (0.5f + 0.35f * std::min(b.vol, 3.0f));
            b.vx = clampf(b.vx + dir * a * dt, -dip(60.0f), dip(60.0f));
            b.x += b.vx * dt;
        }
        const float r = beadRadius(b.vol);
        if (b.x - r * 0.5f <= endL || b.x + r * 0.5f >= endR) {
            arrive[b.x - r * 0.5f <= endL ? 0 : 1] += b.vol;
            b.vol = 0.0f;
        }
    }
    // leere entfernen, dann Nachbarn, die sich berühren, zusammenfließen lassen
    size_t w = 0;
    for (size_t i = 0; i < p.beads.size(); ++i)
        if (p.beads[i].vol > 0.03f) p.beads[w++] = p.beads[i];
    p.beads.resize(w);
    std::sort(p.beads.begin(), p.beads.end(), [](const Bead& a, const Bead& b) { return a.hang != b.hang ? a.hang < b.hang : a.x < b.x; });
    for (size_t i = 0; i + 1 < p.beads.size();) {
        Bead& a = p.beads[i];
        Bead& b = p.beads[i + 1];
        if (a.hang == -1 && b.hang == -1 && b.x - a.x < beadRadius(a.vol) + beadRadius(b.vol)) {
            float nv = a.vol + b.vol;
            a.x = (a.x * a.vol + b.x * b.vol) / nv;
            a.vx = (a.vx * a.vol + b.vx * b.vol) / nv;
            if (a.vx == 0.0f && (b.vx != 0.0f)) a.vx = b.vx;
            a.vol = nv;
            p.beads.erase(p.beads.begin() + (long)(i + 1));
        } else {
            ++i;
        }
    }
    for (int side = 0; side < 2; ++side)
        if (arrive[side] > 0.0f) addHanging(p, side, arrive[side]);
}

// Fenster verschoben/geschlossen: Tropfen in diesem Bereich fallen herunter
void World::releaseBeads(Pile& p, float x0, float x1) {
    for (size_t i = 0; i < p.beads.size();) {
        const Bead& b = p.beads[i];
        if (b.x >= x0 && b.x < x1) {
            const float r = beadRadius(b.vol);
            if (b.hang >= 0) emitDrop(hangX(p, b.hang, r), hangY(p, r, 1.0f), b.vol, 0.0f, dip(rnd(0.0f, 20.0f)), p.id);
            else emitDrop(b.x, edgeY(p, b.x) - r * 0.8f, b.vol, b.vx * 0.5f, dip(rnd(0.0f, 20.0f)), p.id);
            p.beads[i] = p.beads.back();
            p.beads.pop_back();
        } else {
            ++i;
        }
    }
    if (x0 <= p.left && x1 >= p.right) p.meltWater = 0.0f;
}

void World::meltPile(Pile& p, float dt, float ratePx) {
    if (p.empty || p.absorb) return;
    const float d = ratePx * dt;
    float melted = 0.0f;
    for (int i = 0; i < p.n; ++i) {
        float v = p.h[i];
        if (v <= 0.0f) continue;
        float nv = std::max(0.0f, v - d);
        melted += (v - nv) * cell_;
        p.h[i] = nv;
    }
    lostMass_ += melted;
    if (!p.floor) p.meltWater += melted / (scale_ * scale_ * kMeltDip2PerVol);
}

void World::stepFog(float dt) {
    const float target = (snowing_ && !thawing_) ? p_.fog : 0.0f;
    fogLevel_ += (target - fogLevel_) * (1.0f - std::exp(-dt / 4.0f));
    if (fogLevel_ < 0.002f && target <= 0.0f) {
        fogLevel_ = 0.0f;
        fog_.clear();
        return;
    }
    if (fog_.empty()) {
        // breite, flache Schwaden, viele überlappend: gleichmäßiger Dunst statt einzelner Flecken, unten dichter
        int n = 6 + (int)(width_ / dip(170.0f));
        for (int i = 0; i < n; ++i) {
            Particle q{};
            q.kind = KindFog;
            q.size = dip(rnd(520.0f, 1050.0f));
            q.soft = rnd(0.32f, 0.5f);   // Höhe im Verhältnis zur Breite
            q.x = rnd(-q.size * 0.3f, width_ + q.size * 0.3f);
            float u = rnd();
            q.y = height_ * (0.25f + 0.85f * std::sqrt(u));   // eher unten
            q.alpha = rnd(0.07f, 0.13f);
            q.vx = dip(rnd(-4.0f, 4.0f));
            q.swayFreq = rnd(0.05f, 0.12f);
            q.swayPhase = rnd(0.0f, 2.0f * kPi);
            q.seed = rnd();
            fog_.push_back(q);
        }
    }
    for (Particle& q : fog_) {
        q.x += (q.vx + windNow_ * 0.3f) * dt;
        const float half = q.size * 0.5f;
        if (q.x - half > width_) q.x = -half;
        else if (q.x + half < 0.0f) q.x = width_ + half;
    }
}

float World::flashAlpha() const {
    if (flashT_ < 0.0f) return 0.0f;
    const float t = flashT_;
    float a;   // drei Zuckungen und ein Nachglühen
    if (t < 0.07f) a = 0.20f;
    else if (t < 0.14f) a = 0.03f;
    else if (t < 0.19f) a = 0.13f;
    else if (t < 0.30f) a = 0.02f;
    else if (t < 0.36f) a = 0.24f;
    else a = 0.24f * std::exp(-(t - 0.36f) / 0.12f);
    return a * thawAlpha_;
}

void World::flashNow() { flashT_ = 0.0f; }

float World::frameRateHint() const {
    if (thawing_ || !parts_.empty() || !pending_.empty() || spawnRate() > 0.0f || rainRate() > 0.0f || flashT_ >= 0.0f) return -1.0f;
    float hz = 0.0f;
    if (fogLevel_ > 0.002f || (snowing_ && p_.fog > 0.0f)) hz = 12.0f;
    if (p_.thunder && snowing_) hz = std::max(hz, 2.0f);   // der nächste Blitz kommt bestimmt
    const bool melting = p_.meltDipPerMin > 0.0f;
    for (const Pile& p : piles_) {
        if (p.settling) hz = std::max(hz, 15.0f);
        if (!p.empty && melting) hz = std::max(hz, 4.0f);
        if (p.meltWater > 0.0f) hz = std::max(hz, 4.0f);
        for (const Bead& b : p.beads) hz = std::max(hz, b.vx != 0.0f ? 24.0f : 2.0f);   // rollt bzw. verdunstet
    }
    return hz;
}

int World::beadCount(uint64_t id) const {
    for (const Pile& p : piles_)
        if (p.id == id) return (int)p.beads.size();
    return 0;
}

float World::beadWater(uint64_t id) const {
    for (const Pile& p : piles_) {
        if (p.id != id) continue;
        float v = 0.0f;
        for (const Bead& b : p.beads) v += b.vol;
        return v;
    }
    return 0.0f;
}

void World::addWaterForTest(uint64_t id, float x, float vol) {
    for (Pile& p : piles_)
        if (p.id == id) addWater(p, x, vol);
}

// ------------------------------------------------------------------ Ablauf

void World::updateWind(float dt) {
    float base = p_.wind * dip(70.0f);   // Vorzeichen = Richtung
    float slow = 0.78f + 0.22f * std::sin(time_ * 0.071f + windPhase1_) + 0.12f * std::sin(time_ * 0.19f + windPhase2_);
    gustTimer_ -= dt;
    if (gustTimer_ <= 0.0f) {
        gustTimer_ = rnd(8.0f, 26.0f) / (0.5f + std::fabs(p_.wind));
        if (std::fabs(p_.wind) > 0.01f) {
            gustTarget_ = base * rnd(0.6f, 1.5f) + (p_.wind < 0 ? -dip(8.0f) : dip(8.0f));
            gustPhase_ = 0.0f;
        }
    }
    if (gustTarget_ != 0.0f) {
        gustPhase_ += dt;
        float g;
        if (gustPhase_ < 1.5f) g = smoothf(gustPhase_ / 1.5f);
        else if (gustPhase_ < 2.5f) g = 1.0f;
        else if (gustPhase_ < 5.5f) g = 1.0f - smoothf((gustPhase_ - 2.5f) / 3.0f);
        else {
            g = 0.0f;
            gustTarget_ = 0.0f;
        }
        gust_ = gustTarget_ * g;
    } else {
        gust_ = 0.0f;
    }
    windNow_ = base * slow + gust_;
}

void World::setCursor(bool inside, float x, float y) {
    cursorIn_ = inside;
    cx_ = x;
    cy_ = y;
}

void World::step(float dt) {
    if (dt <= 0.0f || width_ <= 0 || height_ <= 0) return;
    // fällt nichts, reichen wenige Bilder pro Sekunde: dann auch größere Schritte (Tauen, Nebel, rollende Tropfen)
    const bool falling = !parts_.empty() || !pending_.empty() || spawnRate() > 0.0f || rainRate() > 0.0f;
    if (dt > (falling ? 0.1f : 0.5f)) dt = falling ? 0.1f : 0.5f;
    time_ += dt;
    updateWind(dt);

    // Mausgeschwindigkeit (für das Wegpusten)
    if (cursorIn_ && cursorValid_) {
        float vx = (cx_ - pcx_) / dt, vy = (cy_ - pcy_) / dt;
        cvx_ = lerpf(cvx_, vx, 0.5f);
        cvy_ = lerpf(cvy_, vy, 0.5f);
    } else {
        cvx_ = cvy_ = 0.0f;
    }
    cursorValid_ = cursorIn_;
    pcx_ = cx_;
    pcy_ = cy_;

    for (Pile& p : piles_) {
        if (p.settle > 0.0f) p.settle = std::max(0.0f, p.settle - dt);
    }

    // neue Flocken + unsichtbare Schnee-Portionen
    float rate = spawnRate();
    if (rate > 0.0f) {
        spawnAcc_ += rate * dt;
        int n = (int)spawnAcc_;
        spawnAcc_ -= n;
        for (int i = 0; i < n; ++i) spawnFlake(-dip(8.0f), false);
        depositVirtual(dt);
    }
    float rr = rainRate();
    if (rr > 0.0f) {
        rainAcc_ += rr * dt;
        int n = (int)rainAcc_;
        rainAcc_ -= n;
        for (int i = 0; i < n; ++i) spawnRain();
    }

    // Abtauen
    float thawAlpha = 1.0f;
    if (thawing_) {
        thawLeft_ -= dt;
        float k = std::exp(-dt / 0.45f);
        float d = dip(8.0f) * dt;
        for (Pile& p : piles_) {
            for (float& v : p.h) {
                float nv = std::max(0.0f, v * k - d);
                lostMass_ += (v - nv) * cell_;
                v = nv;
            }
            for (Bead& bd : p.beads) bd.vol *= k;
        }
        thawAlpha = clampf(thawLeft_ / 1.2f, 0.0f, 1.0f);
        if (thawLeft_ <= 0.0f) {
            clear();
            thawing_ = false;
            snowing_ = false;
            return;
        }
    }
    thawAlpha_ = thawAlpha;

    // Tauwetter: liegender Schnee schmilzt, auf Fenstern läuft das Wasser zu den Ecken
    if (!thawing_ && p_.meltDipPerMin > 0.0f) {
        float rate = dip(p_.meltDipPerMin) / 60.0f;
        for (Pile& p : piles_) meltPile(p, dt, rate);
    }

    const float decay = std::exp(-dt / 0.6f);
    const float blowR = dip(80.0f);
    const float maxImpulse = dip(400.0f);
    const float terminal = dip(1100.0f);
    const float sideMargin = (float)width_ * 0.6f + dip(80.0f);

    int resting = 0;
    for (size_t i = 0; i < parts_.size();) {
        Particle& q = parts_[i];
        bool remove = false;

        if (q.resting) {
            q.life -= dt;
            if (q.life <= 0.0f) remove = true;
            else ++resting;
        } else {
            float prevContact = q.y + q.size * 0.35f;
            if (q.kind == KindSoft || q.kind == KindCrystal) {
                float w = windNow_ * lerpf(0.55f, 1.25f, q.depth);
                float sway = q.swayAmp * std::cos(time_ * q.swayFreq + q.swayPhase);
                q.vx *= decay;
                q.vy *= decay;
                if (p_.mouseBlow && cursorIn_) {
                    float dx = q.x - cx_, dy = q.y - cy_;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < blowR * blowR && d2 > 0.25f) {
                        float d = std::sqrt(d2);
                        float kk = 1.0f - d / blowR;
                        kk *= kk;
                        q.vx += (dx / d * dip(900.0f) * kk + cvx_ * kk * 2.5f) * dt;
                        q.vy += (dy / d * dip(900.0f) * kk + cvy_ * kk * 2.5f) * dt;
                        q.vx = clampf(q.vx, -maxImpulse, maxImpulse);
                        q.vy = clampf(q.vy, -maxImpulse, maxImpulse);
                    }
                }
                q.x += (w + sway + q.vx) * dt;
                q.y += (q.fall + q.vy) * dt;
                if (q.kind == KindCrystal) q.rot += q.rotSpeed * dt;
                if (q.fadeIn < 1.0f) q.fadeIn = std::min(1.0f, q.fadeIn + dt / 1.5f);
            } else if (q.kind == KindRain) {
                float py = q.y;
                q.x += q.vx * dt;
                q.y += q.vy * dt;
                if (landWater(q, py)) remove = true;
            } else if (q.kind == KindDrop) {
                float py = q.y;
                q.vy = std::min(dip(1300.0f), q.vy + gravity_ * dt);
                q.vx *= std::exp(-dt / 0.8f);
                q.x += q.vx * dt;
                q.y += q.vy * dt;
                if (landWater(q, py)) remove = true;
            } else if (q.kind == KindSplash) {
                q.vy += gravity_ * dt;
                q.x += q.vx * dt;
                q.y += q.vy * dt;
                q.life -= dt;
                if (q.life <= 0.0f) remove = true;
            } else if (q.kind == KindClump || q.kind == KindCrumb) {
                float move = dt;
                if (q.life > 0.0f) {   // hängt noch einen Augenblick in der Luft, dann löst es sich
                    move = std::max(0.0f, dt - q.life);
                    q.life = std::max(0.0f, q.life - dt);
                }
                q.vy = std::min(terminal, q.vy + gravity_ * move);
                float targetVx = windNow_ * 0.25f;
                q.vx += (targetVx - q.vx) * (1.0f - std::exp(-move / 1.2f));
                q.x += q.vx * move;
                q.y += q.vy * move;
                q.rot += q.rotSpeed * move;
            } else {  // Puff
                float dec = std::exp(-dt / 0.22f);
                q.vx *= dec;
                q.vy = q.vy * dec + dip(140.0f) * dt;
                q.x += q.vx * dt;
                q.y += q.vy * dt;
                q.life -= dt;
                if (q.life <= 0.0f) remove = true;
            }

            const bool snowy = q.kind == KindSoft || q.kind == KindCrystal || q.kind == KindClump || q.kind == KindCrumb;
            if (!remove && snowy && land(q, prevContact)) remove = true;
            if (!remove && (q.y - q.size > height_ + dip(20.0f) || q.x < -sideMargin || q.x > width_ + sideMargin)) {
                lostMass_ += q.mass;
                remove = true;
            }
        }
        if (remove) {
            parts_[i] = parts_.back();
            parts_.pop_back();
        } else {
            ++i;
        }
    }
    // land() hat neue liegende Flocken mitgezählt (in pending_)
    int newResting = 0;
    for (const Particle& q : pending_) newResting += q.resting ? 1 : 0;
    restingCount_ = resting + newResting;
    flushPending();

    for (Pile& p : piles_) settlePile(p, dt);
    flushPending();

    // Wasser auf den Kanten, Nebel, Gewitter
    const bool raining = rainRate() > 0.0f;
    for (Pile& p : piles_) stepBeads(p, dt, raining);
    flushPending();
    stepFog(dt);
    if (flashT_ >= 0.0f) {
        flashT_ += dt;
        if (flashT_ > 0.9f) flashT_ = -1.0f;
    }
    if (p_.thunder && snowing_ && !thawing_) {
        flashNext_ -= dt;
        if (flashNext_ <= 0.0f) {
            flashT_ = 0.0f;
            flashNext_ = rnd(14.0f, 50.0f);
        }
    }
    if (active()) ++version_;
}

void World::shakeAll() {
    for (Pile& p : piles_) {
        if (p.floor || p.absorb) continue;
        releaseCells(p, 0, p.n, 0.0f);
        releaseBeads(p, p.left - 1.0f, p.right + 1.0f);
        lostMass_ += p.spillAcc[0] + p.spillAcc[1];
        p.spillAcc[0] = p.spillAcc[1] = 0.0f;
    }
    flushPending();
    ++version_;
}

void World::startThaw() {
    if (thawing_) return;
    thawing_ = true;
    thawLeft_ = 2.0f;
    for (Pile& p : piles_) {
        for (float& v : p.pend) {
            lostMass_ += v * cell_;
            v = 0.0f;
        }
    }
}

void World::clear() {
    for (const Particle& q : parts_) lostMass_ += q.mass;
    for (const Particle& q : pending_) lostMass_ += q.mass;
    for (Pile& p : piles_) {
        for (int i = 0; i < p.n; ++i) {
            lostMass_ += (p.h[i] + p.pend[i]) * cell_;
            p.h[i] = p.pend[i] = 0.0f;
        }
        lostMass_ += p.spillAcc[0] + p.spillAcc[1];
        p.spillAcc[0] = p.spillAcc[1] = 0.0f;
        p.empty = true;
        p.settling = false;
        p.beads.clear();
        p.meltWater = 0.0f;
    }
    parts_.clear();
    pending_.clear();
    restingCount_ = 0;
    thawAlpha_ = 1.0f;
    fog_.clear();
    fogLevel_ = 0.0f;
    flashT_ = -1.0f;
    ++version_;
}

void World::resume() {
    thawing_ = false;
    thawAlpha_ = 1.0f;
    snowing_ = true;
}

bool World::active() const {
    if (thawing_ || !parts_.empty() || !pending_.empty() || spawnRate() > 0.0f || rainRate() > 0.0f) return true;
    if (fogLevel_ > 0.002f || (snowing_ && p_.fog > 0.0f) || flashT_ >= 0.0f) return true;
    for (const Pile& p : piles_) {
        if (!p.empty || !p.beads.empty() || p.meltWater > 0.0f) return true;
    }
    return false;
}

// ------------------------------------------------------------------ Ausgabe

void World::collectSprites(std::vector<Sprite>& out) const {
    out.clear();
    out.reserve(parts_.size() + fog_.size() + 16);
    // Nebel ganz hinten
    for (const Particle& q : fog_) {
        float a = q.alpha * fogLevel_ * thawAlpha_ * (0.85f + 0.15f * std::sin(time_ * q.swayFreq + q.swayPhase));
        if (a < 0.004f) continue;
        out.push_back(Sprite{q.x, q.y, q.size, a, 0.0f, q.soft, q.seed, (uint32_t)KindFog});
    }
    for (const Particle& q : parts_) {
        Sprite s;
        s.x = q.x;
        s.y = q.y;
        s.size = q.size;
        s.alpha = q.alpha * q.fadeIn * thawAlpha_;
        s.rot = q.rot;
        s.soft = q.soft;
        s.seed = q.seed;
        s.kind = q.kind;
        if (q.resting) {
            // liegt auf der Schneedecke und schmilzt ein: wird blasser, kleiner und sinkt ein Stück ein
            float t = q.maxLife > 0 ? clampf(q.life / q.maxLife, 0.0f, 1.0f) : 0.0f;
            s.alpha *= std::pow(t, 0.8f);
            s.size *= 0.7f + 0.3f * t;
            s.y += (1.0f - t) * q.size * 0.3f;
        } else if (q.kind == KindPuff) {
            float t = q.maxLife > 0 ? clampf(q.life / q.maxLife, 0.0f, 1.0f) : 0.0f;
            s.alpha *= std::pow(t, 1.5f);
            s.size *= 1.0f + 0.9f * (1.0f - t);
        } else if (q.kind == KindSplash) {
            float t = q.maxLife > 0 ? clampf(q.life / q.maxLife, 0.0f, 1.0f) : 0.0f;
            s.alpha *= t;
        }
        if (s.alpha < 0.004f) continue;
        if (s.x + s.size < 0 || s.x - s.size > width_ || s.y + s.size < 0 || s.y - s.size > height_) continue;
        out.push_back(s);
    }
    // Tropfen auf den Kanten (liegend leicht abgeflacht, an den Ecken hängend und gestreckt)
    for (const Pile& p : piles_) {
        for (const Bead& b : p.beads) {
            const float r = beadRadius(b.vol);
            Sprite s;
            s.x = b.x;
            s.size = 2.0f * r;
            s.alpha = 0.92f * thawAlpha_;
            s.rot = 0.0f;
            s.seed = b.seed;
            s.kind = KindDrop;
            if (b.hang >= 0) {
                s.soft = 1.0f + 0.22f * std::min(b.vol, 2.5f);
                s.x = hangX(p, b.hang, r);
                s.y = hangY(p, r, s.soft);
            } else {
                s.soft = 0.84f;
                s.y = edgeY(p, b.x) - r * 0.8f;
            }
            if (s.alpha >= 0.004f) out.push_back(s);
        }
    }
    float fa = flashAlpha();
    if (fa > 0.003f) out.push_back(Sprite{width_ * 0.5f, height_ * 0.5f, 2.0f * (float)std::max(width_, height_), fa, 0.0f, 0.0f, 0.0f, (uint32_t)KindFlash});
}

void World::buildPileMesh(std::vector<PileVertex>& out) const {
    out.clear();
    std::vector<float> hc, xs, dr, hs;
    std::vector<PileVertex> tops, bottoms;
    for (const Pile& p : piles_) {
        if (p.absorb || p.empty) continue;
        bool any = false;
        for (float v : p.h) {
            if (v > 0.05f) {
                any = true;
                break;
            }
        }
        if (!any) continue;
        int k = std::max(1, (int)std::round(3.0f * scale_ / cell_));   // Darstellungsspalten ≈ 3 DIP
        int m = (p.n + k - 1) / k;
        hc.assign(m, 0.0f);
        dr.assign(m, 0.0f);
        xs.assign(m, 0.0f);
        for (int c = 0; c < m; ++c) {
            int a = c * k, b = std::min(p.n, a + k);
            float sh = 0, sd = 0;
            for (int i = a; i < b; ++i) {
                sh += p.h[i];
                sd += p.drop[i];
            }
            hc[c] = sh / (b - a);
            dr[c] = sd / (b - a);
            xs[c] = p.left + (a + b) * 0.5f * cell_;
        }
        // leicht glätten (±2 Spalten): weiche Oberfläche, die runden Enden bleiben erhalten
        static const float kw[3] = {0.40f, 0.24f, 0.06f};
        hs.assign(m, 0.0f);
        for (int c = 0; c < m; ++c) {
            float acc = 0, wsum = 0;
            for (int o = -2; o <= 2; ++o) {
                int cc = c + o;
                if (cc < 0 || cc >= m) continue;
                float wgt = kw[o < 0 ? -o : o];
                acc += hc[cc] * wgt;
                wsum += wgt;
            }
            hs[c] = acc / wsum;
        }
        // Oberkante 1 px höher mit dist = -1: der Shader blendet dort weich aus (Kantenglättung)
        auto vertexAt = [&](float x, float h, float d, PileVertex& top, PileVertex& bottom) {
            float floorY = p.top + d;
            top = PileVertex{x, floorY - h - 1.0f, -1.0f, h};
            bottom = PileVertex{x, floorY, h, h};
        };
        tops.clear();
        bottoms.clear();
        PileVertex t, b;
        float endL = p.floor ? hs[0] : std::min(hs[0], p.h[0] + 0.5f);
        float endR = p.floor ? hs[m - 1] : std::min(hs[m - 1], p.h[p.n - 1] + 0.5f);
        vertexAt(p.left, endL, p.drop[0], t, b);
        tops.push_back(t);
        bottoms.push_back(b);
        for (int c = 0; c < m; ++c) {
            float x = std::min(xs[c], p.right);
            vertexAt(x, hs[c], dr[c], t, b);
            tops.push_back(t);
            bottoms.push_back(b);
        }
        vertexAt(p.right, endR, p.drop[p.n - 1], t, b);
        tops.push_back(t);
        bottoms.push_back(b);
        for (size_t c = 0; c + 1 < tops.size(); ++c) {
            if (tops[c].height <= 0.05f && tops[c + 1].height <= 0.05f) continue;
            if (tops[c + 1].x <= tops[c].x) continue;
            out.push_back(tops[c]);
            out.push_back(bottoms[c]);
            out.push_back(tops[c + 1]);
            out.push_back(tops[c + 1]);
            out.push_back(bottoms[c]);
            out.push_back(bottoms[c + 1]);
        }
    }
}

Stats World::stats() const {
    Stats s;
    for (const Particle& q : parts_) {
        if (q.resting) {
            s.resting++;
        } else {
            switch (q.kind) {
                case KindSoft: s.flakes++; break;
                case KindCrystal: s.crystals++; break;
                case KindClump: s.clumps++; break;
                case KindCrumb: s.crumbs++; break;
                case KindRain: s.rain++; break;
                case KindDrop: s.drops++; break;
                case KindSplash: s.splashes++; break;
                default: s.puffs++; break;
            }
        }
        s.airMass += q.mass;
    }
    for (const Particle& q : pending_) s.airMass += q.mass;
    s.fog = (int)fog_.size();
    for (const Pile& p : piles_) {
        for (const Bead& b : p.beads) {
            if (b.hang >= 0) s.hanging++;
            else s.beads++;
            s.water += b.vol;
        }
        s.piles++;
        for (int i = 0; i < p.n; ++i) s.pileMass += (p.h[i] + p.pend[i]) * cell_;
        s.pileMass += p.spillAcc[0] + p.spillAcc[1];
    }
    s.spawnedMass = spawnedMass_;
    s.lostMass = lostMass_;
    s.landedMass = landedMass_;
    return s;
}

float World::pileElevation(uint64_t id, float x) const {
    for (const Pile& p : piles_) {
        if (p.id != id) continue;
        if (x < p.left || x >= p.right) return -1.0f;
        int i = cellOf(p, x);
        return p.h[i] - p.drop[i];
    }
    return -1.0f;
}

float World::pileHeightAt(uint64_t id, float x) const {
    for (const Pile& p : piles_) {
        if (p.id != id) continue;
        if (x < p.left || x >= p.right) return -1.0f;
        return p.h[cellOf(p, x)];
    }
    return -1.0f;
}

bool World::hasPile(uint64_t id) const {
    for (const Pile& p : piles_) {
        if (p.id == id) return true;
    }
    return false;
}

}  // namespace snow
