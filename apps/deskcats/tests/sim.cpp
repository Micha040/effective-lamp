// Simulation: zwei Katzen auf einem Beispiel-Desktop (Fenster, Taskleiste), mit Füttern, Spielen, Fenster ziehen,
// Fenster schließen, Rufen, Tragen, Schlafen. Schreibt out/sim/frames.txt + Sprites (.raw) für das Vorschauvideo.
#include "../src/core/brain.h"
#include "../src/core/pixcat.h"
#include "../src/core/props.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace cat;

struct Win {
    uint64_t id;
    int x0, y0, x1, y1;
    bool open;
    const char* title;
};

static const int SW = 1280, SH = 720, TB = 40, SCALE = 2;

static void buildWorld(World& w, const std::vector<Win>& wins) {
    w.plats.clear();
    w.monitors = {Monitor{0, 0, SW, SH, SCALE}};
    for (size_t i = 0; i < wins.size(); ++i) {
        const Win& win = wins[i];
        if (!win.open) continue;
        Platform p;
        p.id = win.id;
        p.kind = PlatKind::Window;
        p.x0 = win.x0;
        p.x1 = win.x1;
        p.y = win.y0;
        p.z = (int)i;
        std::vector<Span> spans{{win.x0, win.x1}};
        for (size_t j = 0; j < i; ++j) {   // Fenster davor verdecken die Kante
            const Win& f = wins[j];
            if (!f.open || !(f.y0 <= p.y - 2 && f.y1 > p.y - 2)) continue;
            std::vector<Span> next;
            for (const Span& s : spans) {
                if (f.x1 <= s.x0 || f.x0 >= s.x1) next.push_back(s);
                else {
                    if (f.x0 > s.x0) next.push_back({s.x0, f.x0});
                    if (f.x1 < s.x1) next.push_back({f.x1, s.x1});
                }
            }
            spans.swap(next);
        }
        p.visible = spans;
        w.plats.push_back(p);
    }
    Platform tb;
    tb.id = 1;
    tb.kind = PlatKind::Taskbar;
    tb.x0 = 0;
    tb.x1 = SW;
    tb.y = SH - TB;
    tb.z = -1;
    tb.visible = {{0, SW}};
    w.plats.push_back(tb);
}

static std::set<std::string> g_saved;

static void saveSprite(const std::string& key, const PixelImage& im) {
    if (g_saved.count(key)) return;
    g_saved.insert(key);
    FILE* f = std::fopen(("out/sim/" + key + ".raw").c_str(), "wb");
    std::fprintf(f, "%d %d %d %d\n", im.w, im.h, im.originX, im.originY);
    for (uint32_t v : im.px) {
        unsigned char b[4] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, (unsigned char)(v >> 24)};
        std::fwrite(b, 1, 4, f);
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const double T = argc > 1 ? std::atof(argv[1]) : 90.0;
    if (std::system("mkdir -p out/sim") != 0) return 1;
    std::vector<Win> wins = {
        {101, 700, 120, 1160, 520, true, "Browser"},
        {100, 90, 260, 640, 600, true, "Notizen"},
        {102, 380, 470, 780, 660, true, "Musik"},
    };
    World world;
    buildWorld(world, wins);
    Look looks[2] = {lookFatos(), lookKosta()};
    CatBrain cats[2] = {CatBrain(personalityFatos(), 7), CatBrain(personalityKosta(), 11)};
    cats[0].place(world, *world.find(100), 420, Posture::Loaf);
    cats[1].place(world, *world.find(101), 960, Posture::Sit);
    double nextT[2] = {0, 0};
    Item bowl, toy;
    bowl.kind = 0;
    toy.kind = 1;
    PixelStyle st;
    FILE* out = std::fopen("out/sim/frames.txt", "w");
    int cursorX = 900, cursorY = 400;
    bool carrying = false;
    int ticks[2] = {0, 0};
    const double dt = 1.0 / kFps;
    for (int fi = 0; fi * dt < T; ++fi) {
        const double t = fi * dt;
        // ---- Ablauf
        auto senses = [&](int i) {
            Senses s;
            s.now = t;
            s.world = &world;
            s.cursorValid = true;
            s.cursorX = cursorX;
            s.cursorY = cursorY;
            s.bowl = &bowl;
            s.toy = &toy;
            const CatBrain& o = cats[1 - i];
            s.otherPresent = true;
            s.otherX = o.x();
            s.otherY = o.y();
            s.otherPlat = o.platform();
            s.otherSleeping = o.sleeping();
            s.otherBowlSide = o.bowlSide();
            return s;
        };
        // Mauszeiger wandert gemächlich
        cursorX = 640 + (int)(380 * std::sin(t * 0.21));
        cursorY = 330 + (int)(160 * std::sin(t * 0.33 + 1));
        if (fi == (int)(6 / dt)) {   // Füttern
            bowl.active = true;
            bowl.platform = 1;
            bowl.x = 560;
            bowl.y = SH - TB;
            bowl.food = 1.0;
            bowl.since = t;
            for (int i = 0; i < 2; ++i) cats[i].command(Command::Feed, senses(i)), nextT[i] = t;
        }
        if (fi == (int)(30 / dt)) bowl.active = false;
        if (fi == (int)(32 / dt)) {   // Spielen: Knäuel fällt auf die Notizen
            toy.active = true;
            toy.platform = 0;
            toy.airborne = true;
            toy.x = 360;
            toy.y = 120;
            toy.vx = 40;
            toy.vy = 0;
            for (int i = 0; i < 2; ++i) cats[i].command(Command::Play, senses(i)), nextT[i] = t;
        }
        if (fi == (int)(54 / dt)) toy.active = false;   // Spielzeug weggeräumt
        if (t >= 55 && t < 58) {                          // Rufen: Mauszeiger über dem Browser
            cursorX = 930;
            cursorY = 80;
        }
        if (fi == (int)(55.2 / dt))
            for (int i = 0; i < 2; ++i) cats[i].command(Command::Call, senses(i)), nextT[i] = t;
        if (t >= 55 && t < 66) {
            cursorX = 930 + (int)(30 * std::sin(t));
            cursorY = 70;
        }
        if (t >= 66 && t < 69) {   // Browser-Fenster langsam nach links ziehen: die Katzen fahren mit
            for (Win& w : wins)
                if (w.id == 101) {
                    w.x0 -= 4;
                    w.x1 -= 4;
                }
            buildWorld(world, wins);
            cursorX = 860 - (int)((t - 66) * 48 * 12 / 12);
            cursorY = 130;
            for (int i = 0; i < 2; ++i) nextT[i] = std::min(nextT[i], t);
        }
        if (fi == (int)(72 / dt)) {   // Browser schließen: wer oben sitzt, fällt
            for (Win& w : wins)
                if (w.id == 101) w.open = false;
            buildWorld(world, wins);
            for (int i = 0; i < 2; ++i) nextT[i] = t;
        }
        if (fi == (int)(79 / dt)) {   // Kosta hochheben und auf die Notizen setzen
            Senses s = senses(1);
            carrying = cats[1].grab((int)cats[1].x(), (int)cats[1].y() - 40, s);
        }
        if (carrying && t < 82.5) {
            Senses s = senses(1);
            int gx = 330, gy = 160;
            cats[1].drag((int)(cats[1].view().x + (gx - cats[1].view().x) * 0.12), (int)(cats[1].view().y + (gy - cats[1].view().y) * 0.12), s);
            cursorX = (int)cats[1].view().x;
            cursorY = (int)cats[1].view().y;
        }
        if (carrying && t >= 82.5 && t < 83.5) {
            cursorX = (int)cats[1].view().x;
            cursorY = (int)cats[1].view().y;
        }
        if (carrying && t >= 83.5) {
            Senses s = senses(1);
            cats[1].release(s);
            carrying = false;
            nextT[1] = t;
        }
        if (fi == (int)(87 / dt)) {
            Senses s = senses(0);
            cats[0].command(Command::Sleep, s);
            nextT[0] = t;
        }
        if (t >= 72 && t < 79) {   // Maus wandert weg
            cursorX = 1000;
            cursorY = 300;
        }
        // ---- Katzen
        for (int i = 0; i < 2; ++i) {
            if (carrying && i == 1) continue;
            if (t + 1e-9 >= nextT[i]) {
                Senses s = senses(i);
                double d = cats[i].tick(s);
                nextT[i] = t + d - 1e-6;
                ++ticks[i];
                double vx, vy;
                if (cats[i].takeBat(vx, vy) && toy.active) {
                    toy.vx = vx;
                    toy.vy = vy;
                    if (vy < 0) {
                        toy.airborne = true;
                        toy.platform = 0;
                    }
                }
            }
        }
        stepItem(bowl, world, dt);
        stepItem(toy, world, dt);
        // ---- Ausgabe
        std::fprintf(out, "frame %d\n", fi);
        for (const Win& w : wins)
            if (w.open) std::fprintf(out, "win %llu %d %d %d %d %s\n", (unsigned long long)w.id, w.x0, w.y0, w.x1, w.y1, w.title);
        for (int i = 0; i < 2; ++i) {
            const View& v = cats[i].view();
            Pose p = v.transition ? transitionPose(looks[i], v.from, v.to, v.frame, v.ov) : actPose(looks[i], v.act, v.frame, v.param, v.ov);
            PixelImage im;
            renderPixelCat(looks[i], p, v.facingLeft, st, im);
            char key[64];
            std::snprintf(key, sizeof key, "c%d_%016llx_%d", i, (unsigned long long)poseKey(p), v.facingLeft ? 1 : 0);
            saveSprite(key, im);
            // Haltepunkt (Nullpunkt bzw. Nackenfell) auf die Position legen
            float ax = p.anchor.x * st.pxPerCm, ay = p.anchor.y * st.pxPerCm;
            float apx = im.originX + (v.facingLeft ? -ax : ax), apy = im.originY - ay;
            int left = (int)std::lround(v.x - apx * SCALE), top = (int)std::lround(v.y - apy * SCALE);
            std::fprintf(out, "cat %d %s %d %d %llu %d\n", i, key, left, top, (unsigned long long)v.platform, v.topmost ? 1 : 0);
        }
        if (bowl.active) {
            PixelImage im;
            renderBowl(bowl.food, im);
            std::string key = bowl.food > 0.55 ? "bowl_full" : (bowl.food > 0.05 ? "bowl_half" : "bowl_empty");
            saveSprite(key, im);
            std::fprintf(out, "item %s %d %d %llu\n", key.c_str(), (int)std::lround(bowl.x - im.originX * SCALE),
                         (int)std::lround(bowl.y - im.originY * SCALE), (unsigned long long)bowl.platform);
        }
        if (toy.active) {
            PixelImage im;
            float spin = std::round(toy.spin * 8.f) / 8.f;
            renderYarn(spin, im);
            char key[32];
            std::snprintf(key, sizeof key, "yarn_%d", (int)std::lround(spin * 8.f) & 7);
            saveSprite(key, im);
            std::fprintf(out, "item %s %d %d %llu\n", key, (int)std::lround(toy.x - im.originX * SCALE), (int)std::lround(toy.y - im.originY * SCALE),
                         toy.airborne ? 0ull : (unsigned long long)toy.platform);
        }
        std::fprintf(out, "cursor %d %d\n", cursorX, cursorY);
    }
    std::fclose(out);
    std::printf("Ticks: Fatos %d, Kosta %d (von %d Bildern), Sprites: %zu\n", ticks[0], ticks[1], (int)(T / dt), g_saved.size());
    return 0;
}
