// Software-Nachbau der GPU-Shader (identische Formeln wie src/app/shaders.h). Für Vorschau-Video und Render-Test.
#pragma once
#include "../src/snowcore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace soft {
using namespace snow;

static const float kPi = 3.14159265f;

struct RGBA { float r, g, b, a; };

struct Image {
    int w = 0, h = 0;
    std::vector<RGBA> px;
    void init(int W, int H) { w = W; h = H; px.assign((size_t)W * H, RGBA{0, 0, 0, 0}); }
    RGBA& at(int x, int y) { return px[(size_t)y * w + x]; }
};

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float smoothstep(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// vormultipliziert "über" zeichnen
static inline void blend(Image& img, int x, int y, float r, float g, float b, float a) {
    if (x < 0 || y < 0 || x >= img.w || y >= img.h || a <= 0) return;
    RGBA& d = img.at(x, y);
    d.r = r * a + d.r * (1 - a);
    d.g = g * a + d.g * (1 - a);
    d.b = b * a + d.b * (1 - a);
    d.a = a + d.a * (1 - a);
}

// ---------------------------------------------------------------- Shader-Nachbau (identisch zu den HLSL-Formeln)

static float segDist(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return std::sqrt(dx * dx + dy * dy);
}

// Schneekristall: 6-fach symmetrisch, Arme mit Seitenästen. p in Einheiten des Radius (−1..1).
static float crystalCoverage(float x, float y, float pixel) {
    float r = std::sqrt(x * x + y * y);
    if (r > 1.05f) return 0;
    float ang = std::atan2(y, x);
    float sector = kPi / 3.0f;
    float a = std::fmod(ang + 2 * kPi, sector) - sector * 0.5f;
    float qx = r * std::cos(a), qy = std::fabs(r * std::sin(a));
    float c60 = 0.5f, s60 = 0.8660254f;
    float d = segDist(qx, qy, 0.0f, 0.0f, 0.92f, 0.0f) - 0.075f;
    d = std::min(d, segDist(qx, qy, 0.42f, 0.0f, 0.42f + 0.30f * c60, 0.30f * s60) - 0.055f);
    d = std::min(d, segDist(qx, qy, 0.66f, 0.0f, 0.66f + 0.18f * c60, 0.18f * s60) - 0.045f);
    d = std::min(d, r - 0.16f);
    return 1.0f - smoothstep(-pixel, pixel, d);
}

static inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }

// Regenspur, Tropfen, Nebel, Blitz (wie im Shader)
static bool drawWeatherSprite(Image& img, const Sprite& s) {
    if (s.kind == KindFlash) {
        for (int y = 0; y < img.h; ++y) {
            float cy = 1.0f - 2.0f * (y + 0.5f) / img.h;
            float a = s.alpha * (0.55f + 0.45f * (cy + 1.0f) * 0.5f);
            for (int x = 0; x < img.w; ++x) blend(img, x, y, 0.93f, 0.95f, 1.0f, a);
        }
        return true;
    }
    if (s.kind == KindRain) {
        float dx = std::sin(s.rot), dy = std::cos(s.rot);
        float nx = dy, ny = -dx;
        float halfW = std::max(s.soft, 0.6f) * 0.5f + 1.0f;
        float tx = s.x - dx * s.size, ty = s.y - dy * s.size;
        float len = s.size + 1.0f;
        float halfPx = std::max(s.soft * 0.5f, 0.3f);
        int x0 = (int)std::floor(std::min(tx, s.x + dx) - halfW - 1), x1 = (int)std::ceil(std::max(tx, s.x + dx) + halfW + 1);
        int y0 = (int)std::floor(std::min(ty, s.y + dy) - halfW - 1), y1 = (int)std::ceil(std::max(ty, s.y + dy) + halfW + 1);
        for (int y = std::max(0, y0); y <= std::min(img.h - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(img.w - 1, x1); ++x) {
                float px = x + 0.5f - tx, py = y + 0.5f - ty;
                float along = (px * dx + py * dy) / len;
                float across = px * nx + py * ny;
                if (along < 0 || along > 1 || std::fabs(across) > halfW) continue;
                float lx = across / halfPx;
                float w = 1.0f - smoothstep(1.0f - 0.5f / halfPx, 1.0f + 1.0f / halfPx, std::fabs(lx));
                float a = s.alpha * w * (0.12f + 0.88f * std::pow(saturate(along), 1.7f));
                if (a > 0.002f) blend(img, x, y, 0.80f, 0.86f, 0.93f, a);
            }
        }
        return true;
    }
    if (s.kind != KindDrop && s.kind != KindFog) return false;
    float radius = s.kind == KindDrop ? std::max(0.5f, s.size * 0.5f) : std::max(1.0f, s.size * 0.5f);
    float extent = s.kind == KindDrop ? radius * std::max(s.soft, 1.0f) * 1.3f + 1.5f : radius;
    int x0 = (int)std::floor(s.x - extent), x1 = (int)std::ceil(s.x + extent);
    int y0 = (int)std::floor(s.y - extent), y1 = (int)std::ceil(s.y + extent);
    for (int y = std::max(0, y0); y <= std::min(img.h - 1, y1); ++y) {
        for (int x = std::max(0, x0); x <= std::min(img.w - 1, x1); ++x) {
            float px = (x + 0.5f - s.x) / radius, py = (y + 0.5f - s.y) / radius;
            if (std::fabs(px) > extent / radius || std::fabs(py) > extent / radius) continue;
            if (s.kind == KindFog) {
                float qy = py / std::max(s.soft, 0.2f);
                float f = saturate(1.0f - std::sqrt(px * px + qy * qy));
                float a = s.alpha * f * f * (3.0f - 2.0f * f);
                if (a > 0.002f) blend(img, x, y, 0.86f, 0.88f, 0.92f, a);
                continue;
            }
            float st = std::max(s.soft, 0.5f);
            float qx = px, qy = py / st;
            if (st > 1.02f) qx *= 1.0f + (st - 1.0f) * saturate(-qy) * 1.2f;
            float d = std::sqrt(qx * qx + qy * qy);
            float cov = 1.0f - smoothstep(1.0f - 1.2f / radius, 1.0f, d);
            float rim = smoothstep(0.45f, 1.0f, d);
            float r = 0.80f + (0.22f - 0.80f) * rim, g = 0.88f + (0.30f - 0.88f) * rim, b = 0.97f + (0.42f - 0.97f) * rim;
            float body = 0.18f + (0.66f - 0.18f) * rim;
            float hx = qx + 0.34f, hy = qy + 0.40f;
            float hl = 1.0f - smoothstep(0.0f, 0.34f, std::sqrt(hx * hx + hy * hy));
            float bx = qx - 0.22f, by = qy - 0.48f;
            float bl = (1.0f - smoothstep(0.0f, 0.42f, std::sqrt(bx * bx + by * by))) * 0.45f;
            float lit = saturate(hl + bl);
            r += (1.0f - r) * lit;
            g += (1.0f - g) * lit;
            b += (1.0f - b) * lit;
            float a = s.alpha * cov * std::max(body, lit * 0.95f);
            if (a > 0.002f) blend(img, x, y, r, g, b, a);
        }
    }
    return true;
}

static void drawSprite(Image& img, const Sprite& s) {
    if (drawWeatherSprite(img, s)) return;
    float radius = std::max(0.5f, s.size * 0.5f * (1.0f + s.soft * 0.8f));
    float extent = radius * 1.2f + 1.5f;   // wie das Viereck im Vertex-Shader
    int x0 = (int)std::floor(s.x - extent), x1 = (int)std::ceil(s.x + extent);
    int y0 = (int)std::floor(s.y - extent), y1 = (int)std::ceil(s.y + extent);
    float cr = std::cos(s.rot), sr = std::sin(s.rot);
    for (int y = std::max(0, y0); y <= std::min(img.h - 1, y1); ++y) {
        for (int x = std::max(0, x0); x <= std::min(img.w - 1, x1); ++x) {
            if (x + 0.5f < s.x - extent || x + 0.5f >= s.x + extent || y + 0.5f < s.y - extent || y + 0.5f >= s.y + extent) continue;
            float dx = (x + 0.5f - s.x) / radius, dy = (y + 0.5f - s.y) / radius;
            float a = 0, cr0 = 0.95f, cg = 0.97f, cb = 1.0f;
            if (s.kind == KindSplash) {
                cr0 = 0.86f;
                cg = 0.91f;
                cb = 0.98f;
            }
            if (s.kind == KindCrystal) {
                float lx = dx * cr - dy * sr, ly = dx * sr + dy * cr;
                a = s.alpha * crystalCoverage(lx, ly, 1.2f / radius);
            } else if (s.kind == KindClump || s.kind == KindCrumb) {
                float d = std::sqrt(dx * dx + dy * dy);
                float ang = std::atan2(dy, dx);
                // weicher, unregelmäßiger Klumpen (wenige, flache Ausbuchtungen statt Sternform)
                float lump = 1.0f + 0.07f * std::sin(3 * ang + s.seed * 6.28f) + 0.04f * std::sin(5 * ang + s.seed * 12.6f);
                float edge = 1.0f / radius * 1.5f;
                a = s.alpha * (1.0f - smoothstep(lump * (1 - s.soft * 0.6f) - edge, lump, d));
                float shade = 0.9f + 0.1f * clampf(-dy, -1.0f, 1.0f);
                cr0 *= shade; cg *= shade; cb = std::min(1.0f, cb * (shade + 0.04f));
            } else {
                float d = std::sqrt(dx * dx + dy * dy);
                float core = 1.0f - s.soft * 0.6f;
                a = s.alpha * (1.0f - smoothstep(core * 0.999f, 1.0f, d));
                if (s.soft < 0.5f) a = s.alpha * (1.0f - smoothstep(core - 1.2f / radius, 1.0f, d));
            }
            if (a > 0.002f) blend(img, x, y, cr0, cg, cb, a);
        }
    }
}

// Dreieck mit baryzentrischer Interpolation von dist/height (wie der Pixel-Shader der Haufen)
static void drawPileTriangle(Image& img, const PileVertex& v0, const PileVertex& v1, const PileVertex& v2, float scale) {
    float minx = std::min({v0.x, v1.x, v2.x}), maxx = std::max({v0.x, v1.x, v2.x});
    float miny = std::min({v0.y, v1.y, v2.y}), maxy = std::max({v0.y, v1.y, v2.y});
    int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(img.w - 1, (int)std::ceil(maxx));
    int y0 = std::max(0, (int)std::floor(miny)), y1 = std::min(img.h - 1, (int)std::ceil(maxy));
    float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (std::fabs(area) < 1e-6f) return;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((v1.x - px) * (v2.y - py) - (v2.x - px) * (v1.y - py)) / area;
            float w1 = ((v2.x - px) * (v0.y - py) - (v0.x - px) * (v2.y - py)) / area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float dist = w0 * v0.dist + w1 * v1.dist + w2 * v2.dist;
            float height = w0 * v0.height + w1 * v1.height + w2 * v2.height;
            float edge = clampf(dist + 0.5f, 0.0f, 1.0f);                   // weiche Oberkante
            float depthT = clampf(dist / std::max(height, 1.0f), 0.0f, 1.0f);
            float thick = clampf(height / (14.0f * scale), 0.0f, 1.0f);
            float f = smoothstep(0.0f, 1.0f, depthT) * (0.35f + 0.65f * thick);
            float r = 0.985f + (0.80f - 0.985f) * f;
            float g = 0.99f + (0.865f - 0.99f) * f;
            float b = 1.0f + (0.955f - 1.0f) * f;
            float glint = dist < 1.8f ? 0.03f * (1.0f - dist / 1.8f) : 0.0f;  // heller Rand oben (Glanz)
            r = std::min(1.0f, r + glint); g = std::min(1.0f, g + glint);
            blend(img, x, y, r, g, b, 0.97f * edge);
        }
    }
}


}  // namespace soft
