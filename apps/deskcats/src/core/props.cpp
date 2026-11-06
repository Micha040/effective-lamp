// DeskCats – Napf und Knäuel (siehe props.h)
#include "props.h"

#include <cmath>
#include <cstring>

namespace cat {

namespace {

constexpr uint32_t rgb(unsigned v) { return 0xFF000000u | v; }

void fromRows(const char* const* rows, int h, uint32_t (*color)(char), PixelImage& out) {
    int w = (int)std::strlen(rows[0]);
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) out.px[(size_t)y * w + x] = color(rows[y][x]);
    out.originX = w / 2;
    out.originY = h;
}

uint32_t bowlColor(char c) {
    switch (c) {
        case 'o': return rgb(0x3B1E2A);   // Kontur
        case 'l': return rgb(0xF08C9A);   // Glanz am Rand
        case 'r': return rgb(0xD0506A);   // Napf
        case 'd': return rgb(0xA23852);   // Schatten unten
        case 'i': return rgb(0x6E2A3C);   // Innenseite (leer)
        case 'c': return rgb(0x8A552A);   // Trockenfutter
        case 'K': return rgb(0xB9803F);   // Trockenfutter hell
        case 'p': return rgb(0xF6C4CC);   // Pfötchen-Aufdruck
        default: return 0;
    }
}

}  // namespace

void renderBowl(double food, PixelImage& out) {
    static const char* const full[] = {
        "....cKc.cKc.....",
        "...cKcKcKccKc...",
        "..cKccKcKcKccc..",
        "oooooooooooooooo",
        "ollllllllllllllo",
        ".orrrrpprrrrrro.",
        "..orrrrrrrrrro..",
        "...oddddddddo...",
        "....oooooooo....",
    };
    static const char* const half[] = {
        "................",
        "................",
        "....cK.cKcKc....",
        "oooooooooooooooo",
        "ollllllllllllllo",
        ".orrrrpprrrrrro.",
        "..orrrrrrrrrro..",
        "...oddddddddo...",
        "....oooooooo....",
    };
    static const char* const empty[] = {
        "................",
        "................",
        "................",
        "oooooooooooooooo",
        "olliiiiiiiiiillo",
        ".orrrrpprrrrrro.",
        "..orrrrrrrrrro..",
        "...oddddddddo...",
        "....oooooooo....",
    };
    const char* const* rows = food > 0.55 ? full : (food > 0.05 ? half : empty);
    fromRows(rows, 9, bowlColor, out);
}

void renderYarn(float spin, PixelImage& out) {
    // Kreis (Radius 4,5 px), Fäden als schräge Streifen, die sich mit der Drehung verschieben
    const int W = 11, H = 10;
    out.w = W;
    out.h = H;
    out.px.assign((size_t)W * H, 0);
    const uint32_t outline = rgb(0x4A1C26), base = rgb(0xD4524E), dark = rgb(0xA33838), light = rgb(0xF2877C), shine = rgb(0xFFD2C8);
    const float cx = 5.f, cy = 4.5f, r = 4.6f;
    float a = spin * 6.2832f;
    float ca = std::cos(a), sa = std::sin(a);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < W; ++x) {
            float dx = x + 0.5f - cx - 0.5f, dy = y + 0.5f - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > r) continue;
            uint32_t col;
            if (d > r - 1.f) col = outline;
            else {
                // Fadenlinien in gedrehtem Koordinatensystem
                float u = dx * ca - dy * sa, v = dx * sa + dy * ca;
                float s1 = std::sin(u * 1.9f + v * 0.6f), s2 = std::sin(v * 2.1f - u * 0.4f);
                col = s1 > 0.55f || s2 > 0.75f ? dark : base;
                if (dx + dy < -2.6f) col = s1 > 0.55f ? base : light;
                if (dx > -2.6f && dx < -1.4f && dy > -2.6f && dy < -1.4f) col = shine;
            }
            out.px[(size_t)y * W + x] = col;
        }
    // loses Fadenende
    const int tail[][2] = {{9, 8}, {10, 9}};
    for (auto& t : tail) out.px[(size_t)t[1] * W + t[0]] = dark;
    out.px[(size_t)9 * W + 9] = dark;
    out.originX = 5;
    out.originY = 9;
}

}  // namespace cat
