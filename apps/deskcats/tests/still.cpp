// Standbilder der Katzen (CPU-Shader) -> PPM, zusammengesetzt auf einen Hintergrund
#include "../src/core/cat.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace cat;

static void writePPM(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

// Hintergrund: oben "Fenster-Titelleiste" (hell oder dunkel), darüber Wallpaper-Verlauf
static void background(std::vector<unsigned char>& rgb, int w, int h, int groundY, bool dark) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* o = &rgb[(y * w + x) * 3];
            if (y >= groundY) {
                int v = dark ? 40 : 238;
                o[0] = o[1] = o[2] = (unsigned char)v;
                if (y == groundY) o[0] = o[1] = o[2] = (unsigned char)(dark ? 70 : 205);
            } else {
                float t = (float)y / groundY;
                float s = (float)x / w;
                o[0] = (unsigned char)(dark ? 30 + 30 * s : 120 + 60 * s);
                o[1] = (unsigned char)(dark ? 38 + 20 * t : 150 + 40 * t);
                o[2] = (unsigned char)(dark ? 60 + 30 * t : 190 + 30 * t);
            }
        }
}

static void place(std::vector<unsigned char>& rgb, int W, int H, const Look& lk, const Pose& p, bool left, float pxPerCm,
                  int footX, int groundY) {
    Box b = poseBounds(lk, p);
    // Fenster so wählen, dass die Katze reinpasst
    float mx = 4.f, my = 4.f;
    float x0 = (left ? -b.x1 : b.x0) - mx, x1 = (left ? -b.x0 : b.x1) + mx;
    float y0 = b.y0 - my, y1 = b.y1 + my;
    int w = (int)((x1 - x0) * pxPerCm) + 1, h = (int)((y1 - y0) * pxPerCm) + 1;
    CatCB cb;
    buildCB(lk, p, left, x0, y1, pxPerCm, 0.f, 1.f, cb);
    std::vector<unsigned char> img((size_t)w * h * 4);
    renderCpu(cb, w, h, img.data());
    // Katzen-Koordinate (0,0) liegt im Bild bei px = -x0*pxPerCm, py = y1*pxPerCm
    int ox = footX - (int)(-x0 * pxPerCm), oy = groundY - (int)(y1 * pxPerCm);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int X = ox + x, Y = oy + y;
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            const unsigned char* s = &img[(y * w + x) * 4];
            unsigned char* d = &rgb[(Y * W + X) * 3];
            float a = s[3] / 255.f;
            for (int c = 0; c < 3; ++c) d[c] = (unsigned char)(s[c] + d[c] * (1.f - a) + 0.5f);
        }
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "out";
    float scale = argc > 2 ? std::atof(argv[2]) : 5.f;
    Look fat = lookFatos(), kos = lookKosta();
    for (int dark = 0; dark < 2; ++dark) {
        int W = (int)(150 * scale), H = (int)(60 * scale);
        std::vector<unsigned char> rgb((size_t)W * H * 3);
        int ground = (int)(46 * scale);
        background(rgb, W, H, ground, dark == 1);
        place(rgb, W, H, fat, poseStand(fat), false, scale, (int)(22 * scale), ground);
        place(rgb, W, H, kos, poseSit(kos), true, scale, (int)(62 * scale), ground);
        place(rgb, W, H, fat, poseSit(fat), false, scale, (int)(88 * scale), ground);
        place(rgb, W, H, kos, poseStand(kos), true, scale, (int)(126 * scale), ground);
        writePPM(out + (dark ? "/still-dark.ppm" : "/still-light.ppm"), W, H, rgb);
        std::vector<unsigned char> rgb2((size_t)W * H * 3);
        background(rgb2, W, H, ground, dark == 1);
        place(rgb2, W, H, fat, poseLoaf(fat, 1.f), false, scale, (int)(30 * scale), ground);
        place(rgb2, W, H, kos, poseSleep(kos, 0.f), false, scale, (int)(80 * scale), ground);
        place(rgb2, W, H, fat, poseSleep(fat, 0.f), true, scale, (int)(118 * scale), ground);
        writePPM(out + (dark ? "/still2-dark.ppm" : "/still2-light.ppm"), W, H, rgb2);
    }
    // realistische Größe (wie am Bildschirm, 2.2 px/cm) – alle Haltungen nebeneinander
    {
        float sc = 2.2f;
        int W = (int)(260 * sc), H = (int)(60 * sc);
        for (int dark = 0; dark < 2; ++dark) {
            std::vector<unsigned char> rgb((size_t)W * H * 3);
            int ground = (int)(46 * sc);
            background(rgb, W, H, ground, dark == 1);
            place(rgb, W, H, fat, poseStand(fat), false, sc, (int)(35 * sc), ground);
            place(rgb, W, H, kos, poseSit(kos), true, sc, (int)(70 * sc), ground);
            place(rgb, W, H, fat, poseSit(fat), false, sc, (int)(98 * sc), ground);
            place(rgb, W, H, kos, poseStand(kos), true, sc, (int)(135 * sc), ground);
            place(rgb, W, H, fat, poseLoaf(fat, 1.f), false, sc, (int)(185 * sc), ground);
            place(rgb, W, H, kos, poseSleep(kos, 0.f), false, sc, (int)(235 * sc), ground);
            writePPM(out + (dark ? "/real-dark.ppm" : "/real-light.ppm"), W, H, rgb);
        }
    }
    std::printf("ok\n");
    return 0;
}
