// Vorschau-Video: Software-Nachbau der GPU-Shader + Windows-11-artige Szene. Schreibt rohe RGB-Bilder nach stdout
// (für ffmpeg). Aufruf: ./preview <breite> <höhe> <sekunden> [poster.ppm] | ffmpeg ...
#include "softrender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace snow;

using namespace soft;

// ---------------------------------------------------------------- Szene

struct Win { uint64_t id; float x, y, w, h; bool visible; const char* title; int style; };

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

static void drawWindow(Image& img, const Win& wdw) {
    // Schatten
    for (int i = 6; i >= 1; --i) fillRoundRect(img, wdw.x - i, wdw.y - i * 0.5f + 3, wdw.w + 2 * i, wdw.h + 2 * i, 8 + i, 0, 0, 0, 0.035f);
    fillRoundRect(img, wdw.x, wdw.y, wdw.w, wdw.h, 8, 0.23f, 0.23f, 0.24f, 1.0f);          // Rand
    fillRoundRect(img, wdw.x + 1, wdw.y + 1, wdw.w - 2, wdw.h - 2, 7, 0.125f, 0.125f, 0.13f, 1.0f);
    fillRoundRect(img, wdw.x + 1, wdw.y + 33, wdw.w - 2, wdw.h - 34, 7, 0.098f, 0.098f, 0.105f, 1.0f);
    // Titelleisten-Knöpfe
    float bx = wdw.x + wdw.w - 46 * 3;
    for (int i = 0; i < 3; ++i) {
        float cx = bx + 46 * i + 23, cy = wdw.y + 16;
        for (int dx = -5; dx <= 5; ++dx) blend(img, (int)(cx + dx), (int)cy, 0.8f, 0.8f, 0.82f, i == 1 ? 0.0f : 0.8f);
    }
    // Inhalt: ein paar Zeilen/Kacheln
    for (int row = 0; row < 12; ++row) {
        float ly = wdw.y + 52 + row * 22;
        if (ly > wdw.y + wdw.h - 20) break;
        float len = (wdw.style == 0 ? 0.55f : 0.8f) * (wdw.w - 60) * (0.5f + 0.5f * std::fabs(std::sin(row * 1.7f + wdw.id)));
        fillRoundRect(img, wdw.x + 24, ly, len, 8, 3, 0.26f, 0.27f, 0.29f, 1.0f);
    }
    if (wdw.style == 1) fillRoundRect(img, wdw.x + 12, wdw.y + 40, wdw.w - 24, 26, 13, 0.17f, 0.17f, 0.18f, 1.0f);   // "Adressleiste"
}

static void drawTaskbar(Image& img, float top) {
    for (int y = (int)top; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) blend(img, x, y, 0.11f, 0.12f, 0.14f, 0.92f);
    for (int x = 0; x < img.w; ++x) blend(img, x, (int)top, 0.28f, 0.29f, 0.31f, 0.8f);
    float cx = img.w * 0.5f - 5 * 22;
    float colors[6][3] = {{0.25f, 0.55f, 0.95f}, {0.95f, 0.75f, 0.2f}, {0.35f, 0.4f, 0.95f}, {0.15f, 0.8f, 0.45f}, {0.9f, 0.25f, 0.35f}, {0.6f, 0.6f, 0.65f}};
    for (int i = 0; i < 6; ++i) fillRoundRect(img, cx + i * 44, top + 12, 24, 24, 5, colors[i][0], colors[i][1], colors[i][2], 1.0f);
}

static void makeBackground(Image& bg) {
    for (int y = 0; y < bg.h; ++y) {
        for (int x = 0; x < bg.w; ++x) {
            float t = (float)y / bg.h, u = (float)x / bg.w;
            float r = 0.03f + 0.05f * t, g = 0.10f + 0.18f * t, b = 0.20f + 0.20f * t;
            float glow = std::exp(-((u - 0.6f) * (u - 0.6f) * 6 + (t - 0.75f) * (t - 0.75f) * 8));
            r += 0.02f * glow; g += 0.16f * glow; b += 0.18f * glow;
            bg.at(x, y) = RGBA{r, g, b, 1};
        }
    }
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 720;
    float seconds = argc > 3 ? (float)atof(argv[3]) : 30.0f;
    const char* poster = argc > 4 ? argv[4] : nullptr;
    float scale = W / 1600.0f;
    const int fps = 30;

    Image bg;
    bg.init(W, H);
    makeBackground(bg);
    float taskTop = H - 48 * scale;

    World world(2024);
    world.configure(W, H, scale);
    Params p;
    p.amount = 1.25f;
    p.wind = 0.3f;
    p.fillMinutes = 0.5f;        // Vorschau: in ~25 s voll (echt: einstellbar, Standard 6 Minuten)
    p.maxPileDip = 30.0f;
    p.crystalShare = 0.09f;
    world.setParams(p);

    std::vector<Win> wins = {
        {10, 130 * scale, 300 * scale, 560 * scale, 380 * scale, true, "Explorer", 0},
        {20, 780 * scale, 190 * scale, 640 * scale, 420 * scale, true, "Browser", 1},
        {30, 600 * scale, 560 * scale, 300 * scale, 210 * scale, true, "Dialog", 0},
    };

    auto platforms = [&]() {
        std::vector<PlatformIn> pl;
        for (const Win& w : wins) {
            if (!w.visible) continue;
            PlatformIn q;
            q.id = w.id; q.left = w.x; q.top = w.y; q.right = w.x + w.w; q.corner = 8 * scale; q.floor = false;
            pl.push_back(q);
        }
        PlatformIn f;
        f.id = 1; f.left = 0; f.top = taskTop; f.right = (float)W; f.floor = true;
        pl.push_back(f);
        return pl;
    };
    world.setPlatforms(platforms());
    world.prefill();

    Image snowLayer, frame;
    std::vector<Sprite> sprites;
    std::vector<PileVertex> mesh;
    std::vector<unsigned char> rgb((size_t)W * H * 3);
    int frames = (int)(seconds * fps);
    for (int fIdx = 0; fIdx < frames; ++fIdx) {
        float t = fIdx / (float)fps;
        // Drehbuch: bei 30 s wird der Browser gezogen, bei 36 s das Explorer-Fenster minimiert
        if (t >= 30.0f && t < 31.3f) {
            float k = (t - 30.0f) / 1.3f;
            k = k * k * (3 - 2 * k);
            wins[1].x = (780 + 150 * k) * scale;
            wins[1].y = (190 + 70 * k) * scale;
        }
        if (t >= 36.0f) wins[0].visible = false;
        world.setPlatforms(platforms());
        world.step(1.0f / fps);

        snowLayer.init(W, H);
        world.buildPileMesh(mesh);
        for (size_t i = 0; i + 2 < mesh.size(); i += 3) drawPileTriangle(snowLayer, mesh[i], mesh[i + 1], mesh[i + 2], scale);
        world.collectSprites(sprites);
        for (const Sprite& s : sprites) drawSprite(snowLayer, s);

        frame = bg;
        for (size_t i = 0; i < frame.px.size(); ++i) {
            RGBA& d = frame.px[i];
            const RGBA& s = snowLayer.px[i];
            d.r = s.r + d.r * (1 - s.a);
            d.g = s.g + d.g * (1 - s.a);
            d.b = s.b + d.b * (1 - s.a);
        }
        // Fenster liegen VOR dem Schnee (Schnee gehört zum Hintergrund), dann die Taskleiste
        for (int i = (int)wins.size() - 1; i >= 0; --i) {
            if (wins[i].visible) drawWindow(frame, wins[i]);
        }
        drawTaskbar(frame, taskTop);

        for (size_t i = 0; i < frame.px.size(); ++i) {
            auto toByte = [](float v) {
                v = clampf(v, 0.0f, 1.0f);
                v = std::pow(v, 1.0f / 1.0f);
                return (unsigned char)std::lround(v * 255.0f);
            };
            rgb[i * 3 + 0] = toByte(frame.px[i].r);
            rgb[i * 3 + 1] = toByte(frame.px[i].g);
            rgb[i * 3 + 2] = toByte(frame.px[i].b);
        }
        fwrite(rgb.data(), 1, rgb.size(), stdout);
        if (poster && fIdx == (int)(29.0f * fps)) {
            FILE* f = fopen(poster, "wb");
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            fwrite(rgb.data(), 1, rgb.size(), f);
            fclose(f);
        }
    }
    return 0;
}
