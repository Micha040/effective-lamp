// Prototyp: Katzen-Rig in kleiner Auflösung als Pixel Art (harte Kanten, Farbstufen, Kontur)
#include "../src/core/cat.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace cat;

struct Img {
    int w, h;
    std::vector<float> c;   // rgba linear-ish (sRGB) 0..1
};

static Img renderPix(const Look& lk, const Pose& p, bool left, float pxPerCm) {
    Box b = poseBounds(lk, p);
    float x0 = (left ? -b.x1 : b.x0) - 2.f, x1 = (left ? -b.x0 : b.x1) + 2.f;
    float y0 = b.y0 - 6.f, y1 = b.y1 + 2.f;
    Img im;
    im.w = (int)std::ceil((x1 - x0) * pxPerCm);
    im.h = (int)std::ceil((y1 - y0) * pxPerCm);
    // Fußboden (y=0) genau auf eine Pixelkante legen
    float oy = std::ceil(y1 * pxPerCm) / pxPerCm;
    CatCB cb;
    buildCB(lk, p, left, x0, oy, pxPerCm, 0.f, 1.f, cb);
    cb.shadow2.y = 1.f;
    im.c.resize((size_t)im.w * im.h * 4);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            hlsl::float4 v = shadePixel(cb, (float)x, (float)y);
            float* o = &im.c[(y * im.w + x) * 4];
            float a = v.w;
            o[0] = a > 0 ? v.x / a : 0;
            o[1] = a > 0 ? v.y / a : 0;
            o[2] = a > 0 ? v.z / a : 0;
            o[3] = a > 0.5f ? 1.f : 0.f;
        }
    return im;
}

// Farben auf eine kleine Palette ziehen + Kontur außen (dunklere Variante der Nachbarfarbe)
static void pixelize(Img& im, const std::vector<std::array<int, 3>>& pal) {
    for (size_t i = 0; i < (size_t)im.w * im.h; ++i) {
        float* o = &im.c[i * 4];
        if (o[3] < 0.5f) continue;
        int best = 0;
        float bd = 1e9f;
        for (size_t k = 0; k < pal.size(); ++k) {
            float dr = o[0] * 255 - pal[k][0], dg = o[1] * 255 - pal[k][1], db = o[2] * 255 - pal[k][2];
            float d = dr * dr * 0.3f + dg * dg * 0.59f + db * db * 0.11f;
            if (d < bd) {
                bd = d;
                best = (int)k;
            }
        }
        o[0] = pal[best][0] / 255.f;
        o[1] = pal[best][1] / 255.f;
        o[2] = pal[best][2] / 255.f;
    }
    // Kontur: innerste Randpixel abdunkeln
    std::vector<float> src = im.c;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* s = &src[(y * im.w + x) * 4];
            if (s[3] < 0.5f) continue;
            bool edge = false;
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int X = x + dx[k], Y = y + dy[k];
                if (X < 0 || Y < 0 || X >= im.w || Y >= im.h || src[(Y * im.w + X) * 4 + 3] < 0.5f) edge = true;
            }
            if (edge) {
                float* o = &im.c[(y * im.w + x) * 4];
                o[0] = s[0] * 0.38f;
                o[1] = s[1] * 0.32f;
                o[2] = s[2] * 0.3f;
            }
        }
}

#include <array>
int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "out";
    float ppc = argc > 2 ? std::atof(argv[2]) : 1.2f;
    int scale = 6;
    Look fat = lookFatos(), kos = lookKosta();
    std::vector<std::array<int, 3>> pf = {{150, 72, 30}, {186, 98, 43}, {214, 124, 58}, {229, 146, 76}, {244, 185, 120},
                                           {251, 228, 196}, {226, 190, 152}, {205, 178, 74}, {36, 24, 18}, {255, 255, 255},
                                           {232, 140, 140}, {236, 168, 160}, {120, 100, 40}};
    std::vector<std::array<int, 3>> pk = {{12, 12, 16}, {24, 24, 30}, {38, 38, 46}, {58, 60, 74}, {84, 88, 110},
                                           {246, 243, 236}, {206, 202, 200}, {166, 206, 60}, {90, 130, 40}, {255, 255, 255},
                                           {70, 56, 62}, {170, 128, 136}};
    struct Item {
        const Look* lk;
        Pose p;
        bool left;
        const std::vector<std::array<int, 3>>* pal;
    };
    std::vector<Item> items = {{&fat, poseStand(fat), false, &pf}, {&kos, poseSit(kos), true, &pk}, {&fat, poseSit(fat), false, &pf},
                               {&kos, poseStand(kos), true, &pk}, {&fat, poseLoaf(fat, 1.f), false, &pf}, {&kos, poseSleep(kos, 0.f), false, &pk}};
    // nebeneinander auf Boden setzen
    std::vector<Img> ims;
    int W = 8, H = 0;
    for (auto& it : items) {
        Img im = renderPix(*it.lk, it.p, it.left, ppc);
        pixelize(im, *it.pal);
        W += im.w + 6;
        H = std::max(H, im.h);
        ims.push_back(im);
    }
    H += 12;
    std::vector<unsigned char> rgb((size_t)W * scale * H * scale * 3);
    for (int y = 0; y < H * scale; ++y)
        for (int x = 0; x < W * scale; ++x) {
            unsigned char* o = &rgb[((size_t)y * W * scale + x) * 3];
            bool ground = y >= (H - 10) * scale;
            o[0] = ground ? 236 : 112;
            o[1] = ground ? 236 : 150;
            o[2] = ground ? 236 : 190;
        }
    int ox = 4;
    for (auto& im : ims) {
        int oy = (H - 10) - im.h + (int)std::round(6.f * ppc) - 0;   // Unterkante 6 cm Rand
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                const float* s = &im.c[(y * im.w + x) * 4];
                if (s[3] < 0.5f) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        int X = (ox + x) * scale + sx, Y = (oy + y) * scale + sy;
                        if (X < 0 || Y < 0 || X >= W * scale || Y >= H * scale) continue;
                        unsigned char* o = &rgb[((size_t)Y * W * scale + X) * 3];
                        o[0] = (unsigned char)(s[0] * 255);
                        o[1] = (unsigned char)(s[1] * 255);
                        o[2] = (unsigned char)(s[2] * 255);
                    }
            }
        ox += im.w + 6;
    }
    FILE* f = std::fopen((out + "/pixproto.ppm").c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", W * scale, H * scale);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    return 0;
}
