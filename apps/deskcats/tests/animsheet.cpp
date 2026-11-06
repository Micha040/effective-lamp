// Einzelbilder einer Handlung als Filmstreifen -> out/px/anim-<katze>-<handlung>-<bild>.raw
#include "../src/core/anim.h"
#include "../src/core/pixcat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace cat;

static void save(const std::string& name, const PixelImage& im) {
    FILE* f = std::fopen(("out/px/" + name + ".raw").c_str(), "wb");
    std::fprintf(f, "%d %d %d %d\n", im.w, im.h, im.originX, im.originY);
    for (uint32_t v : im.px) {
        unsigned char b[4] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, (unsigned char)(v >> 24)};
        std::fwrite(b, 1, 4, f);
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const char* names[] = {"stand", "walk", "run", "sit", "loaf", "dangle", "sleep", "groom", "stretch", "crouch", "leap", "fall",
                           "land", "carried", "eat", "wiggle", "pounce", "bat", "petted", "yawn"};
    Look lk = (argc > 1 && std::strcmp(argv[1], "kosta") == 0) ? lookKosta() : lookFatos();
    const char* which = argc > 2 ? argv[2] : "walk";
    int act = -1;
    for (int i = 0; i < (int)Act::Count; ++i)
        if (std::strcmp(names[i], which) == 0) act = i;
    if (act < 0) return 1;
    int n = argc > 3 ? std::atoi(argv[3]) : actFrames((Act)act);
    PixelStyle st;
    for (int f = 0; f < n; ++f) {
        Overlay ov;
        float param = n > 1 ? (float)f / (n - 1) : 0.f;
        if ((Act)act == Act::Carried) param = std::sin(f * 0.8f);
        ov.breath = f / 8.f;
        Pose p = actPose(lk, (Act)act, f, param, ov);
        PixelImage im;
        renderPixelCat(lk, p, false, st, im);
        char nm[128];
        std::snprintf(nm, sizeof nm, "anim-%s-%s-%02d", argc > 1 ? argv[1] : "fatos", which, f);
        save(nm, im);
    }
    return 0;
}
