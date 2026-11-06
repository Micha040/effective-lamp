// Dauertest mit drei Monitoren (verschiedene Größen, Versatz, Skalierung): zwei Katzen, simulierte 2 Stunden,
// zufällige Befehle. Prüft, dass keine Katze verloren geht (außerhalb aller Monitore, ewig in der Luft, NaN),
// und zählt Monitorwechsel.
#include "../src/core/brain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace cat;

static Platform plat(uint64_t id, PlatKind k, int x0, int x1, int y, int mon, int z) {
    Platform p;
    p.id = id;
    p.kind = k;
    p.x0 = x0;
    p.x1 = x1;
    p.y = y;
    p.monitor = mon;
    p.z = z;
    p.visible.push_back({x0, x1});
    return p;
}

int main(int argc, char** argv) {
    unsigned seed = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    World w;
    // A: 1920x1080 @100 % (Taskleiste), B: 2560x1440 @125 % rechts, höher, ohne Taskleiste,
    // C: hochkant links, tiefer, ohne Taskleiste
    w.monitors = {Monitor{0, 0, 1920, 1080, 3}, Monitor{1920, -180, 4480, 1260, 4}, Monitor{-1080, 300, 0, 2220, 3}};
    w.plats.push_back(plat(1, PlatKind::Taskbar, 0, 1920, 1032, 0, -1));
    w.plats.push_back(plat(2, PlatKind::Floor, 1920, 4480, 1260, 1, -1));
    w.plats.push_back(plat(3, PlatKind::Floor, -1080, 0, 2220, 2, -1));
    // Fenster (z: 0 = vorne)
    w.plats.push_back(plat(100, PlatKind::Window, 200, 1100, 420, 0, 0));
    w.plats.push_back(plat(101, PlatKind::Window, 1500, 2600, 610, 0, 1));   // über die Grenze A/B
    w.plats.push_back(plat(102, PlatKind::Window, 2900, 4200, 300, 1, 2));
    w.plats.push_back(plat(103, PlatKind::Window, -900, -150, 1500, 2, 3));
    w.plats.push_back(plat(104, PlatKind::Window, 3300, 4000, 900, 1, 4));

    CatBrain cats[2] = {CatBrain(personalityFatos(), seed * 7 + 1), CatBrain(personalityKosta(), seed * 13 + 5)};
    cats[0].place(w, w.plats[0], 600, Posture::Loaf);
    cats[1].place(w, w.plats[0], 1200, Posture::Sit);
    double next[2] = {0, 0};
    Item bowl, toy;
    std::mt19937 rng(seed);
    auto rnd = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };

    const double T = 2 * 3600;
    double t = 0, nextCmd = 30, lastItem = 0;
    int monNow[2] = {0, 0}, monChanges[2] = {0, 0};
    double monTime[2][3] = {{0}};
    double airSince[2] = {-1, -1}, outsideSince[2] = {-1, -1};
    int problems = 0, ticks = 0, cmds = 0;
    double lastT = 0;
    auto monitorOf = [&](double x, double y) {
        for (size_t i = 0; i < w.monitors.size(); ++i) {
            const Monitor& m = w.monitors[i];
            if (x >= m.x0 && x < m.x1 && y - 1 >= m.y0 && y - 1 < m.y1) return (int)i;
        }
        return -1;
    };
    while (t < T) {
        // nächstes Ereignis
        double due = std::min(next[0], next[1]);
        if (bowl.active || toy.active) due = std::min(due, lastItem + 1.0 / 12);
        due = std::min(due, nextCmd);
        t = std::max(t, due);
        // Gegenstände
        if (t - lastItem >= 1.0 / 12 - 1e-9) {
            double dt = std::min(0.2, t - lastItem);
            lastItem = t;
            if (bowl.active) stepItem(bowl, w, dt);
            if (toy.active) stepItem(toy, w, dt);
        }
        if (t >= nextCmd) {
            nextCmd = t + rnd(40, 240);
            ++cmds;
            int which = (int)rnd(0, 5);
            int mi = (int)rnd(0, 3);
            const Monitor& m = w.monitors[mi];
            int cx = (int)rnd(m.x0 + 50, m.x1 - 50), cy = (int)rnd(m.y0 + 50, m.y1 - 50);
            for (int i = 0; i < 2; ++i) {
                Senses s;
                s.now = t;
                s.world = &w;
                s.cursorValid = true;
                s.cursorX = cx;
                s.cursorY = cy;
                s.bowl = &bowl;
                s.toy = &toy;
                if (which == 0) {   // füttern: Napf auf die Fläche unter der Maus
                    if (i == 0) {
                        const Platform* best = nullptr;
                        for (const Platform& p : w.plats)
                            if (cx >= p.x0 && cx < p.x1 && p.y >= cy && (!best || p.y < best->y)) best = &p;
                        if (!best) break;
                        bowl = Item();
                        bowl.active = true;
                        bowl.kind = 0;
                        bowl.platform = best->id;
                        bowl.x = std::max<double>(best->x0 + 60, std::min<double>(best->x1 - 60, cx));
                        bowl.y = best->y;
                        bowl.food = 1;
                        bowl.since = t;
                    }
                    cats[i].command(Command::Feed, s);
                } else if (which == 1) {
                    if (i == 0) {
                        toy = Item();
                        toy.active = true;
                        toy.kind = 1;
                        toy.airborne = true;
                        toy.x = cx;
                        toy.y = cy;
                        toy.since = t;
                    }
                    cats[i].command(Command::Play, s);
                } else if (which == 2) {
                    cats[i].command(Command::Call, s);
                } else if (which == 3) {
                    cats[i].command(Command::Sleep, s);
                } else {
                    cats[i].command(Command::Wake, s);
                }
                next[i] = t;
            }
            continue;
        }
        for (int i = 0; i < 2; ++i) {
            if (t + 1e-9 < next[i]) continue;
            Senses s;
            s.now = t;
            s.world = &w;
            s.bowl = &bowl;
            s.toy = &toy;
            const CatBrain& o = cats[1 - i];
            s.otherPresent = true;
            s.otherX = o.x();
            s.otherY = o.y();
            s.otherPlat = o.platform();
            s.otherSleeping = o.sleeping();
            s.otherBowlSide = o.bowlSide();
            double d = cats[i].tick(s);
            ++ticks;
            double vx, vy;
            if (cats[i].takeBat(vx, vy) && toy.active) {
                toy.vx = vx;
                toy.vy = vy;
                if (vy < 0) {
                    toy.airborne = true;
                    toy.platform = 0;
                }
            }
            if (!(d > 0) || d > 60) {
                printf("PROBLEM t=%.1f Katze %d: Verzögerung %.3f\n", t, i, d);
                ++problems;
                d = 0.1;
            }
            next[i] = t + d;
            double x = cats[i].x(), y = cats[i].y();
            if (!std::isfinite(x) || !std::isfinite(y)) {
                printf("PROBLEM t=%.1f Katze %d: NaN\n", t, i);
                ++problems;
                return 2;
            }
            uint64_t p = cats[i].platform();
            if (p) {
                const Platform* pl = w.find(p);
                if (!pl) {
                    printf("PROBLEM t=%.1f Katze %d: Fläche %llu gibt es nicht\n", t, i, (unsigned long long)p);
                    ++problems;
                } else if (std::fabs(y - pl->y) > 0.5 || x < pl->x0 - 2 || x > pl->x1 + 2) {
                    printf("PROBLEM t=%.1f Katze %d: steht neben der Fläche %llu (x=%.0f y=%.0f, Fläche %d..%d y=%d)\n", t, i,
                           (unsigned long long)p, x, y, pl->x0, pl->x1, pl->y);
                    ++problems;
                }
                airSince[i] = -1;
            } else if (cats[i].view().act != Act::Carried) {
                if (airSince[i] < 0) airSince[i] = t;
                if (t - airSince[i] > 8) {
                    printf("PROBLEM t=%.1f Katze %d: seit %.1f s in der Luft (x=%.0f y=%.0f)\n", t, i, t - airSince[i], x, y);
                    ++problems;
                    airSince[i] = t;
                }
            }
            int mi = monitorOf(x, y);
            if (mi < 0) {
                if (outsideSince[i] < 0) outsideSince[i] = t;
                if (t - outsideSince[i] > 3) {
                    printf("PROBLEM t=%.1f Katze %d: seit %.1f s außerhalb aller Monitore (x=%.0f y=%.0f, Fläche %llu)\n", t, i,
                           t - outsideSince[i], x, y, (unsigned long long)p);
                    ++problems;
                    outsideSince[i] = t;
                }
            } else {
                outsideSince[i] = -1;
                if (mi != monNow[i]) {
                    ++monChanges[i];
                    monNow[i] = mi;
                }
            }
        }
        for (int i = 0; i < 2; ++i)
            if (monNow[i] >= 0) monTime[i][monNow[i]] += t - lastT;
        lastT = t;
        if (problems > 20) break;
    }
    for (int i = 0; i < 2; ++i)
        printf("%s: Monitorwechsel %d, Zeit A/B/C = %.0f/%.0f/%.0f min\n", i == 0 ? "Fatos" : "Kosta", monChanges[i], monTime[i][0] / 60,
               monTime[i][1] / 60, monTime[i][2] / 60);
    printf("Schritte %d (%.2f/s), Befehle %d, Probleme %d\n", ticks, ticks / T, cmds, problems);
    return problems ? 1 : 0;
}
