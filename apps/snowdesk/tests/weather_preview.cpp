// Vorschau-Video "Echtes Wetter": Regen mit Tropfen auf den Kanten, Gewitter, Fenster ziehen, Schnee mit Tauwetter,
// Nebel. Software-Nachbau der Shader (softrender.h). Schreibt rohe RGB-Bilder nach stdout (für ffmpeg).
// Aufruf: ./weather_preview <breite> <höhe> <sekunden> [standbild-zeit standbild.ppm] | ffmpeg ...
#include "softrender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace snow;
using namespace soft;

struct Win { uint64_t id; float x, y, w, h; bool visible; int style; };

static void fillRoundRect(Image& img, float x, float y, float w, float h, float rad, float r, float g, float b, float a) {
    for (int yy = std::max(0, (int)y); yy < std::min(img.h, (int)std::ceil(y + h)); ++yy) {
        for (int xx = std::max(0, (int)x); xx < std::min(img.w, (int)std::ceil(x + w)); ++xx) {
            float px = xx + 0.5f, py = yy + 0.5f;
            float cx = clampf(px, x + rad, x + w - rad), cy = clampf(py, y + rad, y + h - rad);
            float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy)) - rad;
            float cov = clampf(0.5f - d, 0.0f, 1.0f);
            if (cov > 0) blend(img, xx, yy, r, g, b, a * cov);
        }
    }
}

static void drawWindow(Image& img, const Win& wdw, float s) {
    for (int i = 6; i >= 1; --i) fillRoundRect(img, wdw.x - i, wdw.y - i * 0.5f + 3, wdw.w + 2 * i, wdw.h + 2 * i, 8 * s + i, 0, 0, 0, 0.035f);
    fillRoundRect(img, wdw.x, wdw.y, wdw.w, wdw.h, 8 * s, 0.23f, 0.23f, 0.24f, 1.0f);
    fillRoundRect(img, wdw.x + 1, wdw.y + 1, wdw.w - 2, wdw.h - 2, 8 * s - 1, 0.125f, 0.125f, 0.13f, 1.0f);
    fillRoundRect(img, wdw.x + 1, wdw.y + 33 * s, wdw.w - 2, wdw.h - 34 * s, 8 * s - 1, 0.098f, 0.098f, 0.105f, 1.0f);
    for (int row = 0; row < 14; ++row) {
        float ly = wdw.y + 52 * s + row * 22 * s;
        if (ly > wdw.y + wdw.h - 20 * s) break;
        float len = (wdw.style == 0 ? 0.55f : 0.8f) * (wdw.w - 60 * s) * (0.5f + 0.5f * std::fabs(std::sin(row * 1.7f + wdw.id)));
        fillRoundRect(img, wdw.x + 24 * s, ly, len, 8 * s, 3 * s, 0.26f, 0.27f, 0.29f, 1.0f);
    }
}

static void drawTaskbar(Image& img, float top) {
    for (int y = (int)top; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) blend(img, x, y, 0.11f, 0.12f, 0.14f, 0.92f);
    for (int x = 0; x < img.w; ++x) blend(img, x, (int)top, 0.28f, 0.29f, 0.31f, 0.8f);
}

static void makeBackground(Image& bg) {
    for (int y = 0; y < bg.h; ++y) {
        for (int x = 0; x < bg.w; ++x) {
            float t = (float)y / bg.h, u = (float)x / bg.w;
            float r = 0.05f + 0.06f * t, g = 0.08f + 0.12f * t, b = 0.13f + 0.14f * t;
            float glow = std::exp(-((u - 0.35f) * (u - 0.35f) * 5 + (t - 0.8f) * (t - 0.8f) * 7));
            r += 0.05f * glow; g += 0.07f * glow; b += 0.10f * glow;
            bg.at(x, y) = RGBA{r, g, b, 1};
        }
    }
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 720;
    float seconds = argc > 3 ? (float)atof(argv[3]) : 42.0f;
    float stillAt = argc > 5 ? (float)atof(argv[4]) : -1.0f;
    const char* stillPath = argc > 5 ? argv[5] : nullptr;
    const bool video = !(argc > 6 && strcmp(argv[6], "novideo") == 0);
    float scale = W / 1600.0f * 1.6f;   // groß genug, dass man die Tropfen sieht
    const int fps = 30;

    Image bg;
    bg.init(W, H);
    makeBackground(bg);
    float taskTop = H - 48 * scale * 0.7f;

    World world(77);
    world.configure(W, H, scale);
    Params p;
    p.amount = 0.0f;
    p.rain = 0.0f;
    p.wind = 0.35f;
    p.maxPileDip = 22.0f;
    p.fillMinutes = 0.15f;
    world.setParams(p);

    std::vector<Win> wins = {
        {10, 0.07f * W, 0.40f * H, 0.38f * W, 0.42f * H, true, 0},
        {20, 0.52f * W, 0.22f * H, 0.40f * W, 0.48f * H, true, 1},
    };
    auto platforms = [&]() {
        std::vector<PlatformIn> pl;
        for (const Win& w : wins) {
            if (!w.visible) continue;
            PlatformIn q;
            q.id = w.id;
            q.left = w.x;
            q.top = w.y;
            q.right = w.x + w.w;
            q.corner = 8 * scale;
            q.floor = false;
            pl.push_back(q);
        }
        PlatformIn f;
        f.id = 1;
        f.left = 0;
        f.top = taskTop;
        f.right = (float)W;
        f.floor = true;
        pl.push_back(f);
        return pl;
    };
    world.setPlatforms(platforms());

    Image layer, frame;
    std::vector<Sprite> sprites;
    std::vector<PileVertex> mesh;
    std::vector<unsigned char> rgb((size_t)W * H * 3);
    const int frames = (int)(seconds * fps);
    int phase = -1;
    for (int fi = 0; fi < frames; ++fi) {
        float t = fi / (float)fps;
        // Drehbuch
        int ph = t < 13 ? 0 : t < 19 ? 1 : t < 25 ? 2 : t < 30 ? 3 : t < 36 ? 4 : 5;
        if (ph != phase) {
            phase = ph;
            Params q = p;
            if (ph == 0) { q.rain = 1.0f; q.wind = 0.35f; }
            if (ph == 1) { q.rain = 2.4f; q.wind = 0.6f; q.thunder = true; }
            if (ph == 2) { q.rain = 0.0f; q.wind = 0.15f; q.thunder = false; }
            if (ph == 3) { q.rain = 0.0f; q.amount = 1.6f; q.wind = -0.25f; }
            if (ph == 4) { q.amount = 0.0f; q.meltDipPerMin = 90.0f; q.wind = -0.1f; }
            if (ph == 5) { q.amount = 0.0f; q.meltDipPerMin = 90.0f; q.fog = 0.85f; q.wind = 0.2f; }
            world.setParams(q);
            if (ph == 1) world.flashNow();
        }
        if (std::fabs(t - 16.6f) < 0.5f / fps) world.flashNow();
        if (t >= 22.0f && t < 23.0f) {   // Browser wird gezogen: die Tropfen fallen
            float k = (t - 22.0f);
            k = k * k * (3 - 2 * k);
            wins[1].x = (0.52f + 0.04f * k) * W;
            wins[1].y = (0.22f + 0.06f * k) * H;
        }
        world.setPlatforms(platforms());
        world.step(1.0f / fps);

        layer.init(W, H);
        world.buildPileMesh(mesh);
        for (size_t i = 0; i + 2 < mesh.size(); i += 3) drawPileTriangle(layer, mesh[i], mesh[i + 1], mesh[i + 2], scale);
        world.collectSprites(sprites);
        for (const Sprite& s : sprites) drawSprite(layer, s);
        frame = bg;
        for (size_t i = 0; i < frame.px.size(); ++i) {
            RGBA& d = frame.px[i];
            const RGBA& s = layer.px[i];
            d.r = s.r + d.r * (1 - s.a);
            d.g = s.g + d.g * (1 - s.a);
            d.b = s.b + d.b * (1 - s.a);
        }
        for (int i = (int)wins.size() - 1; i >= 0; --i)
            if (wins[i].visible) drawWindow(frame, wins[i], scale);
        drawTaskbar(frame, taskTop);
        for (size_t i = 0; i < frame.px.size(); ++i) {
            auto b = [](float v) { return (unsigned char)std::lround(clampf(v, 0.0f, 1.0f) * 255.0f); };
            rgb[i * 3 + 0] = b(frame.px[i].r);
            rgb[i * 3 + 1] = b(frame.px[i].g);
            rgb[i * 3 + 2] = b(frame.px[i].b);
        }
        if (video) fwrite(rgb.data(), 1, rgb.size(), stdout);
        if (stillPath && fi == (int)(stillAt * fps)) {
            FILE* f = fopen(stillPath, "wb");
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            fwrite(rgb.data(), 1, rgb.size(), f);
            fclose(f);
            Stats st = world.stats();
            fprintf(stderr, "t=%.1f rain=%d drops=%d splashes=%d beads=%d hanging=%d water=%.2f fog=%d hint=%.0f\n", t, st.rain, st.drops,
                    st.splashes, st.beads, st.hanging, st.water, st.fog, world.frameRateHint());
            if (!video) return 0;
        }
    }
    return 0;
}
