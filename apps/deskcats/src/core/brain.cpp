// DeskCats – Verhalten (siehe brain.h)
#include "brain.h"

#include <algorithm>
#include <cmath>

namespace cat {

namespace {

constexpr double kDt = 1.0 / kFps;
constexpr double kGravity = 1400.0;   // Sprite-Pixel/s² (etwas kräftiger als echt, wirkt flotter)

template <class T>
T clampv(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

Posture nextStep(Posture a, Posture t) {
    if (a == t) return t;
    switch (a) {
        case Posture::Stand: return t == Posture::Sit ? Posture::Sit : Posture::Loaf;
        case Posture::Sit: return t == Posture::Stand ? Posture::Stand : Posture::Loaf;
        case Posture::Loaf: return t == Posture::Sleep ? Posture::Sleep : (t == Posture::Sit ? Posture::Sit : Posture::Stand);
        default: return Posture::Loaf;
    }
}

}  // namespace

Personality personalityFatos() {
    Personality p;
    p.idleMin = 7.f;
    p.idleMax = 22.f;
    p.sleepMin = 150.f;
    p.sleepMax = 600.f;
    p.energyUse = 0.07f;
    p.wanderOther = 0.35f;
    p.runChance = 0.12f;
    p.playful = 0.35f;
    p.groomChance = 0.3f;
    p.dangleChance = 0.6f;
    p.reachUp = 175;
    p.reachDown = 420;
    p.reachSide = 170;
    p.halfLen = 26;
    p.headAhead = 17;
    p.callSide = -1;
    return p;
}

Personality personalityKosta() {
    Personality p;
    p.idleMin = 3.f;
    p.idleMax = 11.f;
    p.sleepMin = 80.f;
    p.sleepMax = 320.f;
    p.energyUse = 0.08f;
    p.wanderOther = 0.6f;
    p.runChance = 0.45f;
    p.playful = 0.85f;
    p.groomChance = 0.22f;
    p.dangleChance = 0.25f;
    p.reachUp = 235;
    p.reachDown = 480;
    p.reachSide = 230;
    p.halfLen = 23;
    p.headAhead = 15;
    p.callSide = 1;
    return p;
}

// ------------------------------------------------------------------ Gegenstände

bool stepItem(Item& it, const World& w, double dt) {
    if (!it.active) return false;
    const int sc = w.scaleAt((int)it.x, (int)it.y - 4);
    const double g = kGravity * sc;
    bool moved = false;
    if (!it.airborne) {
        const Platform* p = it.platform ? w.find(it.platform) : nullptr;
        if (!p) {
            it.airborne = true;
            it.platform = 0;
            it.vy = 0;
        } else {
            if (it.y != p->y) moved = true;
            it.y = p->y;
            if (std::fabs(it.vx) > 4.0) {
                it.x += it.vx * dt;
                it.spin += (float)(it.vx * dt / (3.0 * sc));
                it.vx *= std::pow(0.3, dt);
                moved = true;
            } else {
                it.vx = 0;
            }
            if (it.x < p->x0 || it.x > p->x1) {
                it.airborne = true;
                it.platform = 0;
                it.vy = -20.0 * sc;
            }
        }
    }
    if (it.airborne) {
        it.vy += g * dt;
        double ny = it.y + it.vy * dt;
        it.x += it.vx * dt;
        it.spin += (float)(it.vx * dt / (3.0 * sc));
        const Platform* L = it.vy > 0 ? w.landingBelow(it.x, it.y, ny) : nullptr;
        if (L) {
            it.y = L->y;
            it.platform = L->id;
            if (it.vy > 260.0 * sc && it.kind == 1) {
                it.vy = -it.vy * 0.33;   // Knäuel hüpft
                it.vx *= 0.7;
            } else {
                it.airborne = false;
                it.vy = 0;
            }
        } else {
            it.y = ny;
            // nie aus allen Monitoren fallen: auf die nächste Bodenfläche legen
            if (!w.monitors.empty()) {
                int bottom = w.monitors[0].y1;
                for (const Monitor& m : w.monitors) bottom = std::max(bottom, m.y1);
                if (it.y > bottom) {
                    const Platform* f = nullptr;
                    for (const Platform& p : w.plats)
                        if (p.kind != PlatKind::Window && (!f || std::fabs((p.x0 + p.x1) / 2.0 - it.x) < std::fabs((f->x0 + f->x1) / 2.0 - it.x))) f = &p;
                    if (f) {
                        it.x = std::min(std::max(it.x, f->x0 + 12.0), f->x1 - 12.0);
                        it.y = f->y;
                        it.platform = f->id;
                    }
                    it.airborne = false;
                    it.vx = it.vy = 0;
                }
            }
        }
        moved = true;
    }
    // am Bildschirmrand abprallen (nicht in unsichtbare Bereiche rollen)
    if (!w.monitors.empty() && moved) {
        int m = w.monitorAt((int)it.x, (int)it.y - 4);
        if (m < 0) {
            int left = w.monitors[0].x0, right = w.monitors[0].x1;
            for (const Monitor& mo : w.monitors) {
                left = std::min(left, mo.x0);
                right = std::max(right, mo.x1);
            }
            if (it.x < left + 6) {
                it.x = left + 6;
                it.vx = std::fabs(it.vx) * 0.5;
            } else if (it.x > right - 6) {
                it.x = right - 6;
                it.vx = -std::fabs(it.vx) * 0.5;
            }
        } else {
            const Monitor& mo = w.monitors[m];
            if (it.x < mo.x0 + 6 && it.vx < 0) it.vx = -it.vx * 0.5;
            if (it.x > mo.x1 - 6 && it.vx > 0) it.vx = -it.vx * 0.5;
        }
    }
    return moved;
}

// ------------------------------------------------------------------ Grundlagen

CatBrain::CatBrain(const Personality& p, uint32_t seed) : P_(p), rng_(seed) {}

int CatBrain::scale(const Senses& s) const { return s.world ? s.world->scaleAt((int)x_, (int)y_ - 8) : 3; }

void CatBrain::setPlatform(const Platform& p) {
    plat_ = p.id;
    platX0_ = p.x0;
    platW_ = p.width();
    platY_ = p.y;
}

void CatBrain::place(const World& w, const Platform& plat, double x, Posture posture) {
    (void)w;
    x_ = x;
    y_ = plat.y;
    setPlatform(plat);
    posture_ = posture;
    mode_ = Mode::Idle;
    frame_ = 0;
    dangle_ = false;
    lastTick_ = -1;
    nextDecision_ = 0;
}

bool CatBrain::sleeping() const { return mode_ == Mode::Idle && posture_ == Posture::Sleep; }
bool CatBrain::busyWithItem() const { return mode_ == Mode::Eat || mode_ == Mode::Play || goal_ == Goal::Bowl || goal_ == Goal::Toy; }

bool CatBrain::takeBat(double& vx, double& vy) {
    if (!batPending_) return false;
    batPending_ = false;
    vx = batVx_;
    vy = batVy_;
    return true;
}

// ------------------------------------------------------------------ Fläche mitverfolgen

void CatBrain::followPlatform(const Senses& s) {
    if (plat_ == 0 || mode_ == Mode::Air || mode_ == Mode::Carried) return;
    const Platform* p = s.world->find(plat_);
    const int sc = scale(s);
    if (!p) {   // Fenster zu, minimiert, maximiert ... – oder dieselbe Kante kommt unter neuer ID wieder
        const Platform* q = nullptr;   // (z. B. Boden wird zur Taskleiste, wenn "Auf der Taskleiste" eingeschaltet wird)
        for (const Platform& o : s.world->plats)
            if (std::abs(o.y - y_) <= 3 && x_ >= o.x0 && x_ <= o.x1 && (!q || o.z < q->z)) q = &o;
        if (q) {
            setPlatform(*q);
            y_ = q->y;
        } else {
            startFall(0, 0);
        }
        return;
    }
    int dx = p->x0 - platX0_, dy = p->y - platY_;
    if (std::abs(dx) + std::abs(dy) > 70 * sc) {   // ruckartig weggezogen: Katze verliert den Halt
        startFall(0, 0);
        return;
    }
    if (p->width() == platW_) x_ += dx;   // verschoben: mitfahren (bei Größenänderung bleibt sie stehen)
    y_ = p->y;
    setPlatform(*p);
    if (x_ < p->x0 - 1 || x_ > p->x1 + 1) {
        const Platform* q = nullptr;   // gleich hohe Fläche daneben?
        for (const Platform& o : s.world->plats)
            if (std::abs(o.y - p->y) <= 3 && x_ >= o.x0 && x_ <= o.x1 && (!q || o.z < q->z)) q = &o;
        if (q) {
            setPlatform(*q);
            y_ = q->y;
        } else {
            startFall((left_ ? -1 : 1) * 30.0 * sc, 0);
        }
    }
}

// Wohin mit einer Katze, die aus dem Bild gefallen ist? Am liebsten auf eine feste Fläche (Taskleiste oder Boden)
// unter ihr, sonst auf die nächstgelegene feste Fläche, notfalls auf irgendeine.
const Platform* CatBrain::rescuePlatform(const Senses& s) const {
    const Platform* best = nullptr;
    double bestD = 0;
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (const Platform& p : s.world->plats) {
            if (pass == 0 && p.kind == PlatKind::Window) continue;
            double d = x_ < p.x0 ? p.x0 - x_ : (x_ > p.x1 ? x_ - p.x1 : 0.0);
            if (!best || d < bestD) {
                best = &p;
                bestD = d;
            }
        }
    return best;
}

void CatBrain::startFall(double vx, double vy) {
    mode_ = Mode::Air;
    falling_ = true;
    plat_ = 0;
    vx_ = vx;
    vy_ = vy;
    frame_ = 0;
    jumpAfterWalk_ = false;
    if (goal_ == Goal::Wander) goal_ = Goal::None;
}

void CatBrain::startJump(const Senses& s, const Platform& to, int landX) {
    (void)s;
    if (std::fabs(landX - x_) > 1) left_ = landX < x_;
    mode_ = Mode::Crouch;
    frame_ = 0;
    airTo_ = to.id;
    ax0_ = x_;
    ay0_ = y_;
    ax1_ = landX;
    ay1_ = to.y;
    crouchFrames_ = std::fabs(ay1_ - ay0_) > 40 ? 4 : 3;
    jumpAfterWalk_ = false;
}

void CatBrain::land(const Senses& s, const Platform& p) {
    (void)s;
    setPlatform(p);
    y_ = p.y;
    mode_ = Mode::Land;
    frame_ = 0;
    falling_ = false;
    posture_ = Posture::Stand;
    dangle_ = false;
}

// ------------------------------------------------------------------ Haltungen, Gehen, Ziele

void CatBrain::beginTransition(Posture a, Posture b) {
    tFrom_ = a;
    tTo_ = b;
    tFrame_ = 0;
    mode_ = Mode::Trans;
}

void CatBrain::goPosture(Posture target, Mode after) {
    afterTrans_ = after;
    tFinal_ = target;
    if (posture_ == Posture::Loaf && dangle_ && target != Posture::Loaf) {
        // erst die Pfote zurückholen (kurzes Einziehen), dann weiter
        dangle_ = false;
    }
    if (posture_ == target) {
        mode_ = after;
        frame_ = 0;
        return;
    }
    beginTransition(posture_, nextStep(posture_, target));
}

void CatBrain::walkTo(double tx, bool run) {
    targetX_ = tx;
    running_ = run;
    bool wantLeft = tx < x_;
    if (std::fabs(tx - x_) > 0.5 && wantLeft != left_) {
        left_ = wantLeft;
        turnFrames_ = 1;   // kurz Kopf zum Betrachter, dann los
    }
    if (posture_ != Posture::Stand) goPosture(Posture::Stand, Mode::Walk);
    else {
        mode_ = Mode::Walk;
        frame_ = 0;
    }
}

bool CatBrain::itemTarget(const Senses& s, const Item* it, uint64_t& plat, int& x) {
    if (!it || !it->active || it->airborne || it->platform == 0) return false;
    const Platform* p = s.world->find(it->platform);
    if (!p) return false;
    const int sc = scale(s);
    plat = p->id;
    if (it->kind == 0) {
        // Seite am Napf: von wo man kommt, aber nicht dieselbe wie die andere Katze
        int side = bowlSide_;
        if (side == 0) side = x_ < it->x ? -1 : 1;
        if (s.otherBowlSide == side) side = -side;
        bowlSide_ = side;
        x = (int)std::lround(it->x + side * P_.headAhead * sc);
    } else {
        int side = x_ < it->x ? -1 : 1;
        double want = it->x + side * (P_.halfLen + 18) * sc;
        if (s.otherPresent && s.otherPlat == it->platform && std::fabs(s.otherX - want) < P_.halfLen * 1.5 * sc) side = -side;
        x = (int)std::lround(it->x + side * (P_.halfLen + 18) * sc);
    }
    x = clampv(x, p->x0 + P_.halfLen * sc / 2, p->x1 - P_.halfLen * sc / 2);
    return true;
}

void CatBrain::pursue(const Senses& s) {
    // Schutz gegen Endlosschleifen (pursue -> arrive -> pursue ...): spätestens beim nächsten Takt weiter
    struct Depth {
        int& d;
        explicit Depth(int& v) : d(v) { ++d; }
        ~Depth() { --d; }
    } guard(depth_);
    if (depth_ > 4) {
        mode_ = Mode::Idle;
        nextDecision_ = s.now + 0.5;
        return;
    }
    const int sc = scale(s);
    if (goal_ == Goal::Bowl && !itemTarget(s, s.bowl, goalPlat_, goalX_)) goal_ = Goal::None;
    if (goal_ == Goal::Toy && !itemTarget(s, s.toy, goalPlat_, goalX_)) {
        if (!(s.toy && s.toy->active)) goal_ = Goal::None;
        else {   // Knäuel fliegt noch: kurz abwarten
            startIdle(s, Posture::Stand);
            nextDecision_ = s.now + 0.4;
            return;
        }
    }
    if (goal_ == Goal::None || s.now > goalUntil_) {
        goal_ = Goal::None;
        startIdle(s, posture_ == Posture::Stand && chance(0.6f) ? Posture::Sit : posture_);
        return;
    }
    const Platform* cur = s.world->find(plat_);
    const Platform* gp = s.world->find(goalPlat_);
    if (!cur || !gp) {
        goal_ = Goal::None;
        startIdle(s, Posture::Stand);
        return;
    }
    const bool urgent = goal_ == Goal::Bowl || goal_ == Goal::Toy || goal_ == Goal::Call;
    const bool run = urgent ? chance(0.35f + P_.runChance) : chance(P_.runChance * 0.5f);
    if (cur->id == gp->id) {
        if (std::fabs(goalX_ - x_) <= 3.0 * sc) {
            arrive(s);
            return;
        }
        walkTo(goalX_, run && std::fabs(goalX_ - x_) > 120.0 * sc);
        return;
    }
    if (++replans_ > 12) {   // kommt nicht hin: aufgeben
        goal_ = Goal::None;
        startIdle(s, Posture::Sit);
        return;
    }
    Reach r{P_.reachUp * sc, P_.reachDown * sc, P_.reachSide * sc};
    Hop h;
    if (!planHop(*s.world, *cur, (int)x_, *gp, goalX_, r, P_.halfLen * sc, h)) {
        goal_ = Goal::None;
        startIdle(s, Posture::Sit);
        lookUntil_ = s.now + 3;   // schaut sehnsüchtig
        return;
    }
    const Platform* nx = s.world->find(h.next);
    if (!nx) return;
    if (h.edge) {
        // Monitorwechsel über den Bildschirmrand (Nachbarmonitor zu hoch/tief zum Springen)
        if (std::fabs(h.takeoffX - x_) <= 3.0 * sc) {
            x_ = h.landX;
            y_ = nx->y;
            setPlatform(*nx);
            pursue(s);   // drüben gleich weiter
            return;
        }
        jumpAfterWalk_ = false;
        walkTo(h.takeoffX, run && std::fabs(h.takeoffX - x_) > 120.0 * sc);
        return;
    }
    if (h.walk) {
        // hinübergehen: Ziel ein Stück auf der neuen Fläche
        double tx = h.next == goalPlat_ ? goalX_ : (nx->x0 + nx->x1) / 2.0;
        jumpAfterWalk_ = false;
        walkTo(tx, run);
        return;
    }
    if (std::fabs(h.takeoffX - x_) <= 3.0 * sc) {
        if (posture_ != Posture::Stand) {
            jumpAfterWalk_ = true;
            jumpTo_ = h.next;
            jumpLandX_ = h.landX;
            targetX_ = x_;
            goPosture(Posture::Stand, Mode::Walk);
        } else {
            startJump(s, *nx, h.landX);
        }
        return;
    }
    jumpAfterWalk_ = true;
    jumpTo_ = h.next;
    jumpLandX_ = h.landX;
    walkTo(h.takeoffX, run && std::fabs(h.takeoffX - x_) > 120.0 * sc);
}

void CatBrain::arrive(const Senses& s) {
    if (jumpAfterWalk_) {
        jumpAfterWalk_ = false;
        const Platform* to = s.world->find(jumpTo_);
        if (to) {
            startJump(s, *to, jumpLandX_);
            return;
        }
    }
    const Platform* cur = s.world->find(plat_);
    // Zielfläche erreicht?
    if (goal_ != Goal::None && cur && cur->id != goalPlat_) {
        // beim Hinübergehen auf eine angrenzende Fläche gewechselt?
        const Platform* gp = s.world->find(goalPlat_);
        if (gp && std::abs(gp->y - cur->y) <= 3 && x_ >= gp->x0 && x_ <= gp->x1) setPlatform(*gp);
        else {
            pursue(s);
            return;
        }
    }
    const int sc = scale(s);
    switch (goal_) {
        case Goal::Bowl: {
            if (s.bowl && s.bowl->active && std::fabs(goalX_ - x_) <= 6.0 * sc) {
                left_ = s.bowl->x < x_;
                mode_ = Mode::Eat;
                frame_ = 0;
                eatUntil_ = s.now + rnd(5.f, 9.f);
                goal_ = Goal::None;
            } else {
                pursue(s);
            }
            return;
        }
        case Goal::Toy: {
            if (s.toy && s.toy->active && !s.toy->airborne) {
                left_ = s.toy->x < x_;
                mode_ = Mode::Play;
                playStep_ = pounced_ ? PlayStep::Bat : PlayStep::Wiggle;   // nach dem Anspringen: zuschlagen
                pounced_ = false;
                batDone_ = false;
                frame_ = 0;
                stepUntil_ = s.now + rnd(0.7f, 1.6f);
            } else {
                pursue(s);
            }
            return;
        }
        case Goal::Call:
            goal_ = Goal::None;
            if (s.cursorValid) left_ = s.cursorX < x_;
            startIdle(s, Posture::Sit);
            lookUntil_ = s.now + 8;
            return;
        case Goal::Other:
            goal_ = Goal::None;
            left_ = s.otherX < x_;
            startIdle(s, s.otherSleeping ? Posture::Loaf : Posture::Sit);
            return;
        case Goal::SleepSpot:
            goal_ = Goal::None;
            goPosture(Posture::Sleep, Mode::Idle);
            wakeAt_ = s.now + rnd(P_.sleepMin, P_.sleepMax);
            return;
        default:
            goal_ = Goal::None;
            startIdle(s, chance(0.5f) ? Posture::Sit : Posture::Stand);
            return;
    }
}

void CatBrain::startIdle(const Senses& s, Posture p) {
    if (posture_ != p) {
        goPosture(p, Mode::Idle);
    } else {
        mode_ = Mode::Idle;
        frame_ = 0;
    }
    nextDecision_ = s.now + rnd(P_.idleMin, P_.idleMax) * (p == Posture::Loaf ? 1.6f : 1.f);
}

void CatBrain::wanderTarget(const Senses& s) {
    const Platform* cur = s.world->find(plat_);
    if (!cur) return;
    const int sc = scale(s);
    const int margin = P_.halfLen * sc;
    auto spotOn = [&](const Platform& p, int& out) {
        // zufälliger sichtbarer Punkt, nicht direkt bei der anderen Katze
        for (int tries = 0; tries < 8; ++tries) {
            int x0 = p.x0 + margin, x1 = p.x1 - margin;
            if (x0 >= x1) return false;
            int x = (int)rnd((float)x0, (float)x1);
            if (!p.visibleAt(x)) continue;
            if (s.otherPresent && s.otherPlat == p.id && std::fabs(x - s.otherX) < 2.4 * margin) continue;
            if (p.id == plat_ && std::fabs(x - x_) < 50.0 * sc) continue;
            out = x;
            return true;
        }
        return false;
    };
    int tx;
    if (chance(P_.wanderOther * (0.5f + energy_ * 0.5f)) && s.world->plats.size() > 1) {
        // andere Fläche: sichtbare Fenster bevorzugen, erreichbar muss sie sein
        std::vector<const Platform*> cands;
        for (const Platform& p : s.world->plats) {
            if (p.id == plat_ || p.visibleLength() < 3 * margin) continue;
            int w = p.kind == PlatKind::Window ? 3 : 1;
            for (int k = 0; k < w; ++k) cands.push_back(&p);
        }
        Reach r{P_.reachUp * sc, P_.reachDown * sc, P_.reachSide * sc};
        for (int tries = 0; tries < 6 && !cands.empty(); ++tries) {
            const Platform* p = cands[(size_t)rnd(0.f, (float)cands.size() - 0.01f)];
            Hop h;
            Hop back;
            if (spotOn(*p, tx) && planHop(*s.world, *cur, (int)x_, *p, tx, r, margin, h) &&
                planHop(*s.world, *p, tx, *cur, (int)x_, r, margin, back)) {   // freiwillig nur, wenn es auch zurück geht
                goal_ = Goal::Wander;
                goalPlat_ = p->id;
                goalX_ = tx;
                goalUntil_ = s.now + 60;
                replans_ = 0;
                return;
            }
        }
    }
    if (spotOn(*cur, tx)) {
        goal_ = Goal::Wander;
        goalPlat_ = cur->id;
        goalX_ = tx;
        goalUntil_ = s.now + 30;
        replans_ = 0;
    }
}

void CatBrain::decide(const Senses& s) {
    const double now = s.now;
    nextDecision_ = now + rnd(P_.idleMin, P_.idleMax) * (posture_ == Posture::Loaf ? 1.6f : 1.f);
    if (posture_ == Posture::Sleep) {
        if (now >= wakeAt_) {
            justWoke_ = true;
            goPosture(chance(0.5f) ? Posture::Sit : Posture::Loaf, Mode::Idle);
        }
        return;
    }
    const float e = energy_;
    // Gewichte der Möglichkeiten
    float wDown = (1.f - e) * 2.2f, wUp = e * 0.8f, wWander = 0.3f + e * 1.6f, wGroom = 0.f, wYawn = 0.f, wStretch = 0.f, wOther = 0.f,
          wDangle = 0.f, wStay = 1.f;
    if (posture_ == Posture::Sit) wGroom = P_.groomChance * 1.5f;
    if (posture_ == Posture::Sit || posture_ == Posture::Loaf) wYawn = 0.25f * (1.2f - e);
    if (posture_ == Posture::Stand && justWoke_) wStretch = 2.f;
    if (s.otherPresent && s.otherPlat != plat_) wOther = 0.25f;
    const Platform* cur = s.world->find(plat_);
    if (posture_ == Posture::Loaf && cur && cur->kind == PlatKind::Window) wDangle = P_.dangleChance * (dangle_ ? 0.6f : 1.5f);
    if (posture_ == Posture::Sleep) wDown = 0.f;
    if (posture_ == Posture::Stand) wUp = 0.f;
    if (posture_ == Posture::Loaf && e > 0.3f) wDown *= 0.3f;   // schlafen erst, wenn müde
    float sum = wDown + wUp + wWander + wGroom + wYawn + wStretch + wOther + wDangle + wStay;
    float r = rnd(0.f, sum);
    justWoke_ = false;
    if ((r -= wDown) < 0) {
        Posture t = posture_ == Posture::Stand ? Posture::Sit : (posture_ == Posture::Sit ? Posture::Loaf : Posture::Sleep);
        if (t == Posture::Sleep) wakeAt_ = now + rnd(P_.sleepMin, P_.sleepMax);
        goPosture(t, Mode::Idle);
        return;
    }
    if ((r -= wUp) < 0) {
        goPosture(posture_ == Posture::Loaf ? Posture::Sit : Posture::Stand, Mode::Idle);
        return;
    }
    if ((r -= wWander) < 0) {
        wanderTarget(s);
        if (goal_ != Goal::None) pursue(s);
        return;
    }
    if ((r -= wGroom) < 0) {
        mode_ = Mode::Flourish;
        flourish_ = Act::Groom;
        frame_ = 0;
        return;
    }
    if ((r -= wYawn) < 0) {
        if (posture_ != Posture::Sit) {
            goPosture(Posture::Sit, Mode::Idle);
            return;
        }
        mode_ = Mode::Flourish;
        flourish_ = Act::Yawn;
        frame_ = 0;
        return;
    }
    if ((r -= wStretch) < 0) {
        mode_ = Mode::Flourish;
        flourish_ = Act::Stretch;
        frame_ = 0;
        return;
    }
    if ((r -= wOther) < 0) {
        const Platform* op = s.world->find(s.otherPlat);
        if (op) {
            const int sc = scale(s);
            int side = x_ < s.otherX ? -1 : 1;
            int tx = (int)std::lround(s.otherX + side * P_.halfLen * 2.3 * sc);
            tx = clampv(tx, op->x0 + P_.halfLen * sc, op->x1 - P_.halfLen * sc);
            goal_ = Goal::Other;
            goalPlat_ = op->id;
            goalX_ = tx;
            goalUntil_ = now + 40;
            replans_ = 0;
            pursue(s);
        }
        return;
    }
    if ((r -= wDangle) < 0) {
        mode_ = Mode::Flourish;
        flourish_ = Act::LoafDangle;
        frame_ = 0;
        return;
    }
    // sonst: sitzen bleiben, ab und zu umschauen
    idleYaw_ = chance(0.5f) ? 0.f : rnd(-0.3f, 1.f);
}

// ------------------------------------------------------------------ Kleinigkeiten im Ruhezustand

void CatBrain::lookAtCursor(const Senses& s) {
    lookYaw_ = idleYaw_;
    lookPitch_ = 0.f;
    if (!s.cursorValid || posture_ == Posture::Sleep) return;
    const int sc = scale(s);
    double dx = s.cursorX - x_, dy = s.cursorY - (y_ - 22.0 * sc);
    double dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 150.0 * sc && s.now > lookUntil_) return;
    bool ahead = (dx > 0) != left_;
    lookYaw_ = ahead && std::fabs(dx) > 25.0 * sc ? 0.25f : 1.f;
    lookPitch_ = (float)clampv(-dy / (90.0 * sc), -1.0, 1.0);
    lookPitch_ = std::round(lookPitch_ * 3.f) / 3.f;   // wenige Stufen (wenige verschiedene Bilder)
}

void CatBrain::updateIdleExtras(const Senses& s) {
    const double now = s.now;
    if (posture_ == Posture::Sleep) {
        blinkStep_ = -1;
        nextBlink_ = std::max(nextBlink_, now + 2.0);
        if (now >= nextBreath_) {   // ein- und ausatmen (zwei Bilder, ruhiger Takt)
            breath_ = breath_ < 0.5f ? 0.5f : 0.f;
            nextBreath_ = now + 1.35;
        }
        if (now >= nextEar_) {
            earStep_ = 0;
            nextEar_ = now + rnd(10.f, 30.f);
        }
    } else {
        breath_ = 0.f;
        if (blinkStep_ >= 0) {
            if (++blinkStep_ > 3) blinkStep_ = -1;
        } else if (now >= nextBlink_) {
            blinkStep_ = 0;
            nextBlink_ = now + rnd(2.5f, 7.f);
        }
        if (now >= nextTail_) {
            if (posture_ == Posture::Stand || posture_ == Posture::Sit) tailUntil_ = now + rnd(1.2f, 3.f);
            nextTail_ = now + rnd(6.f, 16.f);
        }
        if (earStep_ < 0 && now >= nextEar_) {
            earStep_ = 0;
            nextEar_ = now + rnd(8.f, 22.f);
        }
        lookAtCursor(s);
        // Neugier: steht der Mauszeiger lange hinter ihr, dreht sie sich um
        if (s.cursorValid && (posture_ == Posture::Sit || posture_ == Posture::Stand)) {
            const int sc = scale(s);
            double dx = s.cursorX - x_;
            if (std::fabs(dx) < 140.0 * sc && std::fabs(dx) > 30.0 * sc && ((dx > 0) == left_) && chance(0.02f)) left_ = !left_;
        }
    }
    const bool fast = blinkStep_ >= 0 || earStep_ >= 0;
    if (earStep_ >= 0 && ++earStep_ > 2) earStep_ = -1;
    // Schwanz pendelt gemächlich: ohne Blinzeln nur jeder zweite Takt (spart die Hälfte der Bildwechsel)
    if (now < tailUntil_) tailPhase_ = std::fmod(tailPhase_ + (fast ? 1.f / 14.f : 1.f / 7.f), 1.f);
}

// ------------------------------------------------------------------ Takt

double CatBrain::tick(const Senses& s) {
    const double now = s.now;
    double dt = lastTick_ < 0 ? 0.0 : clampv(now - lastTick_, 0.0, 10.0);
    lastTick_ = now;
    const int sc = scale(s);
    if (nextDecision_ == 0) {
        nextDecision_ = now + rnd(1.f, 4.f);
        nextBlink_ = now + rnd(1.f, 4.f);
        nextTail_ = now + rnd(2.f, 6.f);
        nextEar_ = now + rnd(5.f, 12.f);
    }
    // Energie
    if (posture_ == Posture::Sleep && mode_ == Mode::Idle) energy_ = std::min(1.f, energy_ + (float)dt / std::max(30.f, P_.sleepMin));
    else {
        float use = P_.energyUse / 60.f * (mode_ == Mode::Walk ? (running_ ? 3.f : 1.6f) : (mode_ == Mode::Play ? 2.5f : 1.f));
        energy_ = std::max(0.f, energy_ - use * (float)dt);
    }

    followPlatform(s);

    switch (mode_) {
        case Mode::Idle: {
            updateIdleExtras(s);
            if (now >= nextDecision_) {
                if (goal_ != Goal::None && posture_ != Posture::Sleep) pursue(s);   // Ziel noch offen (z. B. Knäuel war in der Luft)
                else decide(s);
            }
            break;
        }
        case Mode::Trans: {
            ++tFrame_;
            if (tFrame_ >= transitionFrames(tFrom_, tTo_)) {
                posture_ = tTo_;
                if (posture_ != tFinal_) beginTransition(posture_, nextStep(posture_, tFinal_));
                else {
                    mode_ = afterTrans_;
                    frame_ = 0;
                    if (mode_ == Mode::Idle) nextDecision_ = now + (goal_ != Goal::None ? 0.3 : (double)rnd(P_.idleMin, P_.idleMax));
                    if (mode_ == Mode::Walk && jumpAfterWalk_ && std::fabs(targetX_ - x_) < 1) arrive(s);
                }
            }
            break;
        }
        case Mode::Walk: {
            if (turnFrames_ > 0) {
                --turnFrames_;
                break;
            }
            const double step = (double)actStepPx(running_ ? Act::Run : Act::Walk) * sc;
            double d = targetX_ - x_;
            if (std::fabs(d) <= step) {
                x_ = targetX_;
                ++frame_;
                arrive(s);
            } else {
                x_ += d > 0 ? step : -step;
                left_ = d < 0;
                ++frame_;
            }
            break;
        }
        case Mode::Crouch: {
            if (++frame_ >= crouchFrames_) {
                const double g = kGravity * sc;
                double rise = std::max(12.0 * sc, 0.16 * std::fabs(ax1_ - ax0_)) + (ay1_ < ay0_ ? 8.0 * sc : 0.0);
                double apex = std::min(ay0_, ay1_) - rise;
                avy0_ = -std::sqrt(2.0 * g * (ay0_ - apex));
                double t1 = -avy0_ / g, t2 = std::sqrt(std::max(0.0, 2.0 * (ay1_ - apex) / g));
                airDur_ = std::max(0.25, t1 + t2);
                double hv = std::fabs(ax1_ - ax0_) / airDur_;
                leapPitch_ = (float)clampv(std::atan2(-avy0_, hv + 1.0) * 0.55, -0.4, 0.75);
                mode_ = Mode::Air;
                falling_ = false;
                airT_ = 0;
                plat_ = 0;
                frame_ = 0;
            }
            break;
        }
        case Mode::Air: {
            const double g = kGravity * sc;
            if (falling_) {
                vy_ += g * kDt;
                double ny = y_ + vy_ * kDt;
                x_ += vx_ * kDt;
                vx_ *= 0.97;
                const Platform* L = s.world->landingBelow(x_, y_, ny);
                if (L) land(s, *L);
                else {
                    y_ = ny;
                    int bottom = -1000000;
                    for (const Monitor& m : s.world->monitors) bottom = std::max(bottom, m.y1);
                    if (!s.world->monitors.empty() && y_ > bottom + 200) {   // Sicherheitsnetz: aus dem Bild gefallen
                        const Platform* f = rescuePlatform(s);
                        if (f) {
                            x_ = clampv(x_, (double)f->x0 + 10, (double)f->x1 - 10);
                            land(s, *f);
                        } else {   // gerade gar keine Fläche (z. B. Bildschirme werden umgestellt): unten warten
                            y_ = bottom + 200;
                            vx_ = vy_ = 0;
                        }
                    }
                }
                ++frame_;
            } else {
                airT_ += kDt;
                const Platform* to = s.world->find(airTo_);
                if (airT_ >= airDur_) {
                    if (to && ax1_ >= to->x0 && ax1_ <= to->x1) {
                        x_ = ax1_;
                        land(s, *to);
                    } else {
                        startFall((ax1_ - ax0_) / airDur_, avy0_ + g * airT_);
                    }
                } else {
                    x_ = ax0_ + (ax1_ - ax0_) * airT_ / airDur_;
                    y_ = ay0_ + avy0_ * airT_ + 0.5 * g * airT_ * airT_;
                    if (to) {   // Zielfenster bewegt sich: Landepunkt mitführen
                        ay1_ = to->y;
                    }
                }
            }
            break;
        }
        case Mode::Land: {
            if (++frame_ >= actFrames(Act::Land)) {
                posture_ = Posture::Stand;
                if (pendingCmd_ >= 0) command((Command)pendingCmd_, s);
                else if (goal_ != Goal::None) pursue(s);
                else startIdle(s, Posture::Stand);
            }
            break;
        }
        case Mode::Carried: {
            // pendelt aus, wenn die Maus stillsteht
            swing_ *= 0.8f;
            if (std::fabs(swing_) < 0.05f) swing_ = 0.f;
            break;
        }
        case Mode::Eat: {
            ++frame_;
            if (s.bowl) s.bowl->food = std::max(0.0, s.bowl->food - kDt / 40.0);
            if (now >= eatUntil_ || !s.bowl || !s.bowl->active || s.bowl->food <= 0.0) {
                bowlSide_ = 0;
                startIdle(s, Posture::Sit);
                if (chance(0.7f)) {   // nach dem Essen putzen
                    mode_ = Mode::Flourish;
                    flourish_ = Act::Groom;
                    frame_ = 0;
                    posture_ = Posture::Sit;
                }
            }
            break;
        }
        case Mode::Play: {
            Item* toy = s.toy;
            if (!toy || !toy->active || now > playUntil_) {
                goal_ = Goal::None;
                startIdle(s, Posture::Sit);
                break;
            }
            double dx = toy->x - x_;
            bool samePlat = toy->platform == plat_ && !toy->airborne;
            if (!samePlat || std::fabs(dx) > (P_.halfLen + 50) * sc) {
                goal_ = Goal::Toy;
                goalUntil_ = playUntil_;
                replans_ = 0;
                posture_ = Posture::Stand;
                pursue(s);
                break;
            }
            ++frame_;
            if (playStep_ == PlayStep::Wiggle) {
                left_ = dx < 0;
                if (now >= stepUntil_) {
                    // Anspringen: kurzer Satz bis kurz vor das Knäuel
                    const Platform* cur = s.world->find(plat_);
                    if (cur) {
                        int side = dx < 0 ? 1 : -1;
                        int tx = (int)std::lround(toy->x + side * (P_.halfLen * 0.6) * sc);
                        tx = clampv(tx, cur->x0 + P_.halfLen * sc / 2, cur->x1 - P_.halfLen * sc / 2);
                        pounced_ = true;
                        batDone_ = false;
                        posture_ = Posture::Stand;
                        startJump(s, *cur, tx);
                        crouchFrames_ = 2;
                        goal_ = Goal::Toy;
                        goalPlat_ = cur->id;
                        goalX_ = tx;
                        goalUntil_ = playUntil_;
                    }
                }
            } else if (playStep_ == PlayStep::Bat) {
                left_ = dx < 0;
                if (frame_ == 2 && !batDone_) {
                    batDone_ = true;
                    batPending_ = true;
                    double dir = dx < 0 ? -1.0 : 1.0;
                    batVx_ = dir * rnd(90.f, 240.f) * sc;
                    batVy_ = -rnd(30.f, 160.f) * sc;
                }
                if (frame_ >= actFrames(Act::Bat)) {
                    playStep_ = PlayStep::Wiggle;
                    stepUntil_ = now + rnd(0.5f, 1.4f);
                    frame_ = 0;
                    if (chance(0.25f)) {   // kurz hinsetzen und gucken
                        posture_ = Posture::Sit;
                    }
                }
            }
            break;
        }
        case Mode::Petted: {
            ++frame_;
            if (now >= pettedUntil_) startIdle(s, Posture::Sit);
            break;
        }
        case Mode::Flourish: {
            ++frame_;
            if (flourish_ == Act::LoafDangle) {
                dangleAmt_ = dangle_ ? 1.f - frame_ / 3.f : frame_ / 3.f;
                if (frame_ >= 3) {
                    dangle_ = !dangle_;
                    dangleAmt_ = dangle_ ? 1.f : 0.f;
                    startIdle(s, Posture::Loaf);
                }
            } else if (frame_ >= actFrames(flourish_)) {
                if (flourish_ == Act::Stretch) startIdle(s, Posture::Stand);
                else startIdle(s, Posture::Sit);
            }
            break;
        }
    }
    fillView(s);
    return nextDelay(s);
}

double CatBrain::nextDelay(const Senses& s) const {
    if (mode_ != Mode::Idle) return kDt;
    const double now = s.now;
    if (blinkStep_ >= 0 || earStep_ >= 0) return kDt;
    if (now < tailUntil_) return 2 * kDt;
    // nur die Zeitpunkte, die in dieser Haltung etwas bewirken (sonst Dauertakt durch längst vergangene Termine)
    double due = std::min(nextDecision_, nextEar_);
    if (posture_ == Posture::Sleep) due = std::min(due, nextBreath_);
    else due = std::min(due, nextBlink_);
    if (posture_ == Posture::Stand || posture_ == Posture::Sit) due = std::min(due, nextTail_);
    double t = due - now;
    if (posture_ != Posture::Sleep && s.cursorValid) {   // Mauszeiger in der Nähe: ab und zu hinschauen
        const int sc = scale(s);
        double dx = s.cursorX - x_, dy = s.cursorY - y_;
        if (dx * dx + dy * dy < (200.0 * sc) * (200.0 * sc)) t = std::min(t, 0.2);
    }
    return clampv(t, kDt, 5.0);
}

// ------------------------------------------------------------------ Bild

void CatBrain::fillView(const Senses& s) {
    (void)s;
    View& v = view_;
    v.transition = false;
    v.ov = Overlay();
    v.param = 0.f;
    v.x = x_;
    v.y = y_;
    v.facingLeft = left_;
    v.platform = plat_;
    v.topmost = false;
    switch (mode_) {
        case Mode::Idle: {
            switch (posture_) {
                case Posture::Stand: v.act = Act::Stand; break;
                case Posture::Sit: v.act = Act::Sit; break;
                case Posture::Loaf:
                    v.act = dangle_ ? Act::LoafDangle : Act::Loaf;
                    v.param = dangle_ ? 1.f : 0.f;
                    break;
                default: v.act = Act::Sleep; break;
            }
            v.frame = 0;
            v.ov.lookYaw = lookYaw_;
            v.ov.lookPitch = lookPitch_;
            static const float blinkCurve[4] = {0.5f, 1.f, 1.f, 0.5f};
            v.ov.blink = blinkStep_ >= 0 ? blinkCurve[std::min(blinkStep_, 3)] : 0.f;
            v.ov.earTwitch = earStep_ == 1 ? 1.f : 0.f;
            v.ov.tailSway = std::round(tailPhase_ * 14.f) / 14.f;
            v.ov.tailAmp = s.now < tailUntil_ ? 1.f : 0.f;
            v.ov.breath = breath_;
            break;
        }
        case Mode::Trans:
            v.transition = true;
            v.from = tFrom_;
            v.to = tTo_;
            v.frame = tFrame_;
            v.act = Act::Stand;
            break;
        case Mode::Walk:
            if (turnFrames_ > 0) {
                v.act = Act::Stand;
                v.ov.lookYaw = 1.f;
            } else {
                v.act = running_ ? Act::Run : Act::Walk;
                v.frame = frame_ % actFrames(v.act);
            }
            break;
        case Mode::Crouch: {
            static const float depth[4] = {0.45f, 0.8f, 1.f, 1.f};
            v.act = Act::Crouch;
            v.param = depth[std::min(frame_, 3)];
            break;
        }
        case Mode::Air:
            v.topmost = true;
            v.platform = 0;
            if (falling_) {
                v.act = Act::Fall;
                v.frame = frame_ % actFrames(Act::Fall);
            } else {
                v.act = Act::Leap;
                v.param = (float)std::round(clampv(airT_ / airDur_, 0.0, 1.0) * 8.0) / 8.f;
                v.ov.lookPitch = leapPitch_;
            }
            break;
        case Mode::Land:
            v.act = Act::Land;
            v.frame = frame_;
            break;
        case Mode::Carried:
            v.act = Act::Carried;
            v.param = std::round(swing_ * 4.f) / 4.f;
            v.x = carryX_;
            v.y = carryY_;
            v.topmost = true;
            v.platform = 0;
            break;
        case Mode::Eat:
            v.act = Act::Eat;
            v.frame = frame_ % actFrames(Act::Eat);
            break;
        case Mode::Play:
            if (playStep_ == PlayStep::Wiggle) {
                v.act = Act::Wiggle;
                v.frame = frame_ % actFrames(Act::Wiggle);
            } else {
                v.act = Act::Bat;
                v.frame = std::min(frame_, actFrames(Act::Bat) - 1);
            }
            break;
        case Mode::Petted:
            v.act = Act::Petted;
            v.frame = frame_ % actFrames(Act::Petted);
            break;
        case Mode::Flourish:
            v.act = flourish_;
            v.frame = std::min(frame_, actFrames(flourish_) - 1);
            if (flourish_ == Act::LoafDangle) {
                v.param = std::max(0.01f, dangleAmt_);
                v.frame = 0;
            }
            break;
    }
}

// ------------------------------------------------------------------ Befehle und Maus

void CatBrain::command(Command c, const Senses& s) {
    const double now = s.now;
    if (mode_ == Mode::Carried || mode_ == Mode::Air || mode_ == Mode::Crouch) {   // erst landen, dann folgen
        pendingCmd_ = (int)c;
        return;
    }
    pendingCmd_ = -1;
    replans_ = 0;
    switch (c) {
        case Command::Feed:
            if (!s.bowl || !s.bowl->active) return;
            goal_ = Goal::Bowl;
            goalUntil_ = now + 120;
            bowlSide_ = 0;
            break;
        case Command::Play:
            if (!s.toy || !s.toy->active) return;
            goal_ = Goal::Toy;
            goalUntil_ = now + 120;
            playUntil_ = now + rnd(25.f, 50.f) * (0.6f + P_.playful);
            break;
        case Command::Call: {
            if (!s.cursorValid) return;
            const int sc = scale(s);
            const Platform* p = s.world->nearestBelow(s.cursorX, s.cursorY, 400 * sc);
            if (!p) return;
            goal_ = Goal::Call;
            goalPlat_ = p->id;
            goalX_ = clampv(s.cursorX + P_.callSide * (int)(P_.halfLen * 1.3) * sc, p->x0 + P_.halfLen * sc, p->x1 - P_.halfLen * sc);
            goalUntil_ = now + 60;
            break;
        }
        case Command::Sleep:
            goal_ = Goal::None;
            energy_ = std::min(energy_, 0.2f);
            wakeAt_ = now + rnd(P_.sleepMin, P_.sleepMax);
            goPosture(Posture::Sleep, Mode::Idle);
            return;
        case Command::Wake:
            if (posture_ == Posture::Sleep) {
                justWoke_ = true;
                goPosture(Posture::Sit, Mode::Idle);
            }
            return;
    }
    if (mode_ == Mode::Eat || mode_ == Mode::Play || mode_ == Mode::Petted || mode_ == Mode::Flourish) {
        mode_ = Mode::Idle;
        if (posture_ == Posture::Loaf && dangle_) dangle_ = false;
    }
    pursue(s);
}

bool CatBrain::grab(int cx, int cy, const Senses& s) {
    mode_ = Mode::Carried;
    plat_ = 0;
    goal_ = Goal::None;
    jumpAfterWalk_ = false;
    carryX_ = cx;
    carryY_ = cy;
    carryVx_ = carryVy_ = 0;
    lastDrag_ = s.now;
    swing_ = 0.f;
    dangle_ = false;
    fillView(s);
    return true;
}

void CatBrain::drag(int cx, int cy, const Senses& s) {
    if (mode_ != Mode::Carried) return;
    double dt = std::max(0.008, s.now - lastDrag_);
    double vx = (cx - carryX_) / dt, vy = (cy - carryY_) / dt;
    carryVx_ = carryVx_ * 0.6 + vx * 0.4;
    carryVy_ = carryVy_ * 0.6 + vy * 0.4;
    carryX_ = cx;
    carryY_ = cy;
    lastDrag_ = s.now;
    const int sc = scale(s);
    swing_ = (float)clampv(-carryVx_ / (400.0 * sc), -1.0, 1.0);
    fillView(s);
}

void CatBrain::release(const Senses& s) {
    if (mode_ != Mode::Carried) return;
    const int sc = scale(s);
    // vom Nackenfell (Haltepunkt) auf die Pfoten umrechnen: hängt etwa 34 Sprite-Pixel tiefer
    x_ = carryX_;
    y_ = carryY_ + 34.0 * sc;
    // Über einer festen Fläche (Taskleiste/Boden) losgelassen? Die Füße landen sonst schon unterhalb ihrer
    // Oberkante, und die Katze fiele durch alles hindurch.
    for (const Platform& p : s.world->plats) {
        if (p.kind == PlatKind::Window || x_ < p.x0 || x_ > p.x1 || y_ <= p.y) continue;
        int bottom = p.y + 200 * sc;
        for (const Monitor& m : s.world->monitors)
            if (x_ >= m.x0 && x_ < m.x1 && p.y >= m.y0 && p.y < m.y1) bottom = m.y1;
        if (y_ <= bottom) y_ = p.y - 1;
    }
    double vx = clampv(carryVx_, -900.0 * sc / 3, 900.0 * sc / 3), vy = clampv(carryVy_, -900.0 * sc / 3, 600.0 * sc / 3);
    if (s.now - lastDrag_ > 0.15) vx = vy = 0;   // Maus stand still: einfach loslassen
    startFall(vx, vy);
    if (vx != 0) left_ = vx < 0;
    posture_ = Posture::Stand;
    fillView(s);
}

void CatBrain::pet(const Senses& s) {
    if (mode_ == Mode::Carried || mode_ == Mode::Air || mode_ == Mode::Crouch) return;
    pettedUntil_ = s.now + rnd(2.2f, 3.5f);
    goal_ = Goal::None;
    jumpAfterWalk_ = false;
    if (posture_ != Posture::Sit) goPosture(Posture::Sit, Mode::Petted);
    else {
        mode_ = Mode::Petted;
        frame_ = 0;
    }
    energy_ = std::min(1.f, energy_ + 0.02f);
}

}  // namespace cat
