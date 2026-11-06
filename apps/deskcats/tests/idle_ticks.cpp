// Wie oft wird eine Katze in Ruhe aufgeweckt? (Sparsamkeit) – schlafend, liegend, sitzend, mit Mauszeiger in der Nähe
#include "../src/core/brain.h"

#include <cstdio>

using namespace cat;

static double run(Command cmd, bool cursorNear, double T, int& ticksOut) {
    World w;
    w.monitors = {Monitor{0, 0, 1920, 1080, 3}};
    Platform tb;
    tb.id = 1;
    tb.kind = PlatKind::Taskbar;
    tb.x0 = 0;
    tb.x1 = 1920;
    tb.y = 1032;
    tb.z = -1;
    tb.visible = {{0, 1920}};
    w.plats.push_back(tb);
    CatBrain b(personalityFatos(), 5);
    b.place(w, w.plats[0], 800, Posture::Sit);
    Item bowl, toy;
    double next = 0;
    int ticks = 0;
    bool sent = false;
    for (double t = 0; t < T; t += 0.001) {
        if (t < next) continue;
        Senses s;
        s.now = t;
        s.world = &w;
        s.cursorValid = true;
        s.cursorX = cursorNear ? 900 : 10;
        s.cursorY = cursorNear ? 950 : 10;
        s.bowl = &bowl;
        s.toy = &toy;
        if (!sent && t > 1) {
            b.command(cmd, s);
            sent = true;
        }
        next = t + b.tick(s);
        if (t > 30) ++ticks;   // nach dem Einschlafen zählen
    }
    ticksOut = ticks;
    return ticks / (T - 30);
}

int main() {
    int n;
    double r1 = run(Command::Sleep, false, 300, n);
    std::printf("schlafend:               %.2f Schritte/s\n", r1);
    double r2 = run(Command::Wake, false, 300, n);
    std::printf("wach, Maus weit weg:     %.2f Schritte/s\n", r2);
    double r3 = run(Command::Wake, true, 300, n);
    std::printf("wach, Maus in der Nähe:  %.2f Schritte/s\n", r3);
    return (r1 < 2.0 && r2 < 6.0) ? 0 : 1;
}
