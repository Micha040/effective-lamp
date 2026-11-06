#include "../src/core/pixcat.h"
#include <chrono>
#include <cstdio>
using namespace cat;
int main() {
    Look lk = lookFatos();
    Pose ps[4] = {poseStand(lk), poseSit(lk), poseLoaf(lk, 1.f), poseSleep(lk, 0.f)};
    PixelStyle st; PixelImage im;
    for (int k = 0; k < 4; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) renderPixelCat(lk, ps[k], i & 1, st, im);
        auto t1 = std::chrono::steady_clock::now();
        std::printf("pose %d: %dx%d  %.3f ms/frame\n", k, im.w, im.h, std::chrono::duration<double, std::milli>(t1 - t0).count() / 200);
    }
}
