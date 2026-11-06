// DeskCats – Pixel-Art-Darstellung (siehe pixcat.h)
//
// Ablauf je Bild:
//   1. Rig abtasten (je Pixelmitte): Ebene (ferne Beine, Schwanz hinten, Körper, Schwanz vorne, Kopf), Körperteil,
//      Fellbereich (Grundfell, Streifen, Creme/Weiß, ...), Oberflächennormale.
//   2. Aufräumen: einzelne Ausreißer-Pixel in Fellbereichen und Lichtstufen entfernen (saubere Farbflächen).
//   3. Farben aus festen Farbleitern (Kontur, Schatten, Grund, Licht) je Fellbereich.
//   4. Konturen: außen, und innen dort, wo ein vorderer Teil über einem hinteren liegt.
//   5. Gesicht: Augen, Nase, Mund und Stirnzeichnung als kleine Pixel-Stempel.
#include "pixcat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cat {

using hlsl::float2;
using hlsl::float3;

namespace {

// ------------------------------------------------------------------ Farben

enum Region : uint8_t { R_NONE = 0, R_FUR, R_STRIPE, R_CREAM, R_TIP, R_INNEREAR, R_NOSE, R_COUNT };
enum : uint8_t { L_FARLEG = 0, L_TAILBACK = 1, L_BODY = 2, L_TAILFRONT = 3, L_HEAD = 4, L_OVER = 5, L_NONE = 255 };
// Röhren-Ebenen im Zwischenspeicher: 0..3 wie oben, 4 = Vorderbein vor dem Kopf
constexpr int kSlots = 5;
inline int slotOf(uint8_t layer) { return layer == L_OVER ? 4 : layer; }
enum : uint8_t { F_EAR = 1, F_FAREAR = 2, F_FACE = 4 };

struct Ramp {
    uint32_t outline, shadow, mid, light;
};

struct Palette {
    Ramp r[R_COUNT];
    uint32_t iris, irisDark, pupil, white, lid, mouth, closedLid, rim;
};

constexpr uint32_t rgb(unsigned v) { return 0xFF000000u | v; }

Palette paletteFor(const Look& lk) {
    Palette p{};
    if (lk.pattern == 0) {   // Fatos: rot getigert
        p.r[R_FUR] = {rgb(0x6B3518), rgb(0xC9732F), rgb(0xE99A50), rgb(0xF7BE7E)};
        p.r[R_STRIPE] = {rgb(0x6B3518), rgb(0xAF5A26), rgb(0xCD7838), rgb(0xE0914A)};
        p.r[R_CREAM] = {rgb(0x8E5B33), rgb(0xE0B88C), rgb(0xF5DDB8), rgb(0xFFF1DE)};
        p.r[R_TIP] = {rgb(0x5E2D12), rgb(0xA0501F), rgb(0xBC662D), rgb(0xCF7C3E)};
        p.r[R_INNEREAR] = {rgb(0x6B3518), rgb(0xCB8676), rgb(0xE6A595), rgb(0xF2BFB0)};
        p.r[R_NOSE] = {rgb(0x6B3518), rgb(0xC9706E), rgb(0xE58F8B), rgb(0xF2AEA8)};
        p.iris = rgb(0xD8B44A);
        p.irisDark = rgb(0x9A7F2C);
        p.pupil = rgb(0x221510);
        p.white = rgb(0xFFFBF2);
        p.lid = rgb(0x4A2410);
        p.mouth = rgb(0x6B3518);
        p.closedLid = rgb(0x6B3518);
    } else {                 // Kosta: Smoking
        p.r[R_FUR] = {rgb(0x08080B), rgb(0x15151B), rgb(0x22222B), rgb(0x3B3E52)};
        p.r[R_STRIPE] = p.r[R_FUR];
        p.r[R_CREAM] = {rgb(0x4E4C55), rgb(0xBCB9B9), rgb(0xE9E6E0), rgb(0xFFFFFF)};
        p.r[R_TIP] = p.r[R_FUR];
        p.r[R_INNEREAR] = {rgb(0x08080B), rgb(0x7A5862), rgb(0xA27983), rgb(0xBF96A0)};
        p.r[R_NOSE] = {rgb(0x08080B), rgb(0x2B2027), rgb(0x3D2E35), rgb(0x56444C)};
        p.iris = rgb(0xB2D64C);
        p.irisDark = rgb(0x6F9B2A);
        p.pupil = rgb(0x09090B);
        p.white = rgb(0xFFFFFF);
        p.lid = rgb(0x050507);
        p.mouth = rgb(0x4E4C55);
        p.closedLid = rgb(0x5A5F7C);
        p.rim = rgb(0x30334A);
    }
    return p;
}

// ------------------------------------------------------------------ Formen

float2 sub(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
float2 add(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
float2 mul(float2 a, float s) { return float2(a.x * s, a.y * s); }
float2 mix(float2 a, float2 b, float t) { return float2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
float len2(float2 a) { return std::sqrt(dot2(a, a)); }
float2 norm2(float2 a) {
    float l = len2(a);
    return l > 0 ? float2(a.x / l, a.y / l) : a;
}
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

enum Part : uint8_t { P_TORSO = 0, P_NECK, P_FUPPER, P_FLOWER, P_FPAW, P_THIGH, P_SHIN, P_META, P_HPAW, P_TAIL };

struct Seg {
    float2 a, b;
    float ra, rb;
    uint8_t layer, part;
    float u0, u1;
};

struct Hit {
    float d = 1e9f;       // weich vereinigter Abstand
    float best = 1e9f;    // Abstand zum nächsten Teil
    int bestIdx = -1;
    float t = 0, v = 0;   // Lage auf dem nächsten Teil (entlang, quer)
    float3 n{0, 0, 1};
};

void addSeg(Hit& h, const Seg& s, int idx, float2 p, float k) {
    float2 ab = sub(s.b, s.a);
    float l2 = std::max(dot2(ab, ab), 1e-6f);
    float t = clamp01(dot2(sub(p, s.a), ab) / l2);
    float2 c = add(s.a, mul(ab, t));
    float r = std::max(s.ra + (s.rb - s.ra) * t, 1e-3f);
    float2 dv = sub(p, c);
    float dist = len2(dv);
    float d = dist - r;
    float q = std::min(1.f, dist / r);
    float id = dist > 1e-6f ? q / dist : 0.f;   // Normale auf Einheitslänge begrenzt (auch weit außerhalb)
    float3 n(dv.x * id, dv.y * id, std::sqrt(std::max(0.f, 1.f - q * q)));
    float v = std::max(-1.f, std::min(1.f, (ab.x * dv.y - ab.y * dv.x) / std::sqrt(l2) / r));
    // v > 0: links der Richtung (beim Rumpf Hüfte->Schulter ist das der Rücken, rechts der Bauch)
    float w = clamp01(0.5f + 0.5f * (h.d - d) / k);
    h.d = h.d + (d - h.d) * w - k * w * (1 - w);
    h.n = float3(h.n.x + (n.x - h.n.x) * w, h.n.y + (n.y - h.n.y) * w, h.n.z + (n.z - h.n.z) * w);
    if (d < h.best) {
        h.best = d;
        h.bestIdx = idx;
        h.t = t;
        h.v = v;
    }
}

// Abstand zu einem Dreieck (negativ innen)
float triDist(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 e0 = sub(p1, p0), e1 = sub(p2, p1), e2 = sub(p0, p2);
    float2 v0 = sub(p, p0), v1 = sub(p, p1), v2 = sub(p, p2);
    auto pq = [](float2 v, float2 e) {
        float t = clamp01(dot2(v, e) / std::max(dot2(e, e), 1e-9f));
        return sub(v, mul(e, t));
    };
    float2 q0 = pq(v0, e0), q1 = pq(v1, e1), q2 = pq(v2, e2);
    float sgn = (e0.x * e2.y - e0.y * e2.x) > 0 ? 1.f : -1.f;
    float dx = std::min(std::min(dot2(q0, q0), dot2(q1, q1)), dot2(q2, q2));
    float a0 = sgn * (v0.x * e0.y - v0.y * e0.x), a1 = sgn * (v1.x * e1.y - v1.y * e1.x), a2 = sgn * (v2.x * e2.y - v2.y * e2.x);
    float dy = std::min(std::min(a0, a1), a2);
    return -std::sqrt(dx) * (dy > 0 ? 1.f : -1.f);
}

// Lichtstufe aus der Normale: Licht von oben, leicht von vorne (Blickrichtung der Katze) und zum Betrachter
int lightBand(float3 n, float bias) {
    const float3 L(0.28f, 0.80f, 0.53f);
    float ndl = (n.x * L.x + n.y * L.y + n.z * L.z) / 1.0f + bias;
    if (ndl > 0.80f) return 2;
    if (ndl < 0.18f) return 0;
    return 1;
}

// ------------------------------------------------------------------ Gesichts-Stempel
// e Iris, E dunkle Iris, k Pupille, w Lichtpunkt, o Lid/Kontur, n Nase, m Mund, '.' unverändert

struct Stamp {
    int w, h;
    const char* rows[6];
};

// Blick nach rechts (Pupille vorne); gespiegelt wird das ganze Bild
const Stamp kFatosFront = {4, 4, {".oo.", "owko", "oeko", ".oo."}};
const Stamp kFatosSide = {3, 4, {"oo.", "wko", "eko", "oo."}};
const Stamp kFatosFar = {2, 3, {"oo", "ko", "oo"}};
const Stamp kFatosHalf = {4, 3, {"oooo", "oeko", ".oo."}};
const Stamp kFatosHalfSide = {3, 3, {"ooo", "eko", "oo."}};
const Stamp kFatosClosed = {4, 2, {"c..c", ".cc."}};
const Stamp kFatosHappy = {4, 2, {".cc.", "c..c"}};
const Stamp kFatosHappySide = {3, 2, {".cc", "c.."}};
const Stamp kFatosClosedSide = {3, 2, {"c..", ".cc"}};
const Stamp kKostaFront = {3, 3, {"ooo", "wke", "eke"}};
const Stamp kKostaSide = {3, 3, {"oo.", "wke", "eko"}};
const Stamp kKostaFar = {2, 2, {"ko", "ko"}};
const Stamp kKostaHalf = {3, 2, {"ooo", "eko"}};
const Stamp kKostaClosed = {3, 2, {"c.c", ".c."}};
const Stamp kKostaHappy = {3, 2, {".c.", "c.c"}};
const Stamp kKostaHappySide = {3, 2, {".cc", "c.."}};
const Stamp kKostaClosedSide = {3, 2, {"c..", ".cc"}};

}  // namespace

// ------------------------------------------------------------------ Zeichnen

void renderPixelCat(const Look& lk, const Pose& pose, bool facingLeft, const PixelStyle& st, PixelImage& out) {
    const float ppc = st.pxPerCm, inv = 1.f / ppc;
    const float R = lk.headR * st.headScale;

    Box b = poseBounds(lk, pose);
    float grow = lk.headR * (st.headScale - 1.f) * 2.f + 1.f;
    int c0 = (int)std::floor((b.x0 - grow) * ppc), c1 = (int)std::ceil((b.x1 + grow) * ppc);
    int rTop = (int)std::ceil((b.y1 + grow) * ppc), rBot = (int)std::floor(std::min(b.y0, 0.f) * ppc) - 1;
    const int W = c1 - c0, H = rTop - rBot;
    const size_t N = (size_t)W * H;

    std::vector<uint8_t> layer(N, L_NONE), region(N, R_NONE), band(N, 1), part(N, 255), flags(N, 0);
    std::vector<float3> nrm(N, float3(0, 0, 1));

    // ---- Röhren (wie im Shader)
    std::vector<Seg> segs;
    segs.reserve(48);
    auto seg = [&](float2 a, float2 bb, float ra, float rb, uint8_t ly, uint8_t pt, float u0, float u1) {
        segs.push_back(Seg{a, bb, ra, rb, ly, pt, u0, u1});
    };
    auto frontLeg = [&](const float2* j, uint8_t ly) {
        seg(j[0], j[1], lk.upperArmR, lk.upperArmR * 0.82f, ly, P_FUPPER, 0.f, 0.42f);
        seg(j[1], j[2], lk.foreArmR, lk.foreArmR * 0.86f, ly, P_FLOWER, 0.42f, 0.86f);
        float2 d = norm2(sub(j[3], j[2]));
        seg(j[2], sub(j[3], mul(d, lk.pawR * 0.35f)), lk.pawR * 0.92f, lk.pawR, ly, P_FPAW, 0.86f, 1.f);
    };
    auto hindLeg = [&](const float2* j, uint8_t ly) {
        seg(j[0], j[1], lk.thighR, lk.shinR * pose.thighEnd, ly, P_THIGH, 0.f, 0.35f);
        seg(j[1], j[2], lk.shinR, lk.metaR * 1.05f, ly, P_SHIN, 0.35f, 0.62f);
        seg(j[2], j[3], lk.metaR, lk.metaR * 0.95f, ly, P_META, 0.62f, 0.86f);
        float2 d = norm2(sub(j[4], j[3]));
        seg(j[3], sub(j[4], mul(d, lk.pawR * 0.35f)), lk.pawR * 0.95f, lk.pawR, ly, P_HPAW, 0.86f, 1.f);
    };
    hindLeg(pose.hf, L_FARLEG);
    frontLeg(pose.ff, L_FARLEG);
    for (int i = 0; i + 1 < pose.tailN; ++i) {
        float t0 = (float)i / (pose.tailN - 1), t1 = (float)(i + 1) / (pose.tailN - 1);
        float r0 = lk.tailR0 + (lk.tailR1 - lk.tailR0) * t0, r1 = lk.tailR0 + (lk.tailR1 - lk.tailR0) * t1;
        if (i == pose.tailN - 2) r1 *= 0.85f;
        uint8_t ly = i == 0 ? L_BODY : (i >= pose.tailFrontFrom ? L_TAILFRONT : L_TAILBACK);
        seg(pose.tail[i], pose.tail[i + 1], r0, r1, ly, P_TAIL, t0, t1);
    }
    for (int i = 0; i < 3; ++i) seg(pose.spine[i], pose.spine[i + 1], pose.spineR[i], pose.spineR[i + 1], L_BODY, P_TORSO, i / 3.f, (i + 1) / 3.f);
    if (pose.bellyR > 0) seg(pose.belly0, pose.belly1, pose.bellyR, pose.bellyR * 0.96f, L_BODY, P_TORSO, 0.3f, 0.75f);
    seg(pose.neck0, pose.neck1, pose.neckR0, pose.neckR1, L_BODY, P_NECK, 1.f, 1.15f);
    hindLeg(pose.hn, L_BODY);
    frontLeg(pose.fn, pose.frontOverHead ? (uint8_t)L_OVER : (uint8_t)L_BODY);

    // ---- Smoking: weißer Latz vorne an der Brust (Ellipse, läuft über Teilgrenzen)
    float2 bibC(0, 0), bibAx(0, 1);
    float bibRx = 1, bibRy = 1;
    if (lk.pattern == 1) {
        // vom Kinn bis vorne zwischen die Vorderbeine (die Katze schaut in +x)
        float2 chin = add(pose.neck1, float2(pose.neckR1 * 0.5f, -pose.neckR1 * 0.6f));
        float2 chest = add(pose.fn[0], float2(lk.upperArmR * 0.9f, -lk.upperArmR * 0.3f));
        bibAx = norm2(sub(chin, chest));
        bibRy = len2(sub(chin, chest)) * 0.62f + 1.2f;
        bibRx = lk.chestR * 0.42f;
        bibC = add(mix(chest, chin, 0.45f), float2(bibRx * 0.35f, 0.f));
    }
    auto inBib = [&](float2 p) {
        if (lk.pattern != 1) return false;
        float2 q = sub(p, bibC);
        float y = dot2(q, bibAx), x = dot2(q, float2(bibAx.y, -bibAx.x));
        return (x / bibRx) * (x / bibRx) + (y / bibRy) * (y / bibRy) < 1.f;
    };

    // ---- Kopf: Achsen wie im Shader
    float cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pose.pitch), sp = std::sin(pose.pitch);
    float cr = std::cos(pose.roll), sr = std::sin(pose.roll);
    float3 F(cy, 0, sy), U(0, 1, 0), S(-sy, 0, cy);
    {
        float3 F2(F.x * cp + U.x * sp, F.y * cp + U.y * sp, F.z * cp + U.z * sp);
        float3 U2(U.x * cp - F.x * sp, U.y * cp - F.y * sp, U.z * cp - F.z * sp);
        float3 U3(U2.x * cr + S.x * sr, U2.y * cr + S.y * sr, U2.z * cr + S.z * sr);
        float3 S3(S.x * cr - U2.x * sr, S.y * cr - U2.y * sr, S.z * cr - U2.z * sr);
        F = F2;
        U = U3;
        S = S3;
    }
    auto proj = [&](float lx, float ly, float lz) {
        return float3(pose.head.x + (F.x * lx + U.x * ly + S.x * lz) * R, pose.head.y + (F.y * lx + U.y * ly + S.y * lz) * R,
                      (F.z * lx + U.z * ly + S.z * lz) * R);
    };
    struct Sph {
        float x, y, z, r;
    };
    float3 mz = proj(lk.muzzleFwd, -lk.muzzleDown, 0);
    float3 ck1 = proj(lk.cheekFwd, -lk.cheekDown, lk.cheekSide), ck2 = proj(lk.cheekFwd, -lk.cheekDown, -lk.cheekSide);
    const Sph sph[4] = {{pose.head.x, pose.head.y, 0, R}, {mz.x, mz.y, mz.z, lk.muzzleR * R}, {ck1.x, ck1.y, ck1.z, lk.cheekR * R},
                        {ck2.x, ck2.y, ck2.z, lk.cheekR * R}};
    struct Ear {
        float2 a, tip, c, ia, itip, ic;
        float z;
        bool innerVisible;
    };
    Ear ears[2];
    for (int e = 0; e < 2; ++e) {
        float side = e == 0 ? 1.f : -1.f;
        float3 b0, tip, b2, n;
        if (lk.earType == 0) {
            b0 = float3(0.25f, 0.86f, 0.24f * side);    // innere Ecke (oben auf dem Kopf)
            b2 = float3(-0.18f, 0.62f, 0.8f * side);    // äußere Ecke (seitlich)
            tip = float3(0.02f, 1.5f, 0.66f * side);
            n = float3(0.85f, 0.2f, 0.45f * side);      // Ohröffnung zeigt nach vorne
        } else {   // Faltohr: kleine runde Kappe, nach vorne geklappt
            b0 = float3(0.5f, 0.78f, 0.4f * side);
            b2 = float3(-0.12f, 0.9f, 0.46f * side);
            tip = float3(0.58f, 1.0f, 0.56f * side);
            n = float3(0.3f, 0.8f, 0.5f * side);
        }
        float3 bc((b0.x + b2.x) * 0.5f, (b0.y + b2.y) * 0.5f, (b0.z + b2.z) * 0.5f);
        tip = float3(bc.x + (tip.x - bc.x) * lk.earSize - 0.45f * pose.earBack, bc.y + (tip.y - bc.y) * lk.earSize - 0.25f * pose.earBack,
                     bc.z + (tip.z - bc.z) * lk.earSize + 0.25f * pose.earBack * side);
        float3 P0 = proj(b0.x, b0.y, b0.z), P1 = proj(tip.x, tip.y, tip.z), P2 = proj(b2.x, b2.y, b2.z);
        float nz = (F.z * n.x + U.z * n.y + S.z * n.z) / std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        Ear& E = ears[e];
        E.a = float2(P0.x, P0.y);
        E.tip = float2(P1.x, P1.y);
        E.c = float2(P2.x, P2.y);
        float2 bm = mix(E.a, E.c, 0.5f);
        E.ia = mix(E.a, bm, 0.42f);
        E.itip = mix(E.tip, bm, 0.28f);
        E.ic = mix(E.c, bm, 0.42f);
        E.z = P1.z;
        E.innerVisible = lk.earType == 0 && nz > 0.25f;
    }
    const int nearEar = ears[0].z >= ears[1].z ? 0 : 1;
    const float earRound = R * (lk.earType == 0 ? 0.07f : 0.16f);

    // Lage auf der ganzen Wirbelsäule (Hüfte 0 .. Schulter 1) und quer dazu (oben +1, unten -1)
    float spineLen[3], spineTotal = 0.f;
    for (int k = 0; k < 3; ++k) {
        spineLen[k] = len2(sub(pose.spine[k + 1], pose.spine[k]));
        spineTotal += spineLen[k];
    }
    auto spineCoord = [&](float2 p, float& u, float& v) {
        float best = 1e9f, acc = 0.f;
        u = 0.f;
        v = 0.f;
        for (int k = 0; k < 3; ++k) {
            float2 a = pose.spine[k], ab = sub(pose.spine[k + 1], a);
            float l2 = std::max(dot2(ab, ab), 1e-6f);
            float t = clamp01(dot2(sub(p, a), ab) / l2);
            float2 dv = sub(p, add(a, mul(ab, t)));
            float d = len2(dv);
            if (d < best) {
                best = d;
                float r = pose.spineR[k] + (pose.spineR[k + 1] - pose.spineR[k]) * t;
                u = (acc + spineLen[k] * t) / std::max(spineTotal, 1e-3f);
                float cr2 = ab.x * dv.y - ab.y * dv.x;
                v = std::max(-1.2f, std::min(1.2f, cr2 / std::sqrt(l2) / std::max(r, 1e-3f)));
            }
            acc += spineLen[k];
        }
    };

    // ---- 1. Abtasten
    // Körperebenen: jede Röhre nur in ihrem eigenen Rahmen auswerten (spart ~95 % der Rechenzeit)
    const float kBody = 1.6f, kTail = 0.8f;
    std::vector<Hit> acc((size_t)kSlots * N);
    std::vector<uint8_t> touched(N, 0);
    for (size_t k = 0; k < segs.size(); ++k) {
        const Seg& sg = segs[k];
        const float kk = sg.part == P_TAIL ? kTail : kBody;
        const float m = std::max(sg.ra, sg.rb) + 4.f * kk + 3.f;   // weite Reichweite der weichen Vereinigung
        int cx0 = std::max(0, (int)std::floor((std::min(sg.a.x, sg.b.x) - m) * ppc) - c0);
        int cx1 = std::min(W - 1, (int)std::ceil((std::max(sg.a.x, sg.b.x) + m) * ppc) - c0);
        int ry0 = std::max(0, rTop - 1 - (int)std::ceil((std::max(sg.a.y, sg.b.y) + m) * ppc));
        int ry1 = std::min(H - 1, rTop - 1 - (int)std::floor((std::min(sg.a.y, sg.b.y) - m) * ppc));
        for (int r = ry0; r <= ry1; ++r)
            for (int c = cx0; c <= cx1; ++c) {
                const float2 p((c0 + c + 0.5f) * inv, (rTop - r - 0.5f) * inv);
                size_t i = (size_t)r * W + c;
                addSeg(acc[(size_t)slotOf(sg.layer) * N + i], sg, (int)k, p, kk);
                touched[i] = 1;
            }
    }
    // Kopf: nur im Rahmen um Kopf und Ohren
    int hc0 = 0, hc1 = W - 1, hr0 = 0, hr1 = H - 1;
    {
        float hx0 = pose.head.x - R * 1.7f, hx1 = pose.head.x + R * 1.7f, hy0 = pose.head.y - R * 1.7f, hy1 = pose.head.y + R * 1.9f;
        hc0 = std::max(0, (int)std::floor(hx0 * ppc) - c0);
        hc1 = std::min(W - 1, (int)std::ceil(hx1 * ppc) - c0);
        hr0 = std::max(0, rTop - 1 - (int)std::ceil(hy1 * ppc));
        hr1 = std::min(H - 1, rTop - 1 - (int)std::floor(hy0 * ppc));
    }
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            const float2 p((c0 + c + 0.5f) * inv, (rTop - r - 0.5f) * inv);
            const size_t i = (size_t)r * W + c;
            const bool inHeadBox = c >= hc0 && c <= hc1 && r >= hr0 && r <= hr1;
            if (!inHeadBox && !touched[i]) continue;
            int top = acc[(size_t)4 * N + i].d < 0.f ? 4 : -1;   // Vorderbein vor dem Kopf
            if (top < 0) {
                // Kopf (vorderste Ebene)
                float dHead = inHeadBox ? 0.f : 1e9f;
                if (inHeadBox) dHead = len2(sub(p, float2(sph[0].x, sph[0].y))) - sph[0].r;
                const float kh = R * 0.16f;
                for (int k = 1; k < 4 && inHeadBox; ++k) {
                    float dk = len2(sub(p, float2(sph[k].x, sph[k].y))) - sph[k].r;
                    float w = clamp01(0.5f + 0.5f * (dHead - dk) / kh);
                    dHead = dHead + (dk - dHead) * w - kh * w * (1 - w);
                }
                if (dHead < 0.f) {
                    float bestZ = -1e9f;
                    int bs = 0;
                    for (int k = 0; k < 4; ++k) {
                        float2 q = sub(p, float2(sph[k].x, sph[k].y));
                        float rr = sph[k].r * sph[k].r - dot2(q, q);
                        if (rr > 0) {
                            float z = sph[k].z + std::sqrt(rr);
                            if (z > bestZ) {
                                bestZ = z;
                                bs = k;
                            }
                        }
                    }
                    float3 P(p.x, p.y, bestZ > -1e8f ? bestZ : 0.f);
                    float3 n = bestZ > -1e8f ? float3((P.x - sph[bs].x) / sph[bs].r, (P.y - sph[bs].y) / sph[bs].r, (P.z - sph[bs].z) / sph[bs].r)
                                             : float3(0, 0, 1);
                    float3 q(P.x - pose.head.x, P.y - pose.head.y, P.z);
                    float lx = (q.x * F.x + q.y * F.y + q.z * F.z) / R, ly = (q.x * U.x + q.y * U.y + q.z * U.z) / R;
                    Region rg = R_FUR;
                    if (lk.pattern == 0) {
                        if ((bs == 1 && ly < -0.2f) || (ly < -0.52f && lx > 0.05f)) rg = R_CREAM;
                    } else {
                        if ((bs == 1 && ly < -0.32f) || (ly < -0.62f && lx > 0.2f)) rg = R_CREAM;
                    }
                    layer[i] = L_HEAD;
                    region[i] = rg;
                    nrm[i] = n;
                    if (lx > 0.3f) flags[i] |= F_FACE;
                    continue;
                }
                int earHit = -1;
                for (int e = 0; e < 2 && earHit < 0 && inHeadBox; ++e) {
                    int ee = e == 0 ? nearEar : 1 - nearEar;
                    if (triDist(p, ears[ee].a, ears[ee].tip, ears[ee].c) - earRound < 0.f) earHit = ee;
                }
                if (earHit >= 0) {
                    const Ear& E = ears[earHit];
                    layer[i] = L_HEAD;
                    bool inner = E.innerVisible && earHit == nearEar && triDist(p, E.ia, E.itip, E.ic) < 0.f;
                    region[i] = inner ? R_INNEREAR : R_FUR;
                    flags[i] |= F_EAR | (earHit != nearEar ? F_FAREAR : 0);
                    nrm[i] = earHit == nearEar ? float3(0.f, 0.5f, 0.86f) : float3(0.f, 0.2f, 0.3f);
                    continue;
                }
            }
            // Körperebenen
            for (int ly = 3; ly >= 0 && top < 0; --ly)
                if (acc[(size_t)ly * N + i].d < 0.f) top = ly;
            if (top < 0) continue;
            const Hit& h = acc[(size_t)top * N + i];
            const Seg& sg = segs[h.bestIdx];
            layer[i] = top == 4 ? (uint8_t)L_OVER : (uint8_t)top;
            part[i] = sg.part;
            float u = sg.u0 + (sg.u1 - sg.u0) * h.t;
            Region rg = R_FUR;
            if (lk.pattern == 0) {
                switch (sg.part) {
                    case P_TORSO: {
                        float su, sv;
                        spineCoord(p, su, sv);
                        if (sv < -0.5f) rg = R_CREAM;
                        else {
                            // Makrelen-Streifen: vom Rücken nach unten, leicht nach hinten gebogen, unten schmaler
                            float ph = su * lk.stripeFreq + 0.09f * (1.f - sv) * (1.f - sv);
                            float s = std::sin(6.2832f * ph);
                            if (sv > -0.3f && s > 0.5f - 0.3f * std::max(0.f, sv)) rg = R_STRIPE;
                        }
                        break;
                    }
                    case P_NECK:
                        if (h.v < -0.05f) rg = R_CREAM;
                        break;
                    case P_TAIL:
                        if (u > 0.88f) rg = R_TIP;
                        else if (std::sin(6.2832f * (u * 4.5f + 0.15f)) > 0.42f) rg = R_STRIPE;
                        break;
                    case P_FPAW:
                    case P_HPAW: rg = R_CREAM; break;
                    case P_FLOWER:
                    case P_SHIN:
                        if (std::sin(6.2832f * (u * 3.f + 0.1f)) > 0.6f) rg = R_STRIPE;
                        break;
                    case P_THIGH:
                        if (h.v > -0.2f && std::sin(6.2832f * (h.v * 1.3f + u * 2.f)) > 0.65f) rg = R_STRIPE;
                        break;
                    default: break;
                }
            } else {
                if (sg.part == P_FPAW || sg.part == P_HPAW) rg = R_CREAM;
                else if (sg.part == P_FLOWER && u > 0.74f) rg = R_CREAM;
                else if (sg.part == P_META && u > 0.7f) rg = R_CREAM;
                else if ((sg.part == P_TORSO || sg.part == P_NECK || sg.part == P_FUPPER) && inBib(p)) rg = R_CREAM;
            }
            region[i] = rg;
            float nl = std::sqrt(h.n.x * h.n.x + h.n.y * h.n.y + h.n.z * h.n.z);
            nrm[i] = nl > 0 ? float3(h.n.x / nl, h.n.y / nl, h.n.z / nl) : float3(0, 0, 1);
        }
    }

    auto L = [&](int c, int r) -> int { return (c < 0 || r < 0 || c >= W || r >= H) ? (int)L_NONE : (int)layer[(size_t)r * W + c]; };
    const int dc8[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dr8[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    // ---- 2a. Fellbereiche aufräumen: einzelne Pixel ohne gleichen Nachbarn schlucken
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> nr = region;
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c) {
                size_t i = (size_t)r * W + c;
                if (layer[i] == L_NONE || region[i] == R_FUR || region[i] == R_INNEREAR) continue;
                int same = 0;
                int count[R_COUNT] = {};
                for (int k = 0; k < 8; ++k) {
                    int cc = c + dc8[k], rr = r + dr8[k];
                    if (L(cc, rr) != layer[i]) continue;
                    uint8_t rg = region[(size_t)rr * W + cc];
                    if (k < 4 && rg == region[i]) ++same;
                    ++count[rg];
                }
                if (same == 0) {
                    int best = R_FUR;
                    for (int k = 1; k < R_COUNT; ++k)
                        if (count[k] > count[best]) best = k;
                    nr[i] = (uint8_t)best;
                }
            }
        region.swap(nr);
    }

    // ---- 2b. Lichtstufen
    for (size_t i = 0; i < N; ++i) {
        if (layer[i] == L_NONE) continue;
        // Pfoten etwas heller (sonst wirken weiße Pfoten grau)
        float bias = (part[i] == P_FPAW || part[i] == P_HPAW) ? 0.3f : 0.f;
        int bd = lightBand(nrm[i], bias);
        if (layer[i] == L_FARLEG || (flags[i] & F_FAREAR)) bd = std::max(0, bd - 1);
        band[i] = (uint8_t)bd;
    }
    // Schatten unter dem Kopf (Kinn wirft Schatten auf Hals und Brust)
    for (int r = 1; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            size_t i = (size_t)r * W + c;
            if (layer[i] == L_NONE || layer[i] == L_HEAD) continue;
            for (int up = 1; up <= 2; ++up) {
                if (r - up < 0) break;
                size_t j = (size_t)(r - up) * W + c;
                if (layer[j] == L_HEAD && !(flags[j] & F_EAR)) {
                    band[i] = 0;
                    break;
                }
            }
        }
    // Einzelne Lichtpixel entfernen
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t> nb = band;
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c) {
                size_t i = (size_t)r * W + c;
                if (layer[i] == L_NONE) continue;
                int count[3] = {}, same = 0;
                for (int k = 0; k < 8; ++k) {
                    int cc = c + dc8[k], rr = r + dr8[k];
                    if (L(cc, rr) != layer[i]) continue;
                    size_t j = (size_t)rr * W + cc;
                    if (region[j] != region[i]) continue;
                    ++count[band[j]];
                    if (k < 4 && band[j] == band[i]) ++same;
                }
                if (same == 0 && count[0] + count[1] + count[2] > 0) {
                    int best = 0;
                    for (int k = 1; k < 3; ++k)
                        if (count[k] > count[best]) best = k;
                    nb[i] = (uint8_t)best;
                }
            }
        band.swap(nb);
    }

    if (std::getenv("PIXDEBUG")) {
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                size_t i = (size_t)r * W + c;
                char ch = '.';
                if (layer[i] != L_NONE) ch = std::getenv("PIXBAND") ? (char)('0' + band[i]) : "?FSCTIN"[region[i]];
                if (layer[i] == L_HEAD && (flags[i] & F_EAR)) ch = (char)(ch + 32);
                std::fputc(ch, stderr);
            }
            std::fputc('\n', stderr);
        }
    }
    // ---- 3. Farben
    const Palette pal = paletteFor(lk);
    std::vector<uint32_t> img(N, 0);
    for (size_t i = 0; i < N; ++i) {
        if (layer[i] == L_NONE) continue;
        const Ramp& rp = pal.r[region[i]];
        img[i] = band[i] == 0 ? rp.shadow : (band[i] == 2 ? rp.light : rp.mid);
    }

    // ---- 4. Konturen
    std::vector<uint32_t> res = img;
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            size_t i = (size_t)r * W + c;
            int ly = layer[i];
            if (ly == L_NONE) continue;
            bool line = false;
            for (int k = 0; k < 4 && !line; ++k) {
                int cc = c + dc8[k], rr = r + dr8[k];
                int nl = L(cc, rr);
                if (nl == L_NONE) {
                    line = true;
                } else if (nl < ly) {
                    if (ly == L_HEAD) {
                        // Kopf über Körper: abgrenzen, nur der Hinterkopf geht in den Hals über
                        size_t j = (size_t)rr * W + cc;
                        line = (flags[i] & (F_FACE | F_EAR)) != 0 || part[j] != P_NECK;
                    } else {
                        line = true;
                    }
                }
            }
            if (line) {
                uint32_t col = pal.r[region[i]].outline;
                // oben liegende Außenkante bei schwarzem Fell: Randlicht statt Schwarz
                if (lk.pattern == 1 && region[i] == R_FUR && L(c, r - 1) == L_NONE && band[i] > 0) col = pal.rim;
                res[i] = col;
            }
        }

    // ---- 5. Gesicht
    auto toPix = [&](float x, float y, int& pc, int& pr) {
        pc = (int)std::floor(x * ppc) - c0;
        pr = rTop - 1 - (int)std::floor(y * ppc);
    };
    auto isHead = [&](int pc, int pr) { return L(pc, pr) == L_HEAD && !(flags[(size_t)pr * W + pc] & F_EAR); };
    auto stamp = [&](const Stamp& sp, int cx, int cyy) {
        for (int y = 0; y < sp.h; ++y)
            for (int x = 0; x < sp.w; ++x) {
                char ch = sp.rows[y][x];
                int pc = cx + x - (sp.w - 1) / 2, pr = cyy + y - (sp.h - 1) / 2;
                if (!isHead(pc, pr)) continue;
                uint32_t col;
                switch (ch) {
                    case 'e': col = pal.iris; break;
                    case 'E': col = pal.irisDark; break;
                    case 'k': col = pal.pupil; break;
                    case 'w': col = pal.white; break;
                    case 'o': col = pal.lid; break;
                    case 'n': col = pal.r[R_NOSE].mid; break;
                    case 'm': col = pal.mouth; break;
                    case 'c': col = pal.closedLid; break;
                    default: continue;
                }
                res[(size_t)pr * W + pc] = col;
            }
    };
    const bool isFatos = lk.pattern == 0;
    for (int e = 0; e < 2; ++e) {
        float side = e == 0 ? 1.f : -1.f;
        float lx = std::cos(lk.eyeEl) * std::cos(lk.eyeAz), ly = std::sin(lk.eyeEl), lz = side * std::cos(lk.eyeEl) * std::sin(lk.eyeAz);
        float3 P = proj(lx, ly, lz);
        float facing = P.z / R;
        if (facing < 0.15f) continue;
        int pc, pr;
        toPix(P.x, P.y, pc, pr);
        bool front = F.z > 0.6f;   // Kopf zu uns gedreht
        bool far = !front && facing < 0.45f;
        const Stamp* sp;
        if (pose.eyeOpen < 0.2f && pose.squint > 0.5f) sp = isFatos ? (front ? &kFatosHappy : &kFatosHappySide) : (front ? &kKostaHappy : &kKostaHappySide);
        else if (pose.eyeOpen < 0.2f) sp = isFatos ? (front ? &kFatosClosed : &kFatosClosedSide) : (front ? &kKostaClosed : &kKostaClosedSide);
        else if (pose.eyeOpen < 0.75f) sp = isFatos ? (front ? &kFatosHalf : &kFatosHalfSide) : &kKostaHalf;
        else if (far) sp = isFatos ? &kFatosFar : &kKostaFar;
        else sp = isFatos ? (front ? &kFatosFront : &kFatosSide) : (front ? &kKostaFront : &kKostaSide);
        stamp(*sp, pc, pr);
    }
    // Nase (+ Mund von vorne)
    {
        float3 Np = proj(lk.muzzleFwd + lk.muzzleR * 0.92f, -lk.muzzleDown + lk.muzzleR * 0.38f, 0.f);
        int pc, pr;
        toPix(Np.x, Np.y, pc, pr);
        for (int k = 0; k < 3 && !isHead(pc, pr); ++k) pc += (Np.x > pose.head.x ? -1 : 1);
        if (isHead(pc, pr)) {
            bool front = F.z > 0.6f;
            uint32_t nose = pal.r[R_NOSE].mid;
            res[(size_t)pr * W + pc] = nose;
            if (front && isHead(pc + 1, pr)) res[(size_t)pr * W + pc + 1] = nose;
            // offenes Maul (Gähnen, Miauen, Lecken)
            if (pose.mouth > 0.25f) {
                auto put = [&](int x, int y, uint32_t col) {
                    if (isHead(x, y)) res[(size_t)y * W + x] = col;
                };
                const uint32_t dark = pal.lid, inner = pal.r[R_NOSE].shadow;
                const bool wide = pose.mouth > 0.65f;
                if (front) {
                    put(pc, pr + 1, dark);
                    put(pc + 1, pr + 1, dark);
                    if (wide) {
                        put(pc, pr + 2, inner);
                        put(pc + 1, pr + 2, inner);
                        put(pc, pr + 3, dark);
                        put(pc + 1, pr + 3, dark);
                    }
                } else {
                    put(pc, pr + 1, dark);
                    put(pc - 1, pr + 1, dark);
                    if (wide) {
                        put(pc - 1, pr + 2, inner);
                        put(pc, pr + 2, inner);
                        put(pc - 1, pr + 3, dark);
                    }
                }
            }
        }
    }
    // Stirnzeichnung (Fatos): drei kurze Striche über den Augen
    if (isFatos) {
        const float zs[3] = {-0.2f, 0.f, 0.2f};
        for (float z : zs) {
            float3 Pm = proj(0.62f, 0.66f, z);
            if (Pm.z / R < 0.3f) continue;
            int pc, pr;
            toPix(Pm.x, Pm.y, pc, pr);
            for (int k = 0; k < 2; ++k)
                if (isHead(pc, pr + k) && res[(size_t)(pr + k) * W + pc] != pal.r[R_FUR].outline)
                    res[(size_t)(pr + k) * W + pc] = band[(size_t)(pr + k) * W + pc] == 2 ? pal.r[R_STRIPE].mid : pal.r[R_STRIPE].shadow;
        }
    }

    // ---- Ausgabe (bei Blick nach links gespiegelt)
    out.w = W;
    out.h = H;
    out.px.assign(N, 0);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) out.px[(size_t)r * W + (facingLeft ? W - 1 - c : c)] = res[(size_t)r * W + c];
    out.originY = rTop;
    out.originX = facingLeft ? W + c0 : -c0;
}

}  // namespace cat
