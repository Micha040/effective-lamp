// DeskCats – Aussehen, Grundhaltungen und Umrechnung in Shader-Daten
#include "cat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cat {

using hlsl::float4;

namespace {

float2 add(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
float2 sub(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
float2 mix(float2 a, float2 b, float t) { return float2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
float len(float2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
float2 norm(float2 a) {
    float l = len(a);
    return l > 0 ? float2(a.x / l, a.y / l) : a;
}
float3 v3(float x, float y, float z) { return float3(x, y, z); }
float3 add3(float3 a, float3 b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
float3 mul3(float3 a, float s) { return float3(a.x * s, a.y * s, a.z * s); }
float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float3 norm3(float3 a) {
    float l = std::sqrt(dot3(a, a));
    return l > 0 ? mul3(a, 1.f / l) : a;
}

float srgbToLinear(float c) { return std::pow(c, 2.2f); }
float4 colorOf(unsigned rgb, float w = 0.f) {
    float r = ((rgb >> 16) & 255) / 255.f, g = ((rgb >> 8) & 255) / 255.f, b = (rgb & 255) / 255.f;
    return float4(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), w);
}

}  // namespace

// ------------------------------------------------------------------ Aussehen

Look lookFatos() {
    Look k;
    k.name = "Fatos";
    k.bodyLen = 18.f;
    k.shoulderH = 17.f;
    k.hipH = 17.5f;
    k.chestR = 8.0f;
    k.hipR = 7.4f;
    k.waistR = 7.0f;
    k.bellyR = 5.6f;
    k.neckR0 = 6.0f;
    k.neckR1 = 4.7f;
    k.headR = 6.4f;
    k.muzzleR = 0.42f;
    k.muzzleFwd = 0.56f;
    k.muzzleDown = 0.34f;
    k.cheekR = 0.52f;
    k.cheekFwd = 0.24f;
    k.cheekDown = 0.36f;
    k.cheekSide = 0.47f;
    k.upperArmR = 2.9f;
    k.foreArmR = 1.8f;
    k.pawR = 1.6f;
    k.thighR = 5.3f;
    k.shinR = 2.1f;
    k.metaR = 1.5f;
    k.tailLen = 25.f;
    k.tailR0 = 2.5f;
    k.tailR1 = 1.6f;
    k.earType = 1;
    k.earSize = 0.9f;
    k.eyeAX = 0.30f;
    k.eyeAY = 0.285f;
    k.eyeAz = 0.6f;
    k.eyeEl = 0.12f;
    k.noseSize = 0.92f;
    k.pattern = 0;
    k.stripeFreq = 4.4f;
    k.stripeContrast = 0.5f;
    k.furDetail = 0.09f;
    k.rim = 0.16f;
    const unsigned pal[16] = {0xE2A267, 0xC4793D, 0xF6E0C4, 0xA65E2B, 0xE49A92, 0xE8B2A6, 0xD9BF63, 0x8C7F36,
                              0xF7F2EA, 0x2B1D16, 0xD9908A, 0xFFE3C2, 0xFFF4E6, 0x8C8E99, 0x000000, 0x000000};
    std::memcpy(k.pal, pal, sizeof(pal));
    return k;
}

Look lookKosta() {
    Look k;
    k.name = "Kosta";
    k.bodyLen = 16.5f;
    k.shoulderH = 16.2f;
    k.hipH = 16.8f;
    k.chestR = 6.3f;
    k.hipR = 5.8f;
    k.waistR = 5.3f;
    k.bellyR = 4.6f;
    k.neckR0 = 4.8f;
    k.neckR1 = 3.7f;
    k.headR = 5.0f;
    k.muzzleR = 0.4f;
    k.muzzleFwd = 0.62f;
    k.muzzleDown = 0.3f;
    k.cheekR = 0.36f;
    k.cheekFwd = 0.2f;
    k.cheekDown = 0.36f;
    k.cheekSide = 0.5f;
    k.upperArmR = 2.3f;
    k.foreArmR = 1.45f;
    k.pawR = 1.3f;
    k.thighR = 4.3f;
    k.shinR = 1.75f;
    k.metaR = 1.25f;
    k.tailLen = 26.f;
    k.tailR0 = 1.95f;
    k.tailR1 = 1.1f;
    k.earType = 0;
    k.earSize = 1.15f;
    k.eyeAX = 0.28f;
    k.eyeAY = 0.24f;
    k.eyeAz = 0.62f;
    k.eyeEl = 0.13f;
    k.noseSize = 0.78f;
    k.pattern = 1;
    k.stripeFreq = 0.f;
    k.stripeContrast = 0.f;
    k.furDetail = 0.07f;
    k.rim = 0.42f;
    const unsigned pal[16] = {0x1F1E22, 0x151417, 0xF4F0E9, 0x0F0E10, 0x3A3135, 0x9E7F87, 0xCFE05A, 0x5E8E2B,
                              0xF4F1EA, 0x141215, 0x5A4448, 0xB7C8E8, 0xFFF4E6, 0x8C8E99, 0x000000, 0x000000};
    std::memcpy(k.pal, pal, sizeof(pal));
    return k;
}

// ------------------------------------------------------------------ Haltungen

void buildTail(Pose& p, float2 base, float2 dir, float len, float curl0, float curl1, int n) {
    n = std::max(2, std::min(12, n));
    p.tailN = n;
    float seg = len / (n - 1);
    float2 d = norm(dir);
    float2 pos = base;
    for (int i = 0; i < n; ++i) {
        p.tail[i] = pos;
        float s = (float)i / (n - 1);
        float k = (curl0 + (curl1 - curl0) * s) * seg;
        float c = std::cos(k), sn = std::sin(k);
        d = float2(d.x * c - d.y * sn, d.x * sn + d.y * c);   // gegen den Uhrzeigersinn um k
        pos = add(pos, d * seg);
    }
}

void buildTailSpline(Pose& p, const float2* ctrl, int count, int n) {
    n = std::max(2, std::min(12, n));
    // Catmull-Rom durch die Kontrollpunkte, dicht abtasten, dann nach Bogenlänge gleichmäßig verteilen
    const int dense = 96;
    float2 pts[dense];
    float acc[dense];
    for (int i = 0; i < dense; ++i) {
        float t = (float)i / (dense - 1) * (count - 1);
        int k = std::min(count - 2, (int)t);
        float u = t - k;
        float2 p0 = ctrl[std::max(0, k - 1)], p1 = ctrl[k], p2 = ctrl[k + 1], p3 = ctrl[std::min(count - 1, k + 2)];
        float u2 = u * u, u3 = u2 * u;
        auto cr = [&](float a, float b, float c, float d) {
            return 0.5f * ((2.f * b) + (-a + c) * u + (2.f * a - 5.f * b + 4.f * c - d) * u2 + (-a + 3.f * b - 3.f * c + d) * u3);
        };
        pts[i] = float2(cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y));
        acc[i] = i == 0 ? 0.f : acc[i - 1] + len(sub(pts[i], pts[i - 1]));
    }
    p.tailN = n;
    int j = 0;
    for (int i = 0; i < n; ++i) {
        float target = acc[dense - 1] * i / (n - 1);
        while (j < dense - 2 && acc[j + 1] < target) ++j;
        float span = std::max(1e-6f, acc[j + 1] - acc[j]);
        p.tail[i] = mix(pts[j], pts[j + 1], std::max(0.f, std::min(1.f, (target - acc[j]) / span)));
    }
}


namespace {
// Haltungen sind in Zentimetern für Fatos entworfen und werden auf die Maße der Katze skaliert.
struct Scaler {
    float sx, sy;
    explicit Scaler(const Look& lk) : sx(lk.bodyLen / 18.f), sy(lk.shoulderH / 17.f) {}
    float2 operator()(float x, float y) const { return float2(x * sx, y * sy); }
};
void setShadow(Pose& p, float cx, float rx) {
    p.shadowC = float2(cx, 0.3f);
    p.shadowRX = rx;
    p.shadowRY = 1.9f;
    p.shadowA = 0.34f;
}
void setSpine(Pose& p, const Look& lk, float2 a, float2 b, float2 c, float2 d, float ra, float rb, float rc, float rd) {
    p.spine[0] = a;
    p.spine[1] = b;
    p.spine[2] = c;
    p.spine[3] = d;
    p.spineR[0] = lk.hipR * ra;
    p.spineR[1] = lk.waistR * rb;
    p.spineR[2] = lk.chestR * rc;
    p.spineR[3] = lk.chestR * rd;
}
}  // namespace

Pose poseStand(const Look& lk) {
    Pose p;
    Scaler T(lk);
    setSpine(p, lk, T(-10.8f, 20.0f), T(-4.2f, 21.0f), T(3.4f, 20.6f), T(9.6f, 19.4f), 1.f, 0.97f, 0.98f, 1.f);
    p.belly0 = T(-5.f, 16.4f);
    p.belly1 = T(3.6f, 15.8f);
    p.bellyR = lk.bellyR;
    p.head = T(16.4f, 28.6f);
    p.neck0 = T(9.0f, 21.4f);
    p.neck1 = add(p.head, float2(-lk.headR * 0.42f, -lk.headR * 0.5f));
    p.neckR0 = lk.neckR0;
    p.neckR1 = lk.neckR1;
    p.yaw = 0.45f;
    p.pitch = 0.f;
    // lockerer Stand: ferne Beine leicht versetzt, damit alle vier zu sehen sind
    const float2 fn[4] = {T(8.6f, 16.8f), T(9.6f, 10.6f), T(10.2f, 3.2f), T(12.4f, 0.f)};
    const float2 ff[4] = {T(6.2f, 17.0f), T(6.8f, 10.6f), T(7.2f, 3.3f), T(9.4f, 0.f)};
    const float2 hn[5] = {T(-9.f, 16.8f), T(-4.4f, 10.2f), T(-10.4f, 4.8f), T(-9.4f, 0.f), T(-6.8f, 0.f)};
    const float2 hf[5] = {T(-6.6f, 17.f), T(-2.0f, 10.4f), T(-7.8f, 4.9f), T(-6.8f, 0.f), T(-4.2f, 0.f)};
    for (int i = 0; i < 4; ++i) {
        p.fn[i] = fn[i];
        p.ff[i] = ff[i];
    }
    for (int i = 0; i < 5; ++i) {
        p.hn[i] = hn[i];
        p.hf[i] = hf[i];
    }
    p.fn[3].y = p.ff[3].y = lk.pawR * 0.74f;
    p.hn[3].y = p.hf[3].y = lk.pawR * 0.9f;
    p.hn[4].y = p.hf[4].y = lk.pawR * 0.74f;
    const float2 tc[6] = {T(-16.4f, 23.2f), T(-20.6f, 25.2f), T(-23.6f, 29.2f), T(-24.6f, 33.6f), T(-23.4f, 37.f), T(-21.f, 38.4f)};
    buildTailSpline(p, tc, 6, 10);
    setShadow(p, 0.f, lk.bodyLen * 0.95f);
    return p;
}

Pose poseSit(const Look& lk) {
    Pose p;
    Scaler T(lk);
    setSpine(p, lk, T(-7.2f, 7.8f), T(-4.6f, 12.4f), T(-1.0f, 16.8f), T(3.4f, 19.6f), 1.16f, 1.02f, 0.94f, 0.92f);
    p.belly0 = T(-0.5f, 8.2f);
    p.belly1 = T(2.6f, 12.2f);
    p.bellyR = lk.bellyR * 1.02f;
    p.head = T(6.6f, 28.4f);
    p.neck0 = T(3.4f, 21.8f);
    p.neck1 = add(p.head, float2(-lk.headR * 0.22f, -lk.headR * 0.58f));
    p.neckR0 = lk.neckR0;
    p.neckR1 = lk.neckR1;
    p.yaw = 1.1f;
    p.pitch = 0.05f;
    const float2 fn[4] = {T(3.9f, 16.2f), T(5.0f, 10.2f), T(5.6f, 3.2f), T(7.9f, 0.f)};
    const float2 ff[4] = {T(2.2f, 16.4f), T(3.3f, 10.2f), T(3.9f, 3.2f), T(6.1f, 0.f)};
    const float2 hn[5] = {T(-5.4f, 7.6f), T(-0.6f, 5.4f), T(-8.9f, 2.1f), T(-5.6f, 0.f), T(-2.4f, 0.f)};
    p.thighEnd = 1.55f;
    for (int i = 0; i < 4; ++i) {
        p.fn[i] = fn[i];
        p.ff[i] = ff[i];
    }
    for (int i = 0; i < 5; ++i) {
        p.hn[i] = hn[i];
        p.hf[i] = add(hn[i], T(1.4f, 0.4f));
    }
    p.fn[3].y = p.ff[3].y = lk.pawR * 0.74f;
    p.hn[3].y = p.hf[3].y = lk.pawR * 0.9f;
    p.hn[4].y = p.hf[4].y = lk.pawR * 0.74f;
    // Schwanz: vom Po zum Boden, dann vorne um die Pfoten
    const float2 tc[6] = {T(-12.2f, 6.4f), T(-14.2f, 2.8f), T(-11.6f, 1.6f), T(-4.f, 1.5f), T(4.f, 1.5f), T(9.6f, 2.4f)};
    buildTailSpline(p, tc, 6, 11);
    for (int i = 0; i < p.tailN; ++i) p.tail[i].y = std::max(p.tail[i].y, lk.tailR0 * 0.85f);
    p.tailFrontFrom = 4;
    setShadow(p, T(-1.f, 0.f).x, lk.bodyLen * 0.75f);
    return p;
}

Pose poseLoaf(const Look& lk, float pawDangle) {
    Pose p;
    Scaler T(lk);
    float ly = lk.chestR * 0.92f;
    float X = T(1.f, 0.f).x;
    setSpine(p, lk, float2(-9.5f * X, ly + 0.2f), float2(-3.5f * X, ly + 0.9f), float2(2.6f * X, ly + 1.0f), float2(7.6f * X, ly + 0.6f), 1.f,
             1.f, 1.f, 0.97f);
    p.belly0 = float2(-5.f * X, lk.bellyR * 0.95f);
    p.belly1 = float2(3.5f * X, lk.bellyR * 0.95f);
    p.bellyR = lk.bellyR;
    p.head = float2(12.8f * X, ly + lk.headR * 1.1f + 0.6f);
    p.neck0 = add(p.spine[3], float2(-0.6f, 1.2f));
    p.neck1 = add(p.head, float2(-lk.headR * 0.35f, -lk.headR * 0.55f));
    p.neckR0 = lk.neckR0;
    p.neckR1 = lk.neckR1;
    p.yaw = 0.95f;
    p.pitch = -0.05f;
    // nahes Vorderbein: Ellbogen an der Kante, Unterarm hängt nach unten (pawDangle 1) oder liegt vorne (0)
    float d = pawDangle;
    p.fn[0] = float2(7.4f * X, ly - 1.f);
    p.fn[1] = float2(12.2f * X, lk.foreArmR * 1.0f);
    p.fn[2] = mix(float2(16.8f * X, lk.foreArmR * 0.9f), float2(13.2f * X, -5.4f), d);
    p.fn[3] = mix(float2(19.2f * X, lk.pawR * 0.74f), float2(13.9f * X, -8.6f), d);
    p.ff[0] = float2(5.2f * X, ly - 1.f);
    p.ff[1] = float2(9.8f * X, lk.foreArmR * 1.0f);
    p.ff[2] = float2(14.4f * X, lk.foreArmR * 0.9f);
    p.ff[3] = float2(16.8f * X, lk.pawR * 0.74f);
    // Hinterbeine eingeklappt
    p.hn[0] = float2(-8.2f * X, ly - 1.f);
    p.hn[1] = float2(-3.2f * X, lk.shinR * 1.4f);
    p.hn[2] = float2(-10.6f * X, lk.metaR * 1.05f);
    p.hn[3] = float2(-8.2f * X, lk.pawR * 0.9f);
    p.hn[4] = float2(-5.6f * X, lk.pawR * 0.74f);
    for (int i = 0; i < 5; ++i) p.hf[i] = add(p.hn[i], float2(1.6f, 0.3f));
    // Schwanz liegt seitlich am Körper nach vorne
    const float2 tc[6] = {float2(-14.6f * X, ly - 1.6f), float2(-16.6f * X, 3.0f), float2(-13.6f * X, 1.6f), float2(-6.5f * X, 1.5f),
                          float2(0.f, 1.6f), float2(4.4f * X, 2.4f)};
    buildTailSpline(p, tc, 6, 11);
    for (int i = 0; i < p.tailN; ++i) p.tail[i].y = std::max(p.tail[i].y, lk.tailR0 * 0.85f);
    p.tailFrontFrom = 3;
    setShadow(p, 0.f, lk.bodyLen * 1.05f);
    return p;
}

Pose poseSleep(const Look& lk, float breath) {
    Pose p;
    Scaler T(lk);
    float sw = 1.f + 0.035f * breath;
    float2 c = T(-1.f, 6.4f);
    float rr = lk.bodyLen * 0.42f;
    const float ang[4] = {3.45f, 2.4f, 1.2f, 0.3f};
    for (int i = 0; i < 4; ++i) p.spine[i] = float2(c.x + std::cos(ang[i]) * rr, c.y + std::sin(ang[i]) * rr * 0.52f);
    p.spineR[0] = lk.hipR * sw;
    p.spineR[1] = lk.waistR * 1.0f * sw;
    p.spineR[2] = lk.chestR * 0.95f * sw;
    p.spineR[3] = lk.chestR * 0.9f * sw;
    float lift = -1e9f;
    for (int i = 0; i < 4; ++i) lift = std::max(lift, 0.4f - (p.spine[i].y - p.spineR[i]));
    for (int i = 0; i < 4; ++i) p.spine[i].y += lift;
    p.belly0 = add(p.spine[0], float2(rr * 0.5f, -1.f));
    p.belly1 = add(p.spine[3], float2(-rr * 0.5f, -1.f));
    p.bellyR = lk.bellyR * sw;
    p.head = float2(p.spine[3].x + lk.headR * 0.8f, lk.headR * 1.04f + 0.3f);
    p.neck0 = p.spine[3];
    p.neck1 = add(p.head, float2(-lk.headR * 0.5f, -lk.headR * 0.1f));
    p.neckR0 = lk.neckR0 * 0.95f;
    p.neckR1 = lk.neckR1;
    p.yaw = 0.75f;
    p.pitch = -0.3f;
    p.roll = 0.22f;
    p.eyeOpen = 0.f;
    p.squint = 0.35f;
    p.earBack = 0.1f;
    // Vorderpfoten unter dem Kinn
    p.fn[0] = add(p.spine[3], float2(0.f, -2.5f));
    p.fn[1] = float2(p.fn[0].x + 1.5f, lk.foreArmR * 1.1f);
    p.fn[2] = float2(p.fn[0].x + 5.5f, lk.foreArmR);
    p.fn[3] = float2(p.fn[0].x + 8.f, lk.pawR * 0.74f);
    for (int i = 0; i < 4; ++i) p.ff[i] = add(p.fn[i], float2(-1.4f, 0.2f));
    p.hn[0] = add(p.spine[0], float2(0.8f, -1.f));
    p.hn[1] = float2(p.hn[0].x + 4.2f, lk.shinR * 1.5f);
    p.hn[2] = float2(p.hn[0].x - 1.2f, lk.metaR * 1.1f);
    p.hn[3] = float2(p.hn[0].x + 1.2f, lk.pawR * 0.9f);
    p.hn[4] = float2(p.hn[0].x + 3.4f, lk.pawR * 0.74f);
    for (int i = 0; i < 5; ++i) p.hf[i] = add(p.hn[i], float2(1.f, 0.3f));
    // Schwanz liegt vorne um den Körper bis unter die Nase
    float2 tb = add(p.spine[0], float2(-lk.hipR * 0.55f, -lk.hipR * 0.35f));
    const float2 tc[6] = {tb, float2(tb.x - 1.5f, 2.4f), float2(tb.x + 4.f, 1.6f), float2(p.spine[3].x - 1.f, 1.6f),
                          float2(p.head.x - 1.f, 2.0f), float2(p.head.x + 3.f, 3.2f)};
    buildTailSpline(p, tc, 6, 11);
    for (int i = 0; i < p.tailN; ++i) p.tail[i].y = std::max(p.tail[i].y, lk.tailR0 * 0.85f);
    p.tailFrontFrom = 2;
    setShadow(p, 0.f, rr * 1.9f);
    return p;
}

// ------------------------------------------------------------------ Rahmen

Box poseBounds(const Look& lk, const Pose& p) {
    Box b{1e9f, 1e9f, -1e9f, -1e9f};
    auto grow = [&](float2 v, float r) {
        b.x0 = std::min(b.x0, v.x - r);
        b.x1 = std::max(b.x1, v.x + r);
        b.y0 = std::min(b.y0, v.y - r);
        b.y1 = std::max(b.y1, v.y + r);
    };
    for (int i = 0; i < 4; ++i) grow(p.spine[i], p.spineR[i]);
    grow(p.belly0, p.bellyR);
    grow(p.belly1, p.bellyR);
    for (int i = 0; i < 4; ++i) {
        grow(p.fn[i], lk.upperArmR);
        grow(p.ff[i], lk.upperArmR);
    }
    for (int i = 0; i < 5; ++i) {
        grow(p.hn[i], i == 0 ? lk.thighR : lk.shinR);
        grow(p.hf[i], i == 0 ? lk.thighR : lk.shinR);
    }
    for (int i = 0; i < p.tailN; ++i) grow(p.tail[i], lk.tailR0);
    grow(p.head, lk.headR * (lk.earType == 0 ? 1.9f : 1.6f));
    grow(p.shadowC, 0.f);
    b.x0 = std::min(b.x0, p.shadowC.x - p.shadowRX);
    b.x1 = std::max(b.x1, p.shadowC.x + p.shadowRX);
    b.y0 = std::min(b.y0, p.shadowC.y - p.shadowRY);
    return b;
}

// ------------------------------------------------------------------ Shader-Daten

void buildCB(const Look& lk, const Pose& p, bool facingLeft, float originX, float originY, float pxPerCm, float time,
             float alpha, CatCB& cb) {
    cb = CatCB();
    const float m = facingLeft ? -1.f : 1.f;
    auto X = [m](float2 v) { return float2(v.x * m, v.y); };
    cb.view = float4(originX, originY, 1.f / pxPerCm, pxPerCm);
    for (int i = 0; i < 16; ++i) cb.pal[i] = colorOf(lk.pal[i]);
    cb.pat = float4((float)lk.pattern, lk.stripeFreq, lk.stripeContrast, lk.furDetail);

    int n = 0;
    auto seg = [&](float2 a, float2 b, float ra, float rb, int layer, int part, float u0, float u1, float vSign) {
        if (n >= kMaxSeg) return;
        a = X(a);
        b = X(b);
        cb.seg[n * 3] = float4(a.x, a.y, b.x, b.y);
        cb.seg[n * 3 + 1] = float4(ra, rb, (float)layer, (float)part);
        cb.seg[n * 3 + 2] = float4(u0, u1, vSign * m, 0.f);
        ++n;
    };
    auto frontLeg = [&](const float2* j, int layer) {
        seg(j[0], j[1], lk.upperArmR, lk.upperArmR * 0.82f, layer, 2, 0.f, 0.42f, 1.f);
        seg(j[1], j[2], lk.foreArmR, lk.foreArmR * 0.86f, layer, 3, 0.42f, 0.86f, 1.f);
        float2 d = norm(sub(j[3], j[2]));
        seg(j[2], sub(j[3], d * (lk.pawR * 0.35f)), lk.pawR * 0.92f, lk.pawR, layer, 4, 0.86f, 1.f, 1.f);
    };
    auto hindLeg = [&](const float2* j, int layer) {
        seg(j[0], j[1], lk.thighR, lk.shinR * p.thighEnd, layer, 5, 0.f, 0.35f, 1.f);
        seg(j[1], j[2], lk.shinR, lk.metaR * 1.05f, layer, 6, 0.35f, 0.62f, 1.f);
        seg(j[2], j[3], lk.metaR, lk.metaR * 0.95f, layer, 7, 0.62f, 0.86f, 1.f);
        float2 d = norm(sub(j[4], j[3]));
        seg(j[3], sub(j[4], d * (lk.pawR * 0.35f)), lk.pawR * 0.95f, lk.pawR, layer, 8, 0.86f, 1.f, 1.f);
    };
    // ferne Beine (dahinter)
    hindLeg(p.hf, 0);
    frontLeg(p.ff, 0);
    // Schwanz: Ansatz verschmilzt mit dem Po, Rest dahinter bzw. davor
    for (int i = 0; i + 1 < p.tailN; ++i) {
        float t0 = (float)i / (p.tailN - 1), t1 = (float)(i + 1) / (p.tailN - 1);
        float r0 = lk.tailR0 + (lk.tailR1 - lk.tailR0) * t0, r1 = lk.tailR0 + (lk.tailR1 - lk.tailR0) * t1;
        if (i == p.tailN - 2) r1 *= 0.82f;
        int layer = i == 0 ? 2 : (i >= p.tailFrontFrom ? 3 : 1);
        seg(p.tail[i], p.tail[i + 1], r0, r1, layer, 9, t0, t1, -1.f);
    }
    // Rumpf, Bauch, Hals
    for (int i = 0; i < 3; ++i) seg(p.spine[i], p.spine[i + 1], p.spineR[i], p.spineR[i + 1], 2, 0, i / 3.f, (i + 1) / 3.f, 1.f);
    if (p.bellyR > 0) seg(p.belly0, p.belly1, p.bellyR, p.bellyR * 0.96f, 2, 0, 0.3f, 0.75f, 1.f);
    seg(p.neck0, p.neck1, p.neckR0, p.neckR1, 2, 1, 1.f, 1.15f, 1.f);
    // nahe Beine (vorne)
    hindLeg(p.hn, 2);
    frontLeg(p.fn, 2);
    cb.info = float4((float)n, time, lk.rim, alpha);

    // ---- Kopf
    const float R = lk.headR;
    float2 hc = X(p.head);
    float cy = std::cos(p.yaw), sy = std::sin(p.yaw);
    float cp = std::cos(p.pitch), sp = std::sin(p.pitch);
    float cr = std::cos(p.roll), sr = std::sin(p.roll);
    float3 F = v3(cy, 0.f, sy), U = v3(0.f, 1.f, 0.f), S = v3(-sy, 0.f, cy);
    float3 F2 = add3(mul3(F, cp), mul3(U, sp));
    float3 U2 = add3(mul3(U, cp), mul3(F, -sp));
    float3 U3 = add3(mul3(U2, cr), mul3(S, sr));
    float3 S3 = add3(mul3(S, cr), mul3(U2, -sr));
    F = F2;
    U = U3;
    S = S3;
    F.x *= m;
    U.x *= m;
    S.x *= m;
    auto proj = [&](float3 l) {   // Kopf-Koordinate (in Kopfradien) -> Bildschirm (x, y) und Tiefe z (cm)
        float3 w = add3(add3(mul3(F, l.x), mul3(U, l.y)), mul3(S, l.z));
        return v3(hc.x + w.x * R, hc.y + w.y * R, w.z * R);
    };
    cb.head[0] = float4(hc.x, hc.y, R, 0.f);
    cb.head[1] = float4(F, p.eyeOpen);
    cb.head[2] = float4(U, 0.12f + 0.5f * p.pupil);
    cb.head[3] = float4(S, p.squint);
    float3 mz = proj(v3(lk.muzzleFwd, -lk.muzzleDown, 0.f));
    cb.head[4] = float4(mz.x, mz.y, mz.z, lk.muzzleR * R);
    float3 c1 = proj(v3(lk.cheekFwd, -lk.cheekDown, lk.cheekSide));
    float3 c2 = proj(v3(lk.cheekFwd, -lk.cheekDown, -lk.cheekSide));
    if (c2.z > c1.z) std::swap(c1, c2);
    cb.head[5] = float4(c1.x, c1.y, c1.z, lk.cheekR * R);
    cb.head[6] = float4(c2.x, c2.y, c2.z, lk.cheekR * R);
    // Ohren
    auto ear = [&](float side, int slot) {
        float3 b0, tip, b2, nrm;
        if (lk.earType == 0) {
            b0 = v3(0.36f, 0.8f, 0.42f * side);
            b2 = v3(-0.32f, 0.8f, 0.5f * side);
            tip = v3(0.02f, 1.55f, 0.62f * side);
            nrm = v3(0.8f, 0.12f, 0.58f * side);
        } else {
            b0 = v3(0.42f, 0.82f, 0.36f * side);
            b2 = v3(-0.2f, 0.92f, 0.38f * side);
            tip = v3(0.5f, 1.02f, 0.5f * side);
            nrm = v3(0.3f, 0.8f, 0.5f * side);
        }
        float3 bc = mul3(add3(b0, b2), 0.5f);
        float3 dt = add3(tip, mul3(bc, -1.f));
        tip = add3(bc, mul3(dt, lk.earSize));
        tip = add3(tip, v3(-0.45f * p.earBack, -0.25f * p.earBack, 0.25f * p.earBack * side));
        float3 P0 = proj(b0), P1 = proj(tip), P2 = proj(b2);
        float3 nw = norm3(add3(add3(mul3(F, nrm.x), mul3(U, nrm.y)), mul3(S, nrm.z)));
        float vis = std::max(0.f, std::min(1.f, nw.z * 1.4f - 0.05f)) * (lk.earType == 0 ? 1.f : 0.25f);
        float round = R * (lk.earType == 0 ? 0.07f : 0.13f);
        cb.head[slot] = float4(P0.x, P0.y, P1.x, P1.y);
        cb.head[slot + 1] = float4(P2.x, P2.y, vis, round);
        return P1.z;
    };
    float zA = ear(1.f, 7);
    float4 a0 = cb.head[7], a1 = cb.head[8];
    float zB = ear(-1.f, 9);
    if (zB > zA) {   // Slot 7/8 = das uns zugewandte Ohr
        std::swap(cb.head[7], cb.head[9]);
        std::swap(cb.head[8], cb.head[10]);
    }
    (void)a0;
    (void)a1;
    cb.head[11] = float4(lk.eyeAX, lk.eyeAY, 0.f, 0.f);
    cb.head[12] = float4(p.mouth, lk.noseSize, 0.f, 0.f);
    // Augen (Kopf-Koordinaten) + Lichtpunkt Richtung links oben
    for (int i = 0; i < 2; ++i) {
        float side = i == 0 ? 1.f : -1.f;
        float az = lk.eyeAz, el = lk.eyeEl;
        float3 e = v3(std::cos(el) * std::cos(az), std::sin(el), side * std::cos(el) * std::sin(az));
        float3 tH = v3(-std::sin(az), 0.f, side * std::cos(az));
        float3 tV = v3(-std::sin(el) * std::cos(az), std::cos(el), -side * std::sin(el) * std::sin(az));
        auto scr = [&](float3 l) {
            float3 w = add3(add3(mul3(F, l.x), mul3(U, l.y)), mul3(S, l.z));
            return float2(w.x, w.y);
        };
        float2 hs = scr(tH), vs = scr(tV);
        // Lichtpunkt: auf dem Bildschirm nach links oben, in Augen-Einheiten
        float2 want(-0.3f * lk.eyeAX, 0.36f * lk.eyeAY);
        float det = hs.x * vs.y - hs.y * vs.x;
        float hx = 0.f, hy = 0.36f;
        if (std::fabs(det) > 1e-4f) {
            float a = (want.x * vs.y - want.y * vs.x) / det;   // Anteil entlang tH (in Kopfradien)
            float b = (hs.x * want.y - hs.y * want.x) / det;
            hx = std::max(-0.6f, std::min(0.6f, a / lk.eyeAX));
            hy = std::max(-0.6f, std::min(0.6f, b / lk.eyeAY));
        }
        cb.eye[i * 3] = float4(e, hx);
        cb.eye[i * 3 + 1] = float4(tH, hy);
        cb.eye[i * 3 + 2] = float4(tV, 0.f);
    }
    // Schnurrhaare: je drei pro Seite, leicht hängend
    int w = 0;
    for (int s = 0; s < 2; ++s) {
        float side = s == 0 ? 1.f : -1.f;
        float3 pad = v3(lk.muzzleFwd + 0.08f, -lk.muzzleDown - 0.06f, 0.26f * side);
        const float dirs[3][2] = {{0.8f, 0.16f}, {0.78f, -0.02f}, {0.72f, -0.2f}};
        for (int k = 0; k < 3; ++k) {
            float3 d = norm3(v3(dirs[k][0], dirs[k][1], 0.62f * side));
            float Lw = 1.55f;
            float3 a = pad;
            float3 mid = add3(add3(pad, mul3(d, Lw * 0.5f)), v3(0.f, -0.03f, 0.f));
            float3 end = add3(add3(pad, mul3(d, Lw)), v3(0.f, -0.14f, 0.f));
            float3 A = proj(a), M = proj(mid), E = proj(end);
            bool nearSide = (S.z * side) > 0.f;
            cb.whisk[w * 2] = float4(A.x, A.y, M.x, M.y);
            cb.whisk[w * 2 + 1] = float4(E.x, E.y, nearSide ? 0.8f : 0.55f, nearSide ? 1.f : -1.f);
            ++w;
        }
    }
    // Smoking: weißer Latz (Brust bis Kinn) und Bauchstreifen als Ellipsen, damit sie über Teilgrenzen durchlaufen
    if (lk.pattern == 1) {
        auto ellipse = [&](int slot, float2 c, float2 axisY, float rx, float ry) {
            float2 cm = X(c);
            float2 ay = norm(float2(axisY.x * m, axisY.y));
            // Drehung so, dass die y-Achse der Ellipse entlang axisY liegt
            float cs = ay.y, sn = -ay.x;
            cb.patE[slot * 2] = float4(cm.x, cm.y, rx, ry);
            cb.patE[slot * 2 + 1] = float4(cs, sn, 1.f, 0.f);
        };
        float2 up = norm(sub(p.neck1, p.fn[1]));
        float2 fwd = norm(float2(up.y, -up.x));
        float2 chest = add(mix(p.neck0, p.fn[0], 0.42f), fwd * (lk.chestR * 0.62f));
        ellipse(0, chest, up, lk.chestR * 0.5f, lk.chestR * 1.05f);
        float2 bd = norm(sub(p.belly1, p.belly0));
        float2 bn = float2(bd.y, -bd.x);   // nach unten (bei Blick nach rechts)
        float2 bc = add(mix(p.belly0, p.belly1, 0.55f), bn * (p.bellyR * 0.78f));
        ellipse(1, bc, bn, len(sub(p.belly1, p.belly0)) * 0.55f + 2.f, p.bellyR * 0.42f);
        // Ellipse 1: y-Achse entlang der Bauchnormalen -> Radien tauschen (lang entlang des Bauchs)
        cb.patE[2].z = p.bellyR * 0.42f;
        cb.patE[2].w = len(sub(p.belly1, p.belly0)) * 0.55f + 2.f;
        cb.patE[3] = float4(cb.patE[3].y, -cb.patE[3].x, 1.f, 0.f);
        float tmp = cb.patE[2].z;
        cb.patE[2].z = cb.patE[2].w;
        cb.patE[2].w = tmp;
    }
    // Kontaktschatten
    float2 sc = X(p.shadowC);
    cb.shadow = float4(sc.x, sc.y, p.shadowRX, p.shadowRY);
    cb.shadow2 = float4(p.shadowA, 0.f, 0.f, 0.f);
}

}  // namespace cat
