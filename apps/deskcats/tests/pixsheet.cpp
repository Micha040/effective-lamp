// Grundposen beider Katzen als Pixel-Sprites -> out/px/<name>.raw (Kopfzeile "w h ox oy", dann RGBA)
#include "../src/core/pixcat.h"

#include <cstdio>
#include <string>

using namespace cat;

static void save(const std::string& name, const PixelImage& im) {
    std::string path = "out/px/" + name + ".raw";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "%d %d %d %d\n", im.w, im.h, im.originX, im.originY);
    for (uint32_t v : im.px) {
        unsigned char b[4] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, (unsigned char)(v >> 24)};
        std::fwrite(b, 1, 4, f);
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    float ppc = argc > 1 ? (float)std::atof(argv[1]) : 1.25f;
    PixelStyle st;
    st.pxPerCm = ppc;
    Look looks[2] = {lookFatos(), lookKosta()};
    const char* names[2] = {"fatos", "kosta"};
    for (int k = 0; k < 2; ++k) {
        const Look& lk = looks[k];
        PixelImage im;
        renderPixelCat(lk, poseStand(lk), false, st, im);
        save(std::string(names[k]) + "-stand", im);
        renderPixelCat(lk, poseSit(lk), false, st, im);
        save(std::string(names[k]) + "-sit", im);
        renderPixelCat(lk, poseLoaf(lk, 1.f), false, st, im);
        save(std::string(names[k]) + "-loaf", im);
        renderPixelCat(lk, poseSleep(lk, 0.f), false, st, im);
        save(std::string(names[k]) + "-sleep", im);
    }
    return 0;
}
