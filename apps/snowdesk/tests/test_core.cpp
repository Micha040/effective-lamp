// Tests für die Schnee-Simulation (Linux, g++). Aufruf: ./test_core
#include "../src/snowcore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace snow;

static int g_checks = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++g_fail;
}
static std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

static const uint64_t kFloor = 1, kA = 10, kB = 20;

static PlatformIn win(uint64_t id, float l, float t, float r) {
    PlatformIn p;
    p.id = id;
    p.left = l;
    p.top = t;
    p.right = r;
    p.corner = 8.0f;
    p.floor = false;
    return p;
}
static PlatformIn floorAt(float top, float width) {
    PlatformIn p;
    p.id = kFloor;
    p.left = 0;
    p.top = top;
    p.right = width;
    p.floor = true;
    return p;
}
static float avgElevation(const World& w, uint64_t id, float x0, float x1) {
    double s = 0;
    int n = 0;
    for (float x = x0; x < x1; x += 2) {
        s += w.pileElevation(id, x);
        ++n;
    }
    return (float)(s / n);
}
static void run(World& w, float seconds, float dt = 1.0f / 30.0f) {
    int n = (int)std::lround(seconds / dt);
    for (int i = 0; i < n; ++i) w.step(dt);
}
static bool conserved(const World& w, const char* what) {
    Stats s = w.stats();
    double t = s.pileMass + s.airMass + s.lostMass;
    double err = std::fabs(t - s.spawnedMass) / std::max(1.0, s.spawnedMass);
    check(err < 1e-3, std::string("Masse bleibt erhalten: ") + what + fmt(" (Abweichung %.4f %%)", err * 100));
    return err < 1e-3;
}

int main() {
    std::printf("Landen & Skyline:\n");
    {
        World w(42);
        w.configure(2560, 1440, 1.0f);
        Params p;
        w.setParams(p);
        w.setPlatforms({win(kA, 400, 500, 1400), win(kB, 900, 300, 1700), floorAt(1392, 2560)});
        run(w, 120.0f);
        check(w.pileElevation(kA, 600) > 0.3f, fmt("Fenster A bekommt Schnee (%.2f px)", w.pileElevation(kA, 600)));
        check(w.pileElevation(kB, 1100) > 0.3f, fmt("das höhere Fenster B fängt im Überlappungsbereich (%.2f px)", w.pileElevation(kB, 1100)));
        check(w.pileHeightAt(kA, 1150) < 0.05f, fmt("A liegt dort im Windschatten von B: kein Schnee (%.3f px)", w.pileHeightAt(kA, 1150)));
        check(w.pileElevation(kFloor, 2450) > 0.3f, fmt("Taskleiste bekommt Schnee (%.2f px)", w.pileElevation(kFloor, 2450)));
        check(w.pileHeightAt(kFloor, 1950) < w.pileHeightAt(kFloor, 2450), "rechts neben B liegt die Taskleiste im Windschatten (weniger Schnee)");
        conserved(w, "120 s Schneefall");
    }

    std::printf("Füllzeit, Maximalhöhe, weiche Schneedecke:\n");
    {
        World w(7);
        w.configure(2560, 1440, 1.0f);
        Params p;
        p.fillMinutes = 6.0f;
        p.maxPileDip = 32.0f;
        p.wind = 0.0f;
        w.setParams(p);
        w.setPlatforms({win(kA, 600, 700, 1800), floorAt(1392, 2560)});
        float worst = 0, worstJump = 0, worstStep = 0;
        float at3 = 0;
        std::vector<float> prev;
        for (float x = 650; x < 1760; x += 10) prev.push_back(0.0f);
        int restingSeen = 0;
        for (int minute = 1; minute <= 7; ++minute) {
            for (int i = 0; i < 60 * 30; ++i) {
                w.step(1.0f / 30.0f);
                size_t k = 0;
                for (float x = 650; x < 1760; x += 10, ++k) {
                    float h = w.pileHeightAt(kA, x);
                    worstJump = std::max(worstJump, std::fabs(h - prev[k]));
                    prev[k] = h;
                }
                if (i % 30 == 0) {
                    for (float x = 610; x < 1790; x += 37) worst = std::max(worst, w.pileElevation(kA, x));
                    for (float x = 700; x < 1700; x += 2) worstStep = std::max(worstStep, std::fabs(w.pileHeightAt(kA, x + 2) - w.pileHeightAt(kA, x)));
                    restingSeen = std::max(restingSeen, w.stats().resting);
                }
            }
            if (minute == 3) at3 = avgElevation(w, kA, 700, 1700);
        }
        float at7 = avgElevation(w, kA, 700, 1700);
        float peak7 = 0;
        for (float x = 700; x < 1700; x += 2) peak7 = std::max(peak7, w.pileHeightAt(kA, x));
        check(at3 > 0.30f * 32 && at3 < 0.70f * 32, fmt("nach 3 von 6 Minuten etwa halb voll (Mittel %.1f von 32 px)", at3));
        check(at7 > 0.82f * 32 && peak7 > 0.93f * 32, fmt("nach 7 Minuten voll (Mittel %.1f px, höchste Welle %.1f von 32 px)", at7, peak7));
        check(worst <= 32.0f + 0.01f, fmt("Maximalhöhe nie überschritten (max %.3f px)", worst));
        check(worstJump < 0.02f, fmt("wächst ohne Sprünge (höchstens %.4f px pro Bild)", worstJump));
        check(worstStep < 0.5f, fmt("glatte Oberfläche (Nachbarpunkte höchstens %.3f px auseinander)", worstStep));
        float edge = w.pileHeightAt(kA, 601), inner = w.pileHeightAt(kA, 720);
        check(edge < 0.6f * inner && edge > 0.15f * inner, fmt("runde Enden: am Rand %.1f px, innen %.1f px", edge, inner));
        check(w.pileElevation(kA, 603) < w.pileElevation(kA, 1200) - 5, fmt("an der runden Ecke weniger Schnee (%.1f px)", w.pileElevation(kA, 603)));
        check(restingSeen > 0, fmt("gelandete Flocken bleiben kurz liegen (bis zu %.0f gleichzeitig)", restingSeen));
        // Überstand an den Enden rieselt ab und zu als kleines Bröckchen herunter (nicht ständig)
        int crumbs = 0;
        for (int i = 0; i < 120 * 30; ++i) {
            Stats before = w.stats();
            w.step(1.0f / 30.0f);
            Stats after = w.stats();
            if (after.crumbs > before.crumbs) crumbs += after.crumbs - before.crumbs;
        }
        check(crumbs >= 1 && crumbs <= 60, fmt("an den Enden rieseln gelegentlich Bröckchen (%.0f in 2 Minuten)", crumbs));
        conserved(w, "9 Minuten");
        w.setSnowing(false);
        run(w, 75.0f);   // auch die langsamsten Flocken sind dann gelandet
        check(w.stats().resting == 0, "ohne Schneefall keine liegenden Flocken mehr");
    }

    std::printf("Schnelles Füllen (Vorschau-Tempo) bleibt weich:\n");
    {
        World w(17);
        w.configure(1280, 720, 0.8f);
        Params p;
        p.amount = 1.25f;
        p.fillMinutes = 0.5f;
        p.maxPileDip = 30.0f;
        w.setParams(p);
        w.setPlatforms({win(kA, 104, 240, 552), win(kB, 624, 152, 1136), floorAt(681.6f, 1280)});
        float worstJump = 0, worstStep = 0;
        std::vector<float> prev;
        for (float x = 650; x < 1110; x += 6) prev.push_back(0.0f);
        for (int i = 0; i < 40 * 30; ++i) {
            w.step(1.0f / 30.0f);
            size_t k = 0;
            for (float x = 650; x < 1110; x += 6, ++k) {
                float h = w.pileHeightAt(kB, x);
                worstJump = std::max(worstJump, std::fabs(h - prev[k]));
                prev[k] = h;
            }
            for (float x = 660; x < 1100; x += 2) worstStep = std::max(worstStep, std::fabs(w.pileHeightAt(kB, x + 2) - w.pileHeightAt(kB, x)));
        }
        check(worstJump < 0.25f, fmt("höchstens %.3f px Zuwachs pro Bild", worstJump));
        check(worstStep < 0.9f, fmt("Oberfläche bleibt glatt (Nachbarpunkte höchstens %.2f px auseinander)", worstStep));
        conserved(w, "schnelles Füllen");
    }

    std::printf("Fenster bewegen / Größe ändern / schließen:\n");
    {
        World w(3);
        w.configure(2560, 1440, 1.0f);
        Params p;
        p.amount = 2.6f;
        p.fillMinutes = 2.0f;
        w.setParams(p);
        w.setPlatforms({win(kA, 400, 500, 1400), floorAt(1392, 2560)});
        run(w, 90.0f);
        float before700 = w.pileElevation(kA, 700);
        check(before700 > 5, fmt("Haufen ist da (%.1f px)", before700));

        // Größe ändern (rechte Kante nach links): nur der Teil ohne Halt fällt
        double airBefore = w.stats().airMass;
        w.setSnowing(false);
        w.setPlatforms({win(kA, 400, 500, 1200), floorAt(1392, 2560)});
        check(std::fabs(w.pileElevation(kA, 700) - before700) < 0.01f, fmt("Breite verkleinert: Schnee in der Mitte bleibt (%.2f -> %.2f px)", before700, w.pileElevation(kA, 700)));
        check(w.stats().airMass > airBefore + 100, fmt("abgeschnittener Teil fällt herunter (%.0f px² in der Luft)", w.stats().airMass));
        check(w.pileElevation(kA, 1300) < 0, "außerhalb des Fensters kein Haufen mehr");
        conserved(w, "nach Größenänderung");

        // Bewegen: alles fällt (vorher alles Fallende landen lassen, damit nur der Haufen zählt)
        run(w, 60.0f);
        double pileA = 0;
        for (float x = 400; x < 1200; x += 2) pileA += std::max(0.0f, w.pileHeightAt(kA, x)) * 2;
        w.setPlatforms({win(kA, 600, 520, 1400), floorAt(1392, 2560)});
        check(w.pileHeightAt(kA, 900) < 0.01f, "verschoben: Haufen ist weg vom Fenster");
        Stats s = w.stats();
        check(s.clumps + s.crumbs > 5, fmt("fällt als Klumpen/Brösel (%.0f Klumpen, %.0f Brösel)", s.clumps, s.crumbs));
        double floorBefore = 0;
        for (float x = 0; x < 2560; x += 2) floorBefore += std::max(0.0f, w.pileHeightAt(kFloor, x)) * 2;
        run(w, 4.0f, 1.0f / 60.0f);
        Stats s2 = w.stats();
        check(s2.clumps == 0, "nach 4 s ist alles gelandet");
        double back = 0;
        for (float x = 600; x < 1400; x += 2) back += std::max(0.0f, w.pileHeightAt(kA, x)) * 2;
        check(back < 0.01 * pileA, fmt("herabfallender Schnee landet nicht wieder auf dem verschobenen Fenster (%.0f von %.0f px²)", back, pileA));
        check(s2.puffs >= 0, "Aufprall staubt (Puffs)");
        conserved(w, "nach Verschieben");
        (void)floorBefore;

        // Fenster geschlossen/minimiert
        w.setSnowing(true);
        run(w, 30.0f);
        w.setPlatforms({floorAt(1392, 2560)});
        check(!w.hasPile(kA), "geschlossen: Plattform weg");
        conserved(w, "nach Schließen");

        // Taskleiste ändert sich leicht: Schnee bleibt
        float fl = w.pileElevation(kFloor, 1800);
        w.setPlatforms({floorAt(1390, 2560)});
        check(std::fabs(w.pileElevation(kFloor, 1800) - fl) < 0.01f, "Taskleiste: Schnee bleibt bei kleiner Änderung liegen");
    }

    std::printf("Sonderfälle:\n");
    {
        World w(11);
        w.configure(1920, 1080, 1.0f);
        Params p;
        w.setParams(p);
        w.setPlatforms({win(kA, 0, 0, 1920), floorAt(1032, 1920)});     // maximiert
        run(w, 60.0f);
        check(w.pileHeightAt(kA, 900) < 0.001f, "maximiertes Fenster sammelt keinen (unsichtbaren) Schnee");
        conserved(w, "maximiert");

        World z(12);
        z.configure(1920, 1080, 1.0f);
        Params q;
        q.maxPileDip = 0;
        z.setParams(q);
        z.setPlatforms({win(kA, 300, 400, 1200), floorAt(1032, 1920)});
        run(z, 60.0f);
        check(z.pileHeightAt(kA, 700) < 0.001f && z.pileHeightAt(kFloor, 700) < 0.001f, "Maximalhöhe 0: nichts bleibt liegen");

        World t(13);
        t.configure(1920, 1080, 1.0f);
        t.setParams(p);
        t.setPlatforms({win(kA, 300, 400, 1200), floorAt(1032, 1920)});
        run(t, 40.0f);
        t.startThaw();
        run(t, 1.0f);
        check(t.active(), "Abtauen läuft noch nach 1 s");
        run(t, 1.2f);
        check(!t.active() && t.stats().flakes == 0, "nach ~2 s ist alles weg (kein Rechenaufwand mehr)");
        t.resume();
        run(t, 2.0f);
        check(t.active() && t.stats().flakes > 10, "wieder einschalten: es schneit");

        World s(14);
        s.configure(1920, 1080, 1.0f);
        Params sp;
        sp.amount = 2.0f;
        sp.fillMinutes = 1.0f;
        s.setParams(sp);
        s.setPlatforms({win(kA, 300, 400, 1200), floorAt(1032, 1920)});
        run(s, 40.0f);
        float fl = s.pileElevation(kFloor, 1500);
        s.shakeAll();
        check(s.pileHeightAt(kA, 700) < 0.001f && std::fabs(s.pileElevation(kFloor, 1500) - fl) < 0.01f, "Abschütteln: Fenster leer, Taskleiste bleibt");

        // Maximalhöhe verkleinern: Überstand fällt
        float h = s.pileElevation(kFloor, 1500);
        sp.maxPileDip = 8;
        s.setParams(sp);
        check(s.pileElevation(kFloor, 1500) <= 8.01f && h > 8, fmt("kleinere Maximalhöhe wird sofort eingehalten (%.1f -> %.1f px)", h, s.pileElevation(kFloor, 1500)));
    }

    std::printf("Abfall-Stile:\n");
    {
        const char* names[] = {"Mischung", "Klumpen", "Bröseln", "Sanft"};
        for (int style = 0; style < 4; ++style) {
            World w(20 + style);
            w.configure(1920, 1080, 1.0f);
            Params p;
            p.amount = 3.0f;
            p.fillMinutes = 1.0f;
            p.releaseStyle = style;
            w.setParams(p);
            w.setPlatforms({win(kA, 300, 400, 1500), floorAt(1032, 1920)});
            run(w, 70.0f);
            w.setSnowing(false);
            run(w, 60.0f);   // alter Schnee in der Luft landet (langsame ferne Flocken brauchen bis zu ~45 s)
            w.setPlatforms({floorAt(1032, 1920)});
            Stats s = w.stats();
            bool ok;
            if (style == ReleaseClump) ok = s.clumps > 3;
            else if (style == ReleaseCrumble) ok = s.clumps == 0 && s.crumbs > 10;
            else if (style == ReleaseGentle) ok = s.clumps == 0 && s.crumbs < 5 && s.flakes > 10;
            else ok = s.clumps > 0 && s.crumbs > 0;
            if (style != ReleaseGentle && s.flakes != 0) ok = false;   // keine neuen Flocken außer bei "Sanft"
            check(ok, std::string(names[style]) + fmt(": %.0f Klumpen, %.0f Brösel, %.0f Flocken", s.clumps, s.crumbs, s.flakes));
        }
    }

    std::printf("Maus:\n");
    {
        double dist[2];
        for (int blow = 0; blow < 2; ++blow) {
            World w(99);
            w.configure(1920, 1080, 1.0f);
            Params p;
            p.amount = 4.0f;
            p.wind = 0;
            p.mouseBlow = blow == 1;
            w.setParams(p);
            w.setPlatforms({floorAt(1032, 1920)});
            run(w, 30.0f);
            int near = 0;
            std::vector<Sprite> sp;
            for (int i = 0; i < 180; ++i) {
                w.setCursor(true, 960, 540);
                w.step(1.0f / 60.0f);
                if (i < 30) continue;      // erst wirken lassen
                w.collectSprites(sp);
                for (const Sprite& s : sp) {
                    float dx = s.x - 960, dy = s.y - 540;
                    if (dx * dx + dy * dy < 45 * 45) ++near;
                }
            }
            dist[blow] = near;
        }
        check(dist[1] < dist[0] * 0.5, fmt("Wegpusten: Flocken direkt an der Maus %.0f -> %.0f", dist[0], dist[1]));
    }

    std::printf("Darstellung:\n");
    {
        World w(5);
        w.configure(2560, 1440, 1.25f);
        Params p;
        p.amount = 2.0f;
        p.fillMinutes = 1.0f;
        w.setParams(p);
        w.setPlatforms({win(kA, 400, 500, 1400), floorAt(1380, 2560)});
        run(w, 40.0f);
        std::vector<PileVertex> mesh;
        w.buildPileMesh(mesh);
        bool okMesh = !mesh.empty() && mesh.size() % 3 == 0;
        float maxPx = w.maxPilePx();
        for (const PileVertex& v : mesh) {
            if (v.dist < -1.001f || v.height < 0 || v.y < 500 - maxPx - 3 || v.y > 1380.5f) okMesh = false;
        }
        check(okMesh, fmt("Haufen-Gitter gültig (%.0f Ecken, Maximalhöhe bei 125 %%: %.0f px)", (double)mesh.size(), maxPx));
        std::vector<Sprite> sp;
        w.collectSprites(sp);
        bool okSp = !sp.empty();
        int crystals = 0;
        for (const Sprite& s : sp) {
            if (!(s.alpha > 0 && s.alpha <= 1.0f) || s.size <= 0) okSp = false;
            if (s.kind == KindCrystal) ++crystals;
        }
        check(okSp, fmt("Flocken gültig (%.0f sichtbar, davon %.0f Kristalle)", (double)sp.size(), crystals));
        check(std::fabs(maxPx - 40.0f) < 0.01f, "Maximalhöhe skaliert mit der Anzeige (32 DIP = 40 px bei 125 %)");
    }

    std::printf("Gleichlauf & Tempo:\n");
    {
        World a(77), b(77);
        for (World* w : {&a, &b}) {
            w->configure(1920, 1080, 1.0f);
            w->setParams(Params());
            w->setPlatforms({win(kA, 300, 400, 1500), floorAt(1032, 1920)});
            run(*w, 20.0f);
        }
        check(std::fabs(a.stats().pileMass - b.stats().pileMass) < 1e-6 && a.stats().flakes == b.stats().flakes, "gleicher Startwert -> gleicher Ablauf");

        World w(8);
        w.configure(2560, 1440, 1.0f);
        Params p;
        p.amount = 2.6f;   // Schneesturm
        p.wind = 0.8f;
        p.fillMinutes = 2.0f;
        w.setParams(p);
        std::vector<PlatformIn> pl = {win(kA, 100, 300, 900), win(kB, 700, 600, 1600), win(30, 1500, 200, 2400),
                                      win(31, 300, 900, 1200), win(32, 1800, 800, 2500), floorAt(1392, 2560)};
        w.setPlatforms(pl);
        run(w, 60.0f);
        auto t0 = std::chrono::high_resolution_clock::now();
        const int steps = 600;
        for (int i = 0; i < steps; ++i) w.step(1.0f / 60.0f);
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / steps;
        Stats s = w.stats();
        std::vector<Sprite> sp;
        std::vector<PileVertex> mesh;
        auto t2 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i) {
            w.collectSprites(sp);
            w.buildPileMesh(mesh);
        }
        auto t3 = std::chrono::high_resolution_clock::now();
        double us2 = std::chrono::duration<double, std::micro>(t3 - t2).count() / 100;
        check(us < 1500, fmt("Schneesturm, 1440p, 5 Fenster: %.0f µs pro Schritt (%.0f Teilchen)", us, (double)(s.flakes + s.crystals + s.crumbs + s.clumps + s.puffs)));
        check(us2 < 1500, fmt("Zeichendaten aufbereiten: %.0f µs pro Bild (%.0f Haufen-Ecken)", us2, (double)mesh.size()));
        // Fenster wird gezogen: jede Bildaufnahme eine neue Position
        auto t4 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 120; ++i) {
            pl[0] = win(kA, 100.0f + i * 3, 300.0f + i, 900.0f + i * 3);
            w.setPlatforms(pl);
            w.step(1.0f / 60.0f);
        }
        auto t5 = std::chrono::high_resolution_clock::now();
        double us3 = std::chrono::duration<double, std::micro>(t5 - t4).count() / 120;
        check(us3 < 3000, fmt("Fenster ziehen: %.0f µs pro Bild", us3));
        conserved(w, "Sturm + Ziehen");
    }

    std::printf("\nEchtes Wetter: Regen\n");
    {
        World w(11);
        w.configure(1920, 1080, 1.0f);
        Params p;
        p.amount = 0.0f;
        p.rain = 1.0f;
        p.wind = 0.3f;
        w.setParams(p);
        std::vector<PlatformIn> pl = {win(kA, 200, 400, 1000), win(kB, 1200, 300, 1700), floorAt(1032, 1920)};
        w.setPlatforms(pl);
        run(w, 20.0f);
        Stats s = w.stats();
        check(s.rain > 20, fmt("es regnet (%.0f Spuren in der Luft)", s.rain));
        check(s.flakes == 0 && s.pileMass < 1.0, "kein Schnee bei Regen");
        check(w.beadCount(kA) > 0 && w.beadCount(kB) > 0, fmt("Tropfen auf beiden Kanten (%.0f / %.0f)", w.beadCount(kA), w.beadCount(kB)));
        check(w.beadCount(kFloor) == 0, "keine Tropfen auf dem Boden");
        check(w.frameRateHint() < 0, "Regen braucht die volle Bildrate");
        // Regen hört auf: Tropfen rollen ab/tropfen, verdunsten
        p.rain = 0.0f;
        w.setParams(p);
        float water0 = w.beadWater(kA) + w.beadWater(kB);
        int dripsSeen = 0;
        for (int i = 0; i < 30 * 30; ++i) {
            w.step(1.0f / 30.0f);
            dripsSeen = std::max(dripsSeen, w.stats().drops);
        }
        float water1 = w.beadWater(kA) + w.beadWater(kB);
        check(water1 < water0, fmt("Wasser nimmt ab (%.1f -> %.1f)", water0, water1));
        // lange warten: alles verdunstet, danach steht das Bild still
        run(w, 900.0f, 0.25f);
        check(w.beadCount(kA) == 0 && w.beadCount(kB) == 0, "nach 15 Minuten ist alles verdunstet");
        check(w.frameRateHint() == 0.0f, fmt("danach nichts mehr zu zeichnen (Hinweis %.0f)", w.frameRateHint()));
        check(!w.active(), "Welt ist ruhig (kein Takt nötig)");
        uint64_t v = w.version();
        w.step(1.0f / 30.0f);
        check(w.version() == v, "ruhige Welt: Bild ändert sich nicht");
    }
    {
        World w(15);
        w.configure(2560, 1440, 1.25f);
        Params p;
        p.amount = 0.0f;
        p.rain = 2.5f;
        p.wind = 0.6f;
        w.setParams(p);
        std::vector<PlatformIn> pl;
        for (int i = 0; i < 5; ++i) pl.push_back(win(100 + i, 100.0f + i * 420, 200.0f + i * 150, 600.0f + i * 420));
        pl.push_back(floorAt(1380, 2560));
        w.setPlatforms(pl);
        run(w, 10.0f, 1.0f / 60.0f);
        const int steps = 600;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < steps; ++i) w.step(1.0f / 60.0f);
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / steps;
        std::vector<Sprite> sp;
        auto t2 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100; ++i) w.collectSprites(sp);
        auto t3 = std::chrono::high_resolution_clock::now();
        double us2 = std::chrono::duration<double, std::micro>(t3 - t2).count() / 100;
        Stats s = w.stats();
        check(us < 800, fmt("Starkregen, 1440p, 5 Fenster: %.0f µs pro Schritt (%.0f Spuren)", us, (double)s.rain));
        check(us2 < 500, fmt("Zeichendaten: %.0f µs pro Bild (%.0f Sprites)", us2, (double)sp.size()));
    }

    std::printf("\nEchtes Wetter: Tropfen rollen zur Ecke und tropfen ab\n");
    {
        World w(12);
        w.configure(1920, 1080, 1.0f);
        Params p;
        p.amount = 0.0f;
        p.wind = 0.0f;
        w.setParams(p);
        std::vector<PlatformIn> pl = {win(kA, 200, 400, 1000), win(kB, 100, 700, 1600), floorAt(1032, 1920)};
        w.setPlatforms(pl);
        w.addWaterForTest(kA, 260, 1.8f);   // großer Tropfen nahe der linken Ecke (hängt dann außen an A, B reicht weiter nach links)
        check(w.beadCount(kA) == 1, "ein Tropfen liegt");
        float hint = w.frameRateHint();
        int drops = 0;
        bool landedOnB = false;
        for (int i = 0; i < 30 * 12; ++i) {
            w.step(1.0f / 30.0f);
            drops = std::max(drops, w.stats().drops);
            if (w.beadWater(kB) > 0.2f) landedOnB = true;
        }
        check(hint > 0.0f, fmt("rollender Tropfen: wenige Bilder pro Sekunde reichen (%.0f)", hint));
        check(drops > 0 || w.stats().hanging > 0, "an der Ecke angekommen (hängt bzw. tropft)");
        // so viel Wasser an die Ecke, dass er abreißt
        for (int k = 0; k < 6; ++k) w.addWaterForTest(kA, 230, 1.5f);
        for (int i = 0; i < 30 * 8; ++i) {
            w.step(1.0f / 30.0f);
            drops = std::max(drops, w.stats().drops);
            if (w.beadWater(kB) > 0.2f) landedOnB = true;
        }
        check(drops > 0, "Tropfen reißt ab und fällt");
        check(landedOnB, "fällt auf das Fenster darunter (nicht durch)");
        // Fenster wird verschoben: Wasser darauf fällt herunter
        w.addWaterForTest(kB, 900, 0.6f);
        float before = w.beadWater(kB);
        pl[1] = win(kB, 140, 760, 1640);
        w.setPlatforms(pl);
        check(before > 0.0f && w.beadWater(kB) < 0.01f, fmt("verschobenes Fenster: Tropfen fallen herunter (%.1f -> %.1f)", before, w.beadWater(kB)));
        check(w.stats().drops > 0, "fallen als Tropfen");
    }

    std::printf("\nEchtes Wetter: Tauwetter, Nebel, Gewitter\n");
    {
        World w(13);
        w.configure(1920, 1080, 1.0f);
        Params p;
        p.amount = 1.5f;
        p.fillMinutes = 0.5f;
        p.maxPileDip = 30.0f;
        w.setParams(p);
        std::vector<PlatformIn> pl = {win(kA, 200, 400, 1000), floorAt(1032, 1920)};
        w.setPlatforms(pl);
        run(w, 40.0f);
        float e0 = avgElevation(w, kA, 300, 900);
        check(e0 > 15.0f, fmt("Schnee liegt (%.1f px)", e0));
        p.amount = 0.0f;
        p.meltDipPerMin = 6.0f;   // 6 px pro Minute
        w.setParams(p);
        run(w, 120.0f, 0.1f);
        float e1 = avgElevation(w, kA, 300, 900);
        check(e1 < e0 - 8.0f, fmt("taut: %.1f -> %.1f px in 2 Minuten", e0, e1));
        check(w.stats().hanging > 0 || w.stats().drops > 0 || w.beadWater(kA) > 0.0f, "Schmelzwasser sammelt sich an den Ecken");
        float hint = w.frameRateHint();
        check(hint > 0.0f && hint <= 15.0f, fmt("Tauen braucht nur wenige Bilder pro Sekunde (%.0f)", hint));
        run(w, 600.0f, 0.25f);
        check(avgElevation(w, kA, 300, 900) < 0.5f, "nach 12 Minuten weggetaut");

        // Nebel
        p.meltDipPerMin = 0.0f;
        p.fog = 0.8f;
        w.setParams(p);
        run(w, 10.0f);
        std::vector<Sprite> sp;
        w.collectSprites(sp);
        int fog = 0;
        for (const Sprite& x : sp) fog += x.kind == KindFog ? 1 : 0;
        check(fog > 3, fmt("Nebelschwaden sichtbar (%.0f)", fog));
        check(w.frameRateHint() > 0.0f && w.frameRateHint() <= 15.0f, fmt("Nebel: %.0f Bilder pro Sekunde", w.frameRateHint()));
        p.fog = 0.0f;
        w.setParams(p);
        run(w, 40.0f, 0.1f);
        check(w.stats().fog == 0, "Nebel löst sich auf");

        // Blitz
        w.flashNow();
        w.step(1.0f / 30.0f);
        w.collectSprites(sp);
        bool flash = false;
        for (const Sprite& x : sp) flash = flash || x.kind == KindFlash;
        check(flash, "Blitz zu sehen");
        run(w, 1.5f);
        w.collectSprites(sp);
        flash = false;
        for (const Sprite& x : sp) flash = flash || x.kind == KindFlash;
        check(!flash, "Blitz vorbei");
    }

    std::printf("\nWind mit Richtung\n");
    {
        World w(14);
        w.configure(1920, 1080, 1.0f);
        Params p;
        p.wind = -0.8f;
        w.setParams(p);
        w.setPlatforms({floorAt(1032, 1920)});
        double sum = 0;
        for (int i = 0; i < 600; ++i) {
            w.step(1.0f / 60.0f);
            sum += w.windNow();
        }
        check(sum < 0, "Wind nach links (negativ)");
    }

    std::printf("\n%d Prüfungen, %d Fehler\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
