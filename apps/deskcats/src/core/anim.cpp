// DeskCats – Bewegungen (siehe anim.h)
#include "anim.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cat {

namespace {

constexpr float kPi = 3.14159265f;

float2 add(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
float2 sub(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
float2 mul(float2 a, float s) { return float2(a.x * s, a.y * s); }
float2 mix(float2 a, float2 b, float t) { return float2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
float len(float2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
float smooth(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}
float frac(float x) { return x - std::floor(x); }
float angleOf(float2 v) { return std::atan2(v.y, v.x); }
float2 dirOf(float a) { return float2(std::cos(a), std::sin(a)); }
float2 rot(float2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return float2(v.x * c - v.y * s, v.x * s + v.y * c);
}
float2 rotAround(float2 p, float2 c, float a) { return add(c, rot(sub(p, c), a)); }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a + 3.f * kPi, 2.f * kPi) - kPi;
    return a + d * t;
}

// ------------------------------------------------------------------ Knochen und Beine

struct Bones {
    float upperArm, foreArm, fpaw, thigh, shin, meta, hpaw;
};

Bones bonesOf(const Pose& s) {
    Bones b;
    b.upperArm = len(sub(s.fn[1], s.fn[0]));
    b.foreArm = len(sub(s.fn[2], s.fn[1]));
    b.fpaw = len(sub(s.fn[3], s.fn[2]));
    b.thigh = len(sub(s.hn[1], s.hn[0]));
    b.shin = len(sub(s.hn[2], s.hn[1]));
    b.meta = len(sub(s.hn[3], s.hn[2]));
    b.hpaw = len(sub(s.hn[4], s.hn[3]));
    return b;
}

// Mittleres Gelenk zwischen a und b (Knochen l1, l2). side > 0: Gelenk links der Richtung a->b.
float2 ikJoint(float2 a, float2 b, float l1, float l2, float side) {
    float2 d = sub(b, a);
    float L = len(d);
    if (L < 1e-4f) return add(a, float2(0, -l1));
    float2 u = mul(d, 1.f / L);
    L = std::max(std::fabs(l1 - l2) + 1e-3f, std::min(l1 + l2 - 1e-3f, L));
    float x = (l1 * l1 - l2 * l2 + L * L) / (2.f * L);
    float y = std::sqrt(std::max(0.f, l1 * l1 - x * x));
    float2 perp = side > 0 ? float2(-u.y, u.x) : float2(u.y, -u.x);
    return add(a, add(mul(u, x), mul(perp, y)));
}

// Vorderbein: Schulter, Zehen, Richtung der Pfote (Handgelenk -> Zehen). Ellbogen zeigt nach hinten.
void frontIK(float2* j, float2 shoulder, float2 toe, float pawAng, const Bones& B) {
    j[0] = shoulder;
    j[3] = toe;
    j[2] = sub(toe, mul(dirOf(pawAng), B.fpaw));
    j[1] = ikJoint(j[0], j[2], B.upperArm, B.foreArm, -1.f);
}

// Hinterbein: Hüfte, Zehen, Pfotenrichtung (Ballen -> Zehen), Mittelfuß-Richtung (Ballen -> Sprunggelenk). Knie nach vorne.
void hindIK(float2* j, float2 hip, float2 toe, float pawAng, float metaAng, const Bones& B) {
    j[0] = hip;
    j[4] = toe;
    j[3] = sub(toe, mul(dirOf(pawAng), B.hpaw));
    j[2] = add(j[3], mul(dirOf(metaAng), B.meta));
    j[1] = ikJoint(j[0], j[2], B.thigh, B.shin, 1.f);
}

// ------------------------------------------------------------------ ganze Haltung verschieben / drehen

template <class F>
void eachPoint(Pose& p, F f) {
    for (auto& v : p.spine) f(v);
    f(p.belly0);
    f(p.belly1);
    f(p.neck0);
    f(p.neck1);
    f(p.head);
    for (auto& v : p.fn) f(v);
    for (auto& v : p.ff) f(v);
    for (auto& v : p.hn) f(v);
    for (auto& v : p.hf) f(v);
    for (int i = 0; i < p.tailN; ++i) f(p.tail[i]);
    f(p.shadowC);
    f(p.anchor);
}

void movePose(Pose& p, float2 d) {
    eachPoint(p, [&](float2& v) { v = add(v, d); });
}

void rotatePose(Pose& p, float2 c, float a) {
    eachPoint(p, [&](float2& v) { v = rotAround(v, c, a); });
    // Bildschirmdrehung auf den Kopf übertragen: im Profil wirkt sie wie Nicken, von vorne wie Neigen
    p.pitch += a * std::cos(p.yaw);
    p.roll += a * std::sin(p.yaw);
}

// Körperpunkte (Rumpf, Hals, Kopf, Gelenkansätze) zwischen Hüfte und Schulter heben/senken
void bobBody(Pose& p, float hipDy, float shDy, float headDy) {
    for (int i = 0; i < 4; ++i) p.spine[i].y += lerpf(hipDy, shDy, i / 3.f);
    p.belly0.y += lerpf(hipDy, shDy, 0.3f);
    p.belly1.y += lerpf(hipDy, shDy, 0.7f);
    p.neck0.y += shDy;
    p.head.y += headDy;
    p.neck1.y += headDy;
}

void setNeck1(const Look& lk, Pose& p, float fx, float fy) { p.neck1 = add(p.head, float2(-lk.headR * fx, -lk.headR * fy)); }

// Schwanz pendeln: Punkte werden fortschreitend um den Ansatz gedreht. ground: nur die Spitze bewegt sich.
void swayTail(const Look& lk, Pose& p, float amp, float phase) {
    if (p.tailN < 3 || amp == 0.f) return;
    int n = p.tailN;
    bool onGround = p.tail[n - 1].y < lk.tailR0 * 1.2f;
    int from = onGround ? (int)(n * 0.6f) : 1;
    float2 pivot = p.tail[from - 1];
    for (int i = from; i < n; ++i) {
        float s = (float)(i - from + 1) / (n - from);
        float a = amp * std::sin(2.f * kPi * phase - 2.2f * s) * s;
        p.tail[i] = rotAround(p.tail[i], pivot, a);
        if (onGround) p.tail[i].y = std::max(p.tail[i].y, lk.tailR1 * 0.9f);
    }
}

// Atmen: Brustkorb hebt und senkt sich ein wenig
void breathe(Pose& p, float phase, float amount) {
    float s = std::sin(2.f * kPi * phase) * amount;
    p.spineR[1] *= 1.f + 0.5f * s;
    p.spineR[2] *= 1.f + s;
    p.spineR[3] *= 1.f + 0.8f * s;
    p.bellyR *= 1.f + 0.6f * s;
}

void applyOverlay(const Look& lk, Pose& p, const Overlay& ov, bool tailOnGround) {
    (void)tailOnGround;
    p.yaw = std::max(-0.3f, std::min(1.45f, p.yaw + ov.lookYaw * 0.85f));
    p.pitch += ov.lookPitch * 0.35f;
    p.eyeOpen = std::min(p.eyeOpen, 1.f - ov.blink);
    p.earBack = std::max(p.earBack, 0.45f * ov.earTwitch);
    if (ov.breath != 0.f) breathe(p, ov.breath, 0.022f);
    swayTail(lk, p, 0.16f * ov.tailAmp, ov.tailSway);
}

// ------------------------------------------------------------------ Zyklen

Pose walkPose(const Look& lk, int frame) {
    Pose p = poseStand(lk);
    const Pose base = p;
    const Bones B = bonesOf(base);
    const int n = 8;
    const float ph = (float)(frame % n) / n;
    const float stride = 25.6f, beta = 0.62f, E = stride * beta;   // 4 px pro Bild bei 1,25 px/cm
    float hipDy = 0.3f * std::cos(4.f * kPi * (ph - 0.05f));
    float shDy = 0.3f * std::cos(4.f * kPi * (ph - 0.3f));
    float headDy = 0.3f * std::cos(4.f * kPi * (ph - 0.38f)) + shDy * 0.5f;
    bobBody(p, hipDy, shDy, headDy);
    p.yaw = 0.32f;
    setNeck1(lk, p, 0.42f, 0.5f);
    const float aF0 = angleOf(sub(base.fn[3], base.fn[2]));
    const float aH0 = angleOf(sub(base.hn[4], base.hn[3]));
    const float mH0 = angleOf(sub(base.hn[2], base.hn[3]));
    const float xF = base.fn[3].x - 0.6f, xH = base.hn[4].x + 0.4f;
    const float yF = base.fn[3].y, yH = base.hn[4].y;
    auto foot = [&](float off, bool front, float2* j, float2 joint) {
        float q = frac(ph + off);
        float x, y, sw = 0.f, push = 0.f;
        float x0 = front ? xF : xH;
        if (q < beta) {
            float u = q / beta;
            x = x0 + E * (0.5f - u);
            y = 0.f;
            push = smooth((u - 0.7f) / 0.3f);   // Ende der Standphase: Ferse hebt ab
        } else {
            float u = (q - beta) / (1.f - beta);
            x = x0 + E * (-0.5f + smooth(u));
            y = (front ? 2.6f : 2.2f) * std::sin(kPi * u);
            sw = std::sin(kPi * u);
        }
        if (front) frontIK(j, joint, float2(x, yF + y), aF0 - 1.15f * sw - 0.35f * push, B);
        else hindIK(j, joint, float2(x, yH + y), aH0 - 0.9f * sw - 0.3f * push, mH0 + 0.55f * sw - 0.2f * push, B);
    };
    float2 sJ = add(base.fn[0], float2(0, shDy)), hJ = add(base.hn[0], float2(0, hipDy));
    float2 sJf = add(base.fn[0], float2(-1.2f, shDy + 0.1f)), hJf = add(base.hn[0], float2(1.f, hipDy + 0.1f));
    foot(0.f, false, p.hn, hJ);
    foot(0.75f, true, p.fn, sJ);
    foot(0.5f, false, p.hf, hJf);
    foot(0.25f, true, p.ff, sJf);
    swayTail(lk, p, 0.1f, ph);
    return p;
}

// Rennen als Sprunglauf: Hinterbeine und Vorderbeine jeweils fast gemeinsam, mit Flugphase
Pose runPose(const Look& lk, int frame) {
    Pose p = poseStand(lk);
    const Pose base = p;
    const Bones B = bonesOf(base);
    struct Key {
        float bodyY, pitch, lenS, flex;
        float fx, fy, fa;   // Vorderpfote: x relativ zur Schulter, Höhe, Pfotenwinkel-Änderung
        float hx, hy, ha, hm;
    };
    // 6 Bilder, 8 px/Bild (6,4 cm): Pfoten auf dem Boden wandern pro Bild 6,4 cm nach hinten
    const Key K[6] = {
        {0.4f, 0.06f, 0.86f, 1.6f, 7.5f, 4.5f, -0.9f, -1.6f, 0.f, 0.f, 0.f},         // Hinterpfoten stehen, vorne greift
        {2.2f, 0.07f, 1.07f, -0.8f, 11.5f, 3.6f, -0.3f, -10.5f, 3.f, -0.9f, 0.55f},  // Abdruck, gestreckt in der Luft
        {1.1f, -0.06f, 1.04f, -0.6f, 6.6f, 0.f, 0.f, -11.5f, 6.f, -1.f, 0.75f},       // vorne landet
        {0.2f, -0.05f, 0.92f, 0.6f, 0.2f, 0.f, -0.25f, -3.f, 6.5f, -1.1f, 0.6f},      // vorne steht, hinten schwingt vor
        {1.5f, 0.f, 0.84f, 1.8f, -6.5f, 4.f, -1.1f, 4.5f, 4.5f, -0.6f, 0.2f},        // gesammelt in der Luft
        {0.6f, 0.04f, 0.86f, 1.4f, 2.f, 5.5f, -1.2f, 4.8f, 0.f, 0.f, 0.f},           // hinten landet
    };
    const Key& k = K[frame % 6];
    float2 c = mix(base.spine[0], base.spine[3], 0.5f);
    float2 hipC = base.spine[0], shC = base.spine[3];
    // Rumpf stauchen/strecken und neigen
    auto xf = [&](float2 v) {
        float2 d = sub(v, c);
        d.x *= k.lenS;
        return add(add(c, rot(d, k.pitch)), float2(0, k.bodyY - 1.0f));
    };
    for (int i = 0; i < 4; ++i) p.spine[i] = xf(base.spine[i]);
    float2 nrm = rot(float2(0, 1), k.pitch);
    p.spine[1] = add(p.spine[1], mul(nrm, k.flex * 0.8f));
    p.spine[2] = add(p.spine[2], mul(nrm, k.flex * 0.6f));
    p.belly0 = add(xf(base.belly0), mul(nrm, k.flex * 0.5f));
    p.belly1 = add(xf(base.belly1), mul(nrm, k.flex * 0.4f));
    p.neck0 = xf(base.neck0);
    p.head = add(xf(base.head), float2(1.2f, -2.2f));
    p.yaw = 0.15f;
    p.pitch = -0.12f - k.pitch * 0.5f;
    p.earBack = 0.25f;
    setNeck1(lk, p, 0.45f, 0.42f);
    float2 sJ = xf(base.fn[0]), hJ = xf(base.hn[0]);
    (void)hipC;
    (void)shC;
    const float aF0 = angleOf(sub(base.fn[3], base.fn[2]));
    const float aH0 = angleOf(sub(base.hn[4], base.hn[3]));
    const float mH0 = angleOf(sub(base.hn[2], base.hn[3]));
    const float yF = base.fn[3].y, yH = base.hn[4].y;
    frontIK(p.fn, sJ, float2(sJ.x + k.fx, yF + k.fy), aF0 + k.fa, B);
    frontIK(p.ff, add(sJ, float2(-0.8f, 0.1f)), float2(sJ.x + k.fx + 1.8f, yF + k.fy + (k.fy > 0 ? 0.8f : 0.f)), aF0 + k.fa, B);
    hindIK(p.hn, hJ, float2(hJ.x + k.hx, yH + k.hy), aH0 + k.ha, mH0 + k.hm, B);
    hindIK(p.hf, add(hJ, float2(0.8f, 0.1f)), float2(hJ.x + k.hx + 1.6f, yH + k.hy + (k.hy > 0 ? 0.6f : 0.f)), aH0 + k.ha, mH0 + k.hm, B);
    // Schwanz weht flach nach hinten
    float2 tb = xf(base.tail[0]);
    float wave = std::sin(2.f * kPi * (frame % 6) / 6.f);
    const float2 tc[5] = {tb, add(tb, float2(-5.f, 1.2f + 0.6f * wave)), add(tb, float2(-10.f, 1.6f - 0.8f * wave)),
                          add(tb, float2(-15.f, 1.0f + 1.0f * wave)), add(tb, float2(-19.5f, 1.8f - 0.6f * wave))};
    buildTailSpline(p, tc, 5, 10);
    return p;
}

// ------------------------------------------------------------------ Grundhaltungen mit Leben

Pose sitPose(const Look& lk) { return poseSit(lk); }

Pose loafPose(const Look& lk, float dangle, int frame) {
    Pose p = poseLoaf(lk, dangle);
    if (dangle >= 0.999f) {
        // Pfote pendelt ganz leicht
        float s = std::sin(2.f * kPi * (frame % 24) / 24.f);
        float2 pivot = p.fn[1];
        p.fn[2] = rotAround(p.fn[2], pivot, 0.05f * s);
        p.fn[3] = rotAround(p.fn[3], pivot, 0.07f * s);
    }
    return p;
}

// Putzen: Pfote zum Maul, lecken, dann übers Gesicht wischen (Sitzhaltung)
Pose groomPose(const Look& lk, int frame) {
    Pose p = poseSit(lk);
    const Bones B = bonesOf(poseStand(lk));
    const float R = lk.headR;
    float2 mouth = add(p.head, float2(R * 0.55f, -R * 0.55f));
    int f = frame % 16;
    float2 toe;
    float pawAng;
    p.frontOverHead = true;
    if (f < 2) {   // Pfote hebt sich
        float t = (f + 1) / 2.f;
        toe = mix(p.fn[3], add(mouth, float2(0.6f, -1.2f)), smooth(t));
        pawAng = lerpf(angleOf(sub(p.fn[3], p.fn[2])), 1.2f, t);
        p.pitch = lerpf(p.pitch, -0.35f, t);
        p.yaw = lerpf(p.yaw, 0.55f, t);
        if (f == 0) p.frontOverHead = false;
    } else if (f < 9) {   // lecken
        float s = std::sin(2.f * kPi * (f - 2) / 3.5f);
        toe = add(mouth, float2(0.6f, -1.4f + 0.3f * s));
        pawAng = 1.2f;
        p.pitch = -0.42f + 0.1f * s;
        p.yaw = 0.55f;
        p.mouth = s > 0.f ? 0.6f : 0.f;
        p.eyeOpen = 0.35f;
    } else if (f < 14) {   // übers Gesicht wischen: Pfote streicht von der Schnauze über das Auge zur Stirn
        float t = (f - 9) / 4.f;
        float a = lerpf(-0.7f, 0.75f, smooth(t));
        float2 hc = add(p.head, float2(R * 0.1f, -R * 0.05f));
        toe = add(hc, mul(dirOf(a), R * 0.62f));
        pawAng = a + 1.75f;
        p.pitch = -0.25f - 0.15f * std::sin(kPi * t);
        p.roll = 0.18f * std::sin(kPi * t);
        p.yaw = 0.8f;
        p.eyeOpen = 0.f;
        p.squint = 0.f;
        p.earBack = 0.5f * std::sin(kPi * t);
    } else {   // Pfote wieder runter
        float t = (f - 13) / 2.f;
        toe = mix(add(mouth, float2(0.6f, -1.2f)), p.fn[3], smooth(t));
        pawAng = lerpf(1.2f, angleOf(sub(poseSit(lk).fn[3], poseSit(lk).fn[2])), t);
        p.pitch = lerpf(-0.3f, 0.05f, t);
        if (f == 15) p.frontOverHead = false;
    }
    frontIK(p.fn, p.fn[0], toe, pawAng, B);
    return p;
}

// Strecken: vorne flach runter, Po hoch, dann zurück
Pose stretchPose(const Look& lk, int frame) {
    Pose st = poseStand(lk);
    const Bones B = bonesOf(st);
    int f = frame % 14;
    float t = f < 4 ? smooth(f / 3.f) : (f < 10 ? 1.f : smooth((13 - f) / 3.f));
    Pose p = st;
    // Schultern runter und nach vorne, Hüfte bleibt oben
    float2 shD(3.5f * t, -8.5f * t), hipD(0.8f * t, 0.6f * t);
    for (int i = 0; i < 4; ++i) p.spine[i] = add(st.spine[i], mix(hipD, shD, i / 3.f));
    p.spine[1].y -= 1.2f * t;
    p.spine[2].y -= 1.6f * t;
    p.belly0 = add(st.belly0, mix(hipD, shD, 0.3f));
    p.belly1 = add(st.belly1, mix(hipD, shD, 0.7f));
    p.neck0 = add(st.neck0, shD);
    p.head = add(st.head, float2(4.5f * t, -9.5f * t));
    p.pitch = 0.25f * t;
    p.yaw = 0.45f + 0.3f * t;
    p.eyeOpen = 1.f - 0.85f * t;
    p.mouth = (f >= 5 && f <= 8) ? 0.9f : 0.f;   // dabei gähnen
    p.earBack = 0.35f * t;
    setNeck1(lk, p, 0.4f, 0.48f);
    float2 sJ = add(st.fn[0], shD), hJ = add(st.hn[0], hipD);
    const float aF0 = angleOf(sub(st.fn[3], st.fn[2]));
    float2 toeF = add(st.fn[3], float2(9.f * t, 0.f));
    frontIK(p.fn, sJ, toeF, lerpf(aF0, 0.f, t), B);
    frontIK(p.ff, add(sJ, float2(-1.f, 0.1f)), add(toeF, float2(-1.6f, 0.f)), lerpf(aF0, 0.f, t), B);
    hindIK(p.hn, hJ, st.hn[4], angleOf(sub(st.hn[4], st.hn[3])), angleOf(sub(st.hn[2], st.hn[3])), B);
    hindIK(p.hf, add(st.hf[0], hipD), st.hf[4], angleOf(sub(st.hf[4], st.hf[3])), angleOf(sub(st.hf[2], st.hf[3])), B);
    // Schwanz hoch
    swayTail(lk, p, 0.f, 0.f);
    return p;
}

// Ducken vor dem Sprung (Beine angewinkelt, Blick zum Ziel)
Pose crouchPose(const Look& lk, float depth) {
    Pose st = poseStand(lk);
    const Bones B = bonesOf(st);
    Pose p = st;
    float d = depth;
    float2 hipD(1.0f * d, -5.0f * d), shD(-0.5f * d, -4.0f * d);
    for (int i = 0; i < 4; ++i) p.spine[i] = add(st.spine[i], mix(hipD, shD, i / 3.f));
    p.belly0 = add(st.belly0, mix(hipD, shD, 0.3f));
    p.belly1 = add(st.belly1, mix(hipD, shD, 0.7f));
    p.neck0 = add(st.neck0, shD);
    p.head = add(st.head, float2(-0.5f * d, -3.0f * d));
    p.pitch = 0.15f * d;
    p.yaw = 0.3f;
    p.earBack = 0.1f;
    setNeck1(lk, p, 0.42f, 0.5f);
    float2 sJ = add(st.fn[0], shD), hJ = add(st.hn[0], hipD);
    const float aF0 = angleOf(sub(st.fn[3], st.fn[2]));
    const float aH0 = angleOf(sub(st.hn[4], st.hn[3])), mH0 = angleOf(sub(st.hn[2], st.hn[3]));
    frontIK(p.fn, sJ, st.fn[3], aF0, B);
    frontIK(p.ff, add(st.ff[0], shD), st.ff[3], aF0, B);
    hindIK(p.hn, hJ, st.hn[4], aH0, mH0 + 0.5f * d, B);
    hindIK(p.hf, add(st.hf[0], hipD), st.hf[4], aH0, mH0 + 0.5f * d, B);
    return p;
}

// Sprung: t 0 = Absprung (gestreckt), 0.5 = Flug (angezogen), 1 = Landeanflug (Vorderpfoten greifen nach unten)
Pose leapPose(const Look& lk, float t, float flightPitch) {
    Pose st = poseStand(lk);
    const Bones B = bonesOf(st);
    Pose p = st;
    float2 c = mix(st.spine[0], st.spine[3], 0.5f);
    float stretch = t < 0.5f ? lerpf(1.08f, 0.94f, t * 2.f) : lerpf(0.94f, 1.02f, (t - 0.5f) * 2.f);
    float pitch = flightPitch * (1.f - t) + (-0.25f) * t;   // erst Richtung Ziel, zum Landen Nase runter
    auto xf = [&](float2 v) {
        float2 d = sub(v, c);
        d.x *= stretch;
        return add(c, rot(d, pitch));
    };
    for (int i = 0; i < 4; ++i) p.spine[i] = xf(st.spine[i]);
    p.belly0 = xf(st.belly0);
    p.belly1 = xf(st.belly1);
    p.neck0 = xf(st.neck0);
    p.head = add(xf(st.head), float2(0.5f, -1.f));
    p.yaw = 0.25f;
    p.pitch = -0.1f - pitch * 0.4f;
    p.earBack = 0.3f;
    setNeck1(lk, p, 0.42f, 0.45f);
    float2 sJ = xf(st.fn[0]), hJ = xf(st.hn[0]);
    const float aF0 = angleOf(sub(st.fn[3], st.fn[2]));
    const float aH0 = angleOf(sub(st.hn[4], st.hn[3])), mH0 = angleOf(sub(st.hn[2], st.hn[3]));
    // Vorderbeine: vorne gestreckt -> angezogen -> nach unten greifend
    float2 fRel = t < 0.5f ? mix(float2(9.f, 2.f), float2(4.f, -6.f), t * 2.f) : mix(float2(4.f, -6.f), float2(3.f, -14.f), (t - 0.5f) * 2.f);
    float2 hRel = t < 0.5f ? mix(float2(-11.f, -9.f), float2(3.f, -9.f), t * 2.f) : mix(float2(3.f, -9.f), float2(1.f, -11.f), (t - 0.5f) * 2.f);
    float fa = t < 0.5f ? lerpf(-0.2f, -1.4f, t * 2.f) : lerpf(-1.4f, -1.1f, (t - 0.5f) * 2.f);
    frontIK(p.fn, sJ, add(sJ, rot(fRel, pitch)), aF0 + fa + pitch, B);
    frontIK(p.ff, add(sJ, float2(-0.8f, 0.2f)), add(add(sJ, rot(fRel, pitch)), float2(1.5f, 0.6f)), aF0 + fa + pitch, B);
    hindIK(p.hn, hJ, add(hJ, rot(hRel, pitch)), aH0 - 0.8f + pitch, mH0 + 0.8f + pitch, B);
    hindIK(p.hf, add(hJ, float2(0.8f, 0.2f)), add(add(hJ, rot(hRel, pitch)), float2(1.4f, 0.5f)), aH0 - 0.8f + pitch, mH0 + 0.8f + pitch, B);
    // Schwanz als Steuer: gerade nach hinten, leicht nach oben
    float2 tb = xf(st.tail[0]);
    float2 back = rot(float2(-1.f, 0.25f), pitch);
    const float2 tc[4] = {tb, add(tb, mul(back, 7.f)), add(add(tb, mul(back, 14.f)), float2(0, 1.2f)), add(add(tb, mul(back, 20.f)), float2(0, 3.f))};
    buildTailSpline(p, tc, 4, 10);
    // Nullpunkt: Körpermitte unten (die Flugbahn bezieht sich darauf)
    movePose(p, float2(0, -st.fn[3].y));
    return p;
}

Pose fallPose(const Look& lk, int frame) {
    Pose p = leapPose(lk, 1.f, 0.f);
    // Beine weit auseinander, Schwanz hoch, Augen groß
    const Bones B = bonesOf(poseStand(lk));
    float w = std::sin(2.f * kPi * (frame % 4) / 4.f) * 0.15f;
    frontIK(p.fn, p.fn[0], add(p.fn[0], float2(5.f, -11.f + w * 5.f)), -1.3f, B);
    frontIK(p.ff, p.ff[0], add(p.ff[0], float2(7.f, -9.5f)), -1.2f, B);
    hindIK(p.hn, p.hn[0], add(p.hn[0], float2(-5.f, -11.f - w * 5.f)), -1.6f, 0.2f, B);
    hindIK(p.hf, p.hf[0], add(p.hf[0], float2(-2.5f, -12.f)), -1.6f, 0.4f, B);
    float2 tb = p.tail[0];
    const float2 tc[4] = {tb, add(tb, float2(-5.f, 4.f)), add(tb, float2(-8.f, 10.f + w * 8.f)), add(tb, float2(-7.f, 16.f))};
    buildTailSpline(p, tc, 4, 10);
    p.earBack = 0.6f;
    p.pupil = 0.9f;
    return p;
}

// Landen: Vorderpfoten fangen ab, Körper federt ein, dann Stand
Pose landPose(const Look& lk, int frame) {
    const float depth[5] = {0.55f, 0.85f, 0.6f, 0.3f, 0.1f};
    Pose p = crouchPose(lk, depth[std::min(frame, 4)]);
    p.pitch -= 0.15f * depth[std::min(frame, 4)];
    return p;
}

// Getragen: hängt am Nackenfell (Haltepunkt), Beine baumeln
Pose carriedPose(const Look& lk, float swing) {
    Pose st = poseStand(lk);
    const Bones B = bonesOf(st);
    Pose p = st;
    const float L = len(sub(st.spine[3], st.spine[0]));
    float2 sh(0.f, 0.f), hip(-1.6f, -L * 0.93f);
    p.spine[3] = sh;
    p.spine[0] = hip;
    p.spine[1] = add(mix(hip, sh, 0.36f), float2(-0.8f, 0.f));
    p.spine[2] = add(mix(hip, sh, 0.7f), float2(-0.6f, 0.f));
    p.belly0 = add(mix(hip, sh, 0.3f), float2(2.f, 0.f));
    p.belly1 = add(mix(hip, sh, 0.7f), float2(2.4f, 0.f));
    p.head = float2(2.6f, lk.headR * 0.95f);
    p.neck0 = add(sh, float2(0.6f, 1.2f));
    setNeck1(lk, p, 0.3f, 0.55f);
    p.yaw = 1.05f;
    p.pitch = -0.1f;
    p.earBack = 0.55f;
    p.eyeOpen = 0.8f;
    p.squint = 0.f;
    // Vorderbeine hängen unter der Brust, Hinterbeine unter der Hüfte
    float2 sJ = add(sh, float2(1.6f, -2.2f)), hJ = add(hip, float2(1.2f, 2.4f));
    frontIK(p.fn, sJ, add(sJ, float2(1.4f, -(B.upperArm + B.foreArm + B.fpaw) * 0.93f)), -1.45f, B);
    frontIK(p.ff, add(sJ, float2(-1.f, 0.4f)), add(sJ, float2(-0.2f, -(B.upperArm + B.foreArm + B.fpaw) * 0.9f)), -1.5f, B);
    hindIK(p.hn, hJ, add(hJ, float2(1.8f, -(B.thigh + B.shin + B.meta) * 0.82f)), -1.35f, 1.75f, B);
    hindIK(p.hf, add(hJ, float2(-1.f, 0.3f)), add(hJ, float2(0.2f, -(B.thigh + B.shin + B.meta) * 0.8f)), -1.4f, 1.8f, B);
    float2 tb = add(hip, float2(-2.5f, -2.f));
    const float2 tc[4] = {tb, add(tb, float2(-0.6f, -7.f)), add(tb, float2(0.6f, -14.f)), add(tb, float2(2.6f, -18.f))};
    buildTailSpline(p, tc, 4, 10);
    p.tailFrontFrom = 99;
    p.anchor = add(sh, float2(0.f, lk.chestR * 0.9f));   // Nackenfell
    rotatePose(p, p.anchor, swing * 0.22f);
    return p;
}

Pose eatPose(const Look& lk, int frame) {
    Pose p = crouchPose(lk, 0.35f);
    const float R = lk.headR;
    float chew = (frame % 4 < 2) ? 0.f : 0.35f;
    p.head = float2(p.fn[3].x + R * 0.2f, R * 0.95f + 1.2f + chew);
    p.yaw = 0.35f;
    p.pitch = -0.55f;
    p.eyeOpen = 0.6f;
    p.neck0 = add(p.spine[3], float2(0.4f, 0.4f));
    setNeck1(lk, p, 0.5f, 0.15f);
    return p;
}

// Po wackeln: tief geduckt, Hüfte pendelt
Pose wigglePose(const Look& lk, int frame) {
    Pose p = crouchPose(lk, 1.f);
    float s = std::sin(2.f * kPi * (frame % 4) / 4.f);
    p.spine[0] = add(p.spine[0], float2(0.f, 0.5f * s));
    p.spine[1] = add(p.spine[1], float2(0.f, 0.3f * s));
    p.hn[0] = add(p.hn[0], float2(0.f, 0.5f * s));
    p.hf[0] = add(p.hf[0], float2(0.f, 0.5f * s));
    p.head = add(p.head, float2(1.f, -1.f));
    setNeck1(lk, p, 0.42f, 0.5f);
    p.pupil = 1.f;
    p.earBack = 0.f;
    swayTail(lk, p, 0.25f, frame / 4.f);
    return p;
}

// Mit der Pfote schlagen (aus dem Sitzen)
Pose batPose(const Look& lk, int frame) {
    Pose p = poseSit(lk);
    const Bones B = bonesOf(poseStand(lk));
    const float2 toe0 = p.fn[3];
    const float a0 = angleOf(sub(p.fn[3], p.fn[2]));
    int f = frame % 6;
    const float2 keys[6] = {toe0, add(p.fn[0], float2(5.f, 4.f)), add(p.fn[0], float2(9.f, 1.f)), add(p.fn[0], float2(10.f, -6.f)),
                            add(p.fn[0], float2(7.f, -10.f)), toe0};
    const float angs[6] = {a0, 0.9f, 0.2f, -0.6f, -0.9f, a0};
    frontIK(p.fn, p.fn[0], keys[f], angs[f], B);
    p.yaw = 0.5f;
    p.pitch = -0.15f;
    p.pupil = 1.f;
    return p;
}

Pose pettedPose(const Look& lk, int frame) {
    Pose p = poseSit(lk);
    float s = std::sin(2.f * kPi * (frame % 8) / 8.f);
    p.yaw = 0.95f;
    p.pitch = 0.22f + 0.05f * s;
    p.roll = 0.22f + 0.06f * s;
    p.eyeOpen = 0.f;
    p.squint = 1.f;   // glückliche Augen
    p.earBack = 0.2f;
    p.head = add(p.head, float2(0.3f * s, 0.f));
    setNeck1(lk, p, 0.22f, 0.58f);
    return p;
}

Pose yawnPose(const Look& lk, int frame) {
    Pose p = poseSit(lk);
    const float m[10] = {0.f, 0.3f, 0.7f, 1.f, 1.f, 1.f, 0.8f, 0.4f, 0.1f, 0.f};
    float o = m[std::min(frame, 9)];
    p.mouth = o;
    p.pitch += 0.3f * o;
    p.eyeOpen = 1.f - o;
    p.squint = 0.f;
    p.earBack = 0.4f * o;
    return p;
}

}  // namespace

// ------------------------------------------------------------------ öffentlich

int actFrames(Act a) {
    switch (a) {
        case Act::Walk: return 8;
        case Act::Run: return 6;
        case Act::Groom: return 16;
        case Act::Stretch: return 14;
        case Act::Land: return 5;
        case Act::Eat: return 4;
        case Act::Wiggle: return 4;
        case Act::Bat: return 6;
        case Act::Petted: return 8;
        case Act::Yawn: return 10;
        case Act::Fall: return 4;
        case Act::LoafDangle: return 24;
        default: return 1;
    }
}

bool actLoops(Act a) {
    switch (a) {
        case Act::Stretch:
        case Act::Land:
        case Act::Yawn: return false;
        default: return true;
    }
}

int actStepPx(Act a) {
    switch (a) {
        case Act::Walk: return 4;
        case Act::Run: return 8;
        default: return 0;
    }
}

Pose actPose(const Look& lk, Act act, int frame, float param, const Overlay& ov) {
    Pose p;
    bool tailGround = false;
    switch (act) {
        case Act::Stand: p = poseStand(lk); break;
        case Act::Walk: p = walkPose(lk, frame); break;
        case Act::Run: p = runPose(lk, frame); break;
        case Act::Sit: p = sitPose(lk); tailGround = true; break;
        case Act::Loaf: p = loafPose(lk, 0.f, frame); tailGround = true; break;
        case Act::LoafDangle: p = loafPose(lk, param <= 0.f ? 1.f : std::min(1.f, param), frame); tailGround = true; break;
        case Act::Sleep: p = poseSleep(lk, -std::cos(2.f * kPi * ov.breath)); tailGround = true; break;   // 0 aus, 0.5 ein
        case Act::Groom: p = groomPose(lk, frame); tailGround = true; break;
        case Act::Stretch: p = stretchPose(lk, frame); break;
        case Act::Crouch: p = crouchPose(lk, clamp01(param <= 0.f ? 1.f : param)); break;
        case Act::Leap: p = leapPose(lk, clamp01(param), ov.lookPitch); break;
        case Act::Pounce: p = leapPose(lk, clamp01(param), 0.15f); break;
        case Act::Fall: p = fallPose(lk, frame); break;
        case Act::Land: p = landPose(lk, frame); break;
        case Act::Carried: p = carriedPose(lk, param); break;
        case Act::Eat: p = eatPose(lk, frame); break;
        case Act::Wiggle: p = wigglePose(lk, frame); break;
        case Act::Bat: p = batPose(lk, frame); tailGround = true; break;
        case Act::Petted: p = pettedPose(lk, frame); tailGround = true; break;
        case Act::Yawn: p = yawnPose(lk, frame); tailGround = true; break;
        default: p = poseStand(lk); break;
    }
    Overlay o = ov;
    if (act == Act::Sleep) o.breath = 0.f;   // schon in der Haltung
    if (act == Act::Leap || act == Act::Pounce) o.lookPitch = 0.f;
    if (act == Act::Run || act == Act::Leap || act == Act::Pounce || act == Act::Fall || act == Act::Carried) o.tailAmp = 0.f;
    applyOverlay(lk, p, o, tailGround);
    return p;
}

// ------------------------------------------------------------------ Mischen und Übergänge

Pose blendPose(const Look& lk, const Pose& a, const Pose& b, float t) {
    Pose p = t < 0.5f ? a : b;
    auto m2 = [&](float2 x, float2 y) { return mix(x, y, t); };
    for (int i = 0; i < 4; ++i) {
        p.spine[i] = m2(a.spine[i], b.spine[i]);
        p.spineR[i] = lerpf(a.spineR[i], b.spineR[i], t);
    }
    p.belly0 = m2(a.belly0, b.belly0);
    p.belly1 = m2(a.belly1, b.belly1);
    p.bellyR = lerpf(a.bellyR, b.bellyR, t);
    p.neck0 = m2(a.neck0, b.neck0);
    p.neck1 = m2(a.neck1, b.neck1);
    p.neckR0 = lerpf(a.neckR0, b.neckR0, t);
    p.neckR1 = lerpf(a.neckR1, b.neckR1, t);
    p.head = m2(a.head, b.head);
    p.yaw = lerpf(a.yaw, b.yaw, t);
    p.pitch = lerpf(a.pitch, b.pitch, t);
    p.roll = lerpf(a.roll, b.roll, t);
    p.eyeOpen = lerpf(a.eyeOpen, b.eyeOpen, t);
    p.squint = lerpf(a.squint, b.squint, t);
    p.mouth = lerpf(a.mouth, b.mouth, t);
    p.earBack = lerpf(a.earBack, b.earBack, t);
    p.pupil = lerpf(a.pupil, b.pupil, t);
    p.thighEnd = lerpf(a.thighEnd, b.thighEnd, t);
    p.shadowC = m2(a.shadowC, b.shadowC);
    p.anchor = m2(a.anchor, b.anchor);
    // Beine: Ansatz, Zehen und Winkel mischen, Knie/Ellbogen mit gleichen Knochen nachführen (an den Enden exakt)
    const Bones B = bonesOf(poseStand(lk));
    float w = std::sin(kPi * t);
    auto front = [&](const float2* ja, const float2* jb, float2* out) {
        float2 lin[4];
        for (int i = 0; i < 4; ++i) lin[i] = mix(ja[i], jb[i], t);
        float2 ik[4];
        frontIK(ik, lin[0], lin[3], lerpAngle(angleOf(sub(ja[3], ja[2])), angleOf(sub(jb[3], jb[2])), t), B);
        for (int i = 0; i < 4; ++i) out[i] = mix(lin[i], ik[i], w);
    };
    auto hind = [&](const float2* ja, const float2* jb, float2* out) {
        float2 lin[5];
        for (int i = 0; i < 5; ++i) lin[i] = mix(ja[i], jb[i], t);
        float2 ik[5];
        hindIK(ik, lin[0], lin[4], lerpAngle(angleOf(sub(ja[4], ja[3])), angleOf(sub(jb[4], jb[3])), t),
               lerpAngle(angleOf(sub(ja[2], ja[3])), angleOf(sub(jb[2], jb[3])), t), B);
        for (int i = 0; i < 5; ++i) out[i] = mix(lin[i], ik[i], w);
    };
    front(a.fn, b.fn, p.fn);
    front(a.ff, b.ff, p.ff);
    hind(a.hn, b.hn, p.hn);
    hind(a.hf, b.hf, p.hf);
    // Schwanz: Punkte mischen (gleiche Anzahl erzwingen)
    int n = std::max(a.tailN, b.tailN);
    if (a.tailN == b.tailN) {
        for (int i = 0; i < n; ++i) p.tail[i] = m2(a.tail[i], b.tail[i]);
        p.tailN = n;
    }
    p.tailFrontFrom = t < 0.5f ? a.tailFrontFrom : b.tailFrontFrom;
    return p;
}

int transitionFrames(Posture a, Posture b) {
    if (a == b) return 0;
    if ((a == Posture::Loaf && b == Posture::Sleep) || (a == Posture::Sleep && b == Posture::Loaf)) return 6;
    if ((a == Posture::Stand && b == Posture::Loaf) || (a == Posture::Loaf && b == Posture::Stand)) return 6;
    return 4;
}

namespace {
Pose postureBase(const Look& lk, Posture p) {
    switch (p) {
        case Posture::Sit: return poseSit(lk);
        case Posture::Loaf: return poseLoaf(lk, 0.f);
        case Posture::Sleep: return poseSleep(lk, 0.f);
        default: return poseStand(lk);
    }
}
}  // namespace

Pose transitionPose(const Look& lk, Posture a, Posture b, int frame, const Overlay& ov) {
    int n = transitionFrames(a, b);
    float t = n > 0 ? smooth((frame + 1.f) / (n + 1.f)) : 1.f;
    Pose pa = postureBase(lk, a), pb = postureBase(lk, b);
    Pose p = blendPose(lk, pa, pb, t);
    // Schwanzformen passen selten zusammen: in der ersten Hälfte alter, danach neuer Schwanz
    const Pose& tailSrc = t < 0.5f ? pa : pb;
    float2 off = sub(mix(pa.tail[0], pb.tail[0], t), tailSrc.tail[0]);
    p.tailN = tailSrc.tailN;
    for (int i = 0; i < p.tailN; ++i) p.tail[i] = add(tailSrc.tail[i], off);
    p.tailFrontFrom = tailSrc.tailFrontFrom;
    Overlay o = ov;
    o.tailAmp = 0.f;
    applyOverlay(lk, p, o, true);
    return p;
}

uint64_t poseKey(const Pose& p) {
    // Werte auf 0,05 cm runden und mischen (FNV-1a)
    uint64_t h = 1469598103934665603ull;
    auto feed = [&](float v) {
        int32_t q = (int32_t)std::lround(v * 20.f);
        for (int i = 0; i < 4; ++i) {
            h ^= (uint8_t)(q >> (i * 8));
            h *= 1099511628211ull;
        }
    };
    auto feed2 = [&](float2 v) {
        feed(v.x);
        feed(v.y);
    };
    for (int i = 0; i < 4; ++i) {
        feed2(p.spine[i]);
        feed(p.spineR[i]);
    }
    feed2(p.belly0);
    feed2(p.belly1);
    feed(p.bellyR);
    feed2(p.neck0);
    feed2(p.neck1);
    feed(p.neckR0);
    feed(p.neckR1);
    feed2(p.head);
    feed(p.yaw);
    feed(p.pitch);
    feed(p.roll);
    feed(p.eyeOpen);
    feed(p.pupil);
    feed(p.squint);
    feed(p.mouth);
    feed(p.earBack);
    feed(p.thighEnd);
    for (int i = 0; i < 4; ++i) {
        feed2(p.fn[i]);
        feed2(p.ff[i]);
    }
    for (int i = 0; i < 5; ++i) {
        feed2(p.hn[i]);
        feed2(p.hf[i]);
    }
    feed((float)p.tailN);
    for (int i = 0; i < p.tailN; ++i) feed2(p.tail[i]);
    feed((float)p.tailFrontFrom);
    feed(p.frontOverHead ? 1.f : 0.f);
    return h;
}

}  // namespace cat
