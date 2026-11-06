// Test: Geht eine Katze verloren, wenn es keinen "Boden" gibt (Taskleiste auf jedem Monitor)? Fallen lassen über der
// Taskleiste, über den Bildschirmrand werfen, Boden wird zur Taskleiste. Nach 60 Sekunden muss jede Katze wieder
// auf einer Fläche sitzen und ruhig ticken. Aufruf über tests/run.sh
#include "../src/core/brain.h"

#include <cstdio>

using namespace cat;

static Platform plat(uint64_t id, PlatKind k, int x0, int x1, int y, int mon) {
    Platform p;
    p.id = id;
    p.kind = k;
    p.x0 = x0;
    p.x1 = x1;
    p.y = y;
    p.monitor = mon;
    p.z = -1;
    p.visible.push_back({x0, x1});
    return p;
}

static Senses sense(World& w, double t, int cx, int cy) {
    Senses s;
    s.now = t;
    s.world = &w;
    s.cursorValid = true;
    s.cursorX = cx;
    s.cursorY = cy;
    return s;
}

static int g_fail = 0;

// wie App::heartbeat: Gehirn ticken lassen, am Ende prüfen, wo die Katze ist
static void runFor(const char* label, World& w, CatBrain& b, double t0, double secs) {
    double t = t0, next = t0;
    int ticks = 0;
    while (t < t0 + secs) {
        if (t >= next) {
            next = t + b.tick(sense(w, t, 10, 10));
            ++ticks;
        }
        t += 0.001;
    }
    // zur Ruhe gekommen: auf einer Fläche, im Bild, nicht mehr im 12-Hz-Takt des Fallens
    double rate = 0;
    {
        int n = 0;
        double tt = t, nx = t;
        for (; tt < t + 10; tt += 0.001)
            if (tt >= nx) {
                nx = tt + b.tick(sense(w, tt, 10, 10));
                ++n;
            }
        rate = n / 10.0;
    }
    bool onPlat = b.platform() != 0 && w.find(b.platform()) != nullptr;
    bool inside = b.y() <= 1080 && b.y() >= -200;
    bool ok = onPlat && inside && rate < 8;
    std::printf("  %s %-46s Fläche=%llu x=%.0f y=%.0f  Ticks/s danach %.1f\n", ok ? "ok   " : "FAIL ", label, (unsigned long long)b.platform(), b.x(), b.y(), rate);
    if (!ok) ++g_fail;
}

int main() {
    // Default Windows 11: one 1920x1080 monitor, visible bottom taskbar, onTaskbar=true -> no Floor platform
    World w;
    w.monitors = {Monitor{0, 0, 1920, 1080, 3}};
    w.plats.push_back(plat(1, PlatKind::Taskbar, 0, 1920, 1032, 0));

    {   // 1) drag the cat and let go with the cursor just above the taskbar (feet end up 34*sc px below the cursor)
        CatBrain b(personalityFatos(), 1);
        b.place(w, w.plats[0], 800, Posture::Sit);
        b.tick(sense(w, 0, 900, 960));
        b.grab(900, 960, sense(w, 1.0, 900, 960));
        b.drag(900, 960, sense(w, 1.1, 900, 960));
        b.release(sense(w, 1.5, 900, 960));   // mouse stood still -> plain drop
        runFor("1) dropped at cursor y=960 (taskbar y=1032):", w, b, 1.5, 60);
    }
    {   // 2) thrown hard toward the right screen edge
        CatBrain b(personalityKosta(), 2);
        b.place(w, w.plats[0], 800, Posture::Sit);
        b.tick(sense(w, 0, 1700, 600));
        b.grab(1600, 600, sense(w, 1.0, 1600, 600));
        b.drag(1700, 600, sense(w, 1.05, 1700, 600));
        b.drag(1800, 600, sense(w, 1.10, 1800, 600));
        b.release(sense(w, 1.12, 1800, 600));
        runFor("2) thrown right from x=1800,y=600:", w, b, 1.12, 60);
    }
    {   // 3) "Auf der Taskleiste" switched on: Floor (same y) replaced by Taskbar
        World w2;
        w2.monitors = w.monitors;
        w2.plats.push_back(plat(0xFFFE000000000000ull, PlatKind::Floor, 0, 1920, 1032, 0));
        CatBrain b(personalityFatos(), 3);
        b.place(w2, w2.plats[0], 800, Posture::Sit);
        b.tick(sense(w2, 0, 10, 10));
        w2.plats.clear();
        w2.plats.push_back(plat(0xFFFF000000000000ull, PlatKind::Taskbar, 0, 1920, 1032, 0));
        runFor("3) Floor->Taskbar at same y (setting toggled):", w2, b, 0.1, 60);
    }
    {   // control: same drop with a Floor present (old test layout) is rescued
        World w3;
        w3.monitors = w.monitors;
        w3.plats.push_back(plat(2, PlatKind::Floor, 0, 1920, 1032, 0));
        CatBrain b(personalityFatos(), 4);
        b.place(w3, w3.plats[0], 800, Posture::Sit);
        b.tick(sense(w3, 0, 900, 960));
        b.grab(900, 960, sense(w3, 1.0, 900, 960));
        b.release(sense(w3, 1.5, 900, 960));
        runFor("control: same drop, but Floor platform exists:", w3, b, 1.5, 60);
    }
    {   // 4) Monitor abgezogen: die Katze stand auf dem zweiten Bildschirm, der jetzt fehlt
        World w4;
        w4.monitors = {Monitor{0, 0, 1920, 1080, 3}, Monitor{1920, 0, 3840, 1080, 3}};
        w4.plats.push_back(plat(1, PlatKind::Taskbar, 0, 1920, 1032, 0));
        w4.plats.push_back(plat(5, PlatKind::Floor, 1920, 3840, 1080, 1));
        CatBrain b(personalityKosta(), 5);
        b.place(w4, w4.plats[1], 3000, Posture::Sit);
        b.tick(sense(w4, 0, 10, 10));
        w4.monitors.pop_back();
        w4.plats.pop_back();
        runFor("4) zweiter Monitor abgezogen:", w4, b, 0.1, 60);
    }
    std::printf("%s\n", g_fail ? "FEHLER: eine Katze ist verloren gegangen" : "keine Katze verloren");
    return g_fail ? 1 : 0;
}
