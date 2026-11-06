#include "../src/core/pixcat.h"
#include <cstdio>
#include <cstring>
using namespace cat;
int main(int argc, char** argv) {
    Look lk = std::strcmp(argv[1], "kosta") == 0 ? lookKosta() : lookFatos();
    Pose p = std::strcmp(argv[2], "sleep") == 0 ? poseSleep(lk, 0.f) : std::strcmp(argv[2], "sit") == 0 ? poseSit(lk) : std::strcmp(argv[2], "loaf") == 0 ? poseLoaf(lk, 1.f) : poseStand(lk);
    PixelImage im; PixelStyle st;
    renderPixelCat(lk, p, false, st, im);
    return 0;
}
