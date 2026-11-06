// Render-Test (Windows-Programm, läuft auch unter Wine): zeichnet dieselbe Szene einmal mit der echten
// D3D11-Pipeline (offscreen, HLSL aus src/app/shaders.h) und einmal mit dem Software-Nachbau (softrender.h)
// und vergleicht die Pixel. Schreibt gpu.ppm / cpu.ppm zum Ansehen.
#include "../src/app/renderer.h"

#include "softrender.h"

#include <cmath>
#include <cstdio>

using namespace snow;

static PlatformIn win(uint64_t id, float l, float t, float r) {
    PlatformIn p;
    p.id = id;
    p.left = l;
    p.top = t;
    p.right = r;
    p.corner = 8.0f * 0.8f;
    return p;
}

static void writePpm(const char* path, int w, int h, const std::vector<float>& rgbaPremul) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    const float bg[3] = {0.043f, 0.118f, 0.2f};
    for (int i = 0; i < w * h; ++i) {
        float a = rgbaPremul[i * 4 + 3];
        for (int c = 0; c < 3; ++c) {
            float v = rgbaPremul[i * 4 + c] + bg[c] * (1.0f - a);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            fputc((int)std::lround(v * 255.0f), f);
        }
    }
    fclose(f);
}

int wmain() {
    const int W = 1280, H = 720;
    const float scale = 0.8f;
    World w(2024);
    w.configure(W, H, scale);
    Params p;
    p.amount = 1.25f;
    p.wind = 0.3f;
    p.fillMinutes = 0.5f;
    p.maxPileDip = 30.0f;
    p.crystalShare = 0.15f;
    w.setParams(p);
    PlatformIn fl;
    fl.id = 1;
    fl.left = 0;
    fl.top = H - 48 * scale;
    fl.right = (float)W;
    fl.floor = true;
    w.setPlatforms({win(10, 104, 240, 552), win(20, 624, 152, 1136), win(30, 480, 448, 720), fl});
    w.prefill();
    for (int i = 0; i < 25 * 30; ++i) w.step(1.0f / 30.0f);
    // ein Fenster verschwindet: Klumpen, Brösel und Puder in der Luft
    w.setPlatforms({win(20, 624, 152, 1136), win(30, 480, 448, 720), fl});
    for (int i = 0; i < 12; ++i) w.step(1.0f / 30.0f);

    std::vector<Sprite> sprites;
    std::vector<PileVertex> mesh;
    w.collectSprites(sprites);
    w.buildPileMesh(mesh);
    int kinds[5] = {0, 0, 0, 0, 0};
    for (const Sprite& s : sprites) kinds[s.kind < 5 ? s.kind : 4]++;
    printf("Szene: %zu Sprites (Flocken %d, Kristalle %d, Klumpen %d, Brösel %d, Puder %d), %zu Dreiecks-Ecken\n", sprites.size(), kinds[0], kinds[1],
           kinds[2], kinds[3], kinds[4], mesh.size());

    app::Renderer r;
    if (!r.init(false)) {
        printf("FEHLER: D3D11 nicht verfügbar\n");
        return 2;
    }
    printf("Grafik: %ls%s\n", r.adapterName().c_str(), r.software() ? " (Software)" : "");
    std::vector<unsigned char> gpu;
    if (!r.renderOffscreen(W, H, sprites, mesh, scale, gpu)) {
        printf("FEHLER: Offscreen-Zeichnen fehlgeschlagen\n");
        return 3;
    }

    soft::Image img;
    img.init(W, H);
    for (size_t i = 0; i + 2 < mesh.size(); i += 3) soft::drawPileTriangle(img, mesh[i], mesh[i + 1], mesh[i + 2], scale);
    for (const Sprite& s : sprites) soft::drawSprite(img, s);

    std::vector<float> g((size_t)W * H * 4), c((size_t)W * H * 4);
    double sum = 0;
    int maxd = 0;
    long big = 0, covered = 0;
    for (int i = 0; i < W * H; ++i) {
        const unsigned char* px = &gpu[(size_t)i * 4];
        float gv[4] = {px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
        float cv[4] = {img.px[i].r, img.px[i].g, img.px[i].b, img.px[i].a};
        for (int k = 0; k < 4; ++k) {
            g[(size_t)i * 4 + k] = gv[k];
            c[(size_t)i * 4 + k] = cv[k];
            int d = std::abs((int)std::lround(gv[k] * 255) - (int)std::lround(cv[k] * 255));
            sum += d;
            if (d > maxd) maxd = d;
            if (k == 3 && d > 24) ++big;
        }
        if (cv[3] > 0.02f || gv[3] > 0.02f) ++covered;
    }
    double mean = sum / ((double)W * H * 4);
    double bigShare = covered ? (double)big / covered : 0.0;
    printf("Abweichung: Mittel %.3f/255, max %d/255, deutlich anders (Alpha > 24/255): %.2f %% der bedeckten Pixel (%ld)\n", mean, maxd,
           bigShare * 100.0, covered);
    writePpm("gpu.ppm", W, H, g);
    writePpm("cpu.ppm", W, H, c);
    bool ok = mean < 0.6 && bigShare < 0.01 && covered > 10000;
    printf(ok ? "OK: GPU-Shader entsprechen der Vorschau\n" : "FEHLER: GPU-Bild weicht ab\n");
    return ok ? 0 : 1;
}
