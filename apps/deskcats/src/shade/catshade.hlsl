// DeskCats – Katzen-Zeichnung (gemeinsamer Code für Grafikkarte und CPU).
// Jedes Pixel des kleinen Katzenfensters wird aus "weichen" Formen berechnet (vorzeichenbehaftete Abstandsfelder):
// Rumpf, Beine und Schwanz sind Röhren, der Kopf besteht aus Kugeln (Schädel, Schnauze, Backen), auf die Augen,
// Nase und Fellmuster "aufgemalt" werden. Dadurch kann sich der Kopf frei drehen und alles bleibt bei jeder Größe scharf.
// Nur HLSL-Sprachumfang, der auch als C++ übersetzt (keine Swizzles außer .x/.y/.z/.w, keine out-Parameter).
//
// Konstanten (vom Programm je Bild gesetzt):
//   gView   x,y = Katzen-Koordinate von Pixel (0,0), z = Einheiten pro Pixel
//   gInfo   x = Anzahl Röhren, y = Zeit (s), z = Randlicht-Stärke, w = Deckkraft
//   gShadow Kontaktschatten (Mitte x,y, Radien x,y)       gShadow2.x = Stärke
//   gPat    x = Muster (0 getigert, 1 Smoking), y = Streifendichte, z = Streifenkontrast, w = Fellstruktur
//   gPal[16] Farben (linear)      gSeg[] je Röhre 3 Einträge      gHead[16] Kopf      gEye[6] Augen      gWhisk[12] Schnurrhaare

#define CAT_MAX_SEG 48
#define PART_TORSO 0
#define PART_NECK 1
#define PART_FUPPER 2
#define PART_FLOWER 3
#define PART_FPAW 4
#define PART_THIGH 5
#define PART_SHIN 6
#define PART_META 7
#define PART_HPAW 8
#define PART_TAIL 9

// ------------------------------------------------------------------ Hilfen

float cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }

float hash12(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, float3(p3.y + 33.33, p3.z + 33.33, p3.x + 33.33));
    return frac((p3.x + p3.y) * p3.z);
}

float vnoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + float2(1.0, 0.0));
    float c = hash12(i + float2(0.0, 1.0));
    float d = hash12(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float sdSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6));
    return length(pa - ba * h);
}

// Dreieck (Inigo Quilez), negativ innen
float sdTriangle(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 e0 = p1 - p0;
    float2 e1 = p2 - p1;
    float2 e2 = p0 - p2;
    float2 v0 = p - p0;
    float2 v1 = p - p1;
    float2 v2 = p - p2;
    float2 pq0 = v0 - e0 * saturate(dot(v0, e0) / dot(e0, e0));
    float2 pq1 = v1 - e1 * saturate(dot(v1, e1) / dot(e1, e1));
    float2 pq2 = v2 - e2 * saturate(dot(v2, e2) / dot(e2, e2));
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    float2 d = min(min(float2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                       float2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                   float2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

float3 toSrgb(float3 c) { return pow(saturate(c), 1.0 / 2.2); }

// ------------------------------------------------------------------ Licht

// Weiches Fell: umhüllendes Hauptlicht von links oben, kühles Fülllicht, warmes Licht vom Boden, Randlicht oben.
float3 shadeFur(float3 albedo, float3 n, float occl) {
    float3 L = normalize(float3(-0.38, 0.78, 0.5));
    float wrap = saturate((dot(n, L) + 0.45) / 1.45);
    float3 fillL = normalize(float3(0.75, 0.05, 0.65));
    float fill = saturate((dot(n, fillL) + 0.3) / 1.3);
    float3 key = float3(gPal[12].x, gPal[12].y, gPal[12].z);
    float3 amb = float3(gPal[13].x, gPal[13].y, gPal[13].z);
    float bounce = saturate(-n.y) * 0.5;
    float3 light = amb + key * (wrap * wrap * 0.95) + float3(0.55, 0.62, 0.75) * (fill * 0.22) + float3(0.35, 0.25, 0.15) * bounce * 0.4;
    return albedo * light * occl;
}

float rimMask(float3 n) {
    float edge = pow(saturate(1.0 - n.z), 3.0);
    return edge * saturate(n.y * 0.8 + 0.45);
}

// ------------------------------------------------------------------ Fell

float3 palette(int i) { return float3(gPal[i].x, gPal[i].y, gPal[i].z); }

// Fellflecken als Ellipsen in Katzen-Koordinaten (vom Programm je Haltung gesetzt)
float spotMask(float2 p, float wob) {
    float m = 0.0;
    for (int i = 0; i < 3; i++) {
        float4 a = gPatE[i * 2];
        float4 b = gPatE[i * 2 + 1];
        if (b.z <= 0.0) continue;
        float2 q = p - float2(a.x, a.y);
        float qx = q.x * b.x + q.y * b.y;
        float qy = -q.x * b.y + q.y * b.x;
        float r = length(float2(qx / a.z, qy / a.w));
        m = max(m, (1.0 - smoothstep(0.82, 1.0, r + wob * 0.18)) * b.z);
    }
    return m;
}

// Fellfarbe eines Rumpf-/Bein-/Schwanzteils. u = Längskoordinate (Rumpf: 0 Hüfte .. 1 Schulter, Beine: 0 oben .. 1 Pfote,
// Schwanz: 0 Ansatz .. 1 Spitze), v = quer (+1 Rücken/Außenseite, -1 Bauch/Innenseite).
float3 furColor(int part, float u, float v, float2 p) {
    float3 base = palette(0);
    float3 dark = palette(1);
    float3 light = palette(2);
    float3 deep = palette(3);
    float freq = gPat.y;
    float contrast = gPat.z;
    float wob = vnoise(p * 0.45) - 0.5;
    float3 col = base;
    if (gPat.x < 0.5) {
        // ---- rot getigert
        if (part <= PART_NECK) {
            float belly = smoothstep(-0.05 + wob * 0.2, -0.75, v);
            col = lerp(base, light, belly * 0.9);
            col = lerp(col, dark, smoothstep(0.35, 1.0, v) * 0.22);
            float s = sin(6.2832 * (u * freq + 0.16 * sin(v * 2.3 + u * 6.0) + wob * 0.08));
            float stripe = smoothstep(0.25, 0.85, s) * (1.0 - belly) * contrast * smoothstep(-0.55, 0.15, v);
            col = lerp(col, dark, stripe);
            if (part == PART_NECK) col = lerp(col, light, smoothstep(0.1, -0.6, v) * 0.85);
        } else if (part == PART_TAIL) {
            float s = sin(6.2832 * (u * 5.5 + 0.1));
            float stripe = smoothstep(0.0, 0.7, s) * contrast * (0.7 + 0.5 * u);
            col = lerp(base, dark, stripe);
            col = lerp(col, deep, smoothstep(0.82, 0.98, u) * 0.85);
            col = lerp(col, light, smoothstep(-0.4, -0.95, v) * 0.35);
        } else if (part == PART_FPAW || part == PART_HPAW) {
            col = lerp(base, light, 0.75);
        } else {
            // Beine: Ringe oben, innen heller, unten Richtung Pfote cremig
            float s = sin(6.2832 * (u * 2.6 + 0.2));
            float stripe = smoothstep(0.2, 0.8, s) * contrast * (1.0 - smoothstep(0.55, 0.95, u));
            col = lerp(base, dark, stripe);
            col = lerp(col, light, smoothstep(0.55, 1.0, u) * 0.5 + smoothstep(-0.2, -0.9, v) * 0.35);
        }
    } else {
        // ---- Smoking: schwarz, weißer Latz von Kinn bis zwischen die Vorderbeine, schmaler Bauchstreifen, weiße Pfoten
        float white = 0.0;
        if (part == PART_TORSO || part == PART_NECK || part == PART_FUPPER || part == PART_THIGH) {
            white = spotMask(p, wob);
        } else if (part == PART_FPAW || part == PART_HPAW) {
            white = 1.0;
        } else if (part == PART_FLOWER) {
            white = smoothstep(0.66 + wob * 0.12, 0.8, u);
        } else if (part == PART_META) {
            white = smoothstep(0.62 + wob * 0.12, 0.78, u);
        }
        col = lerp(base, light, white);
    }
    // feine Fellstruktur in Längsrichtung
    float grain = vnoise(float2(u * 34.0, v * 3.0 + u * 2.0)) - 0.5;
    col *= 1.0 + grain * gPat.w;
    return col;
}

// ------------------------------------------------------------------ Röhren (Rumpf, Beine, Schwanz)

struct Acc {
    float d;
    float3 n;
    float d1;
    int i1;
    float d2;
    int i2;
    float dLeg;
    float dCore;
};

Acc accInit() {
    Acc a;
    a.d = 1e5;
    a.n = float3(0.0, 0.0, 1.0);
    a.d1 = 1e5;
    a.i1 = -1;
    a.d2 = 1e5;
    a.i2 = -1;
    a.dLeg = 1e5;
    a.dCore = 1e5;
    return a;
}

Acc accAdd(Acc a, float d, float3 n, int idx, float k, float isLeg, float isCore) {
    float h = saturate(0.5 + 0.5 * (a.d - d) / k);
    a.d = lerp(a.d, d, h) - k * h * (1.0 - h);
    a.n = lerp(a.n, n, h);
    if (d < a.d1) {
        a.d2 = a.d1;
        a.i2 = a.i1;
        a.d1 = d;
        a.i1 = idx;
    } else if (d < a.d2) {
        a.d2 = d;
        a.i2 = idx;
    }
    if (isLeg > 0.5) a.dLeg = min(a.dLeg, d);
    if (isCore > 0.5) a.dCore = min(a.dCore, d);
    return a;
}

float3 segAlbedo(int i, float2 p) {
    float4 s0 = gSeg[i * 3];
    float4 s1 = gSeg[i * 3 + 1];
    float4 s2 = gSeg[i * 3 + 2];
    float2 a = float2(s0.x, s0.y);
    float2 b = float2(s0.z, s0.w);
    float2 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
    float2 c = a + ab * t;
    float r = max(lerp(s1.x, s1.y, t), 1e-3);
    float2 dir = normalize(ab);
    float v = clamp(cross2(dir, p - c) / r, -1.0, 1.0) * s2.z;
    float u = lerp(s2.x, s2.y, t);
    return furColor((int)(s1.w + 0.5), u, v, p);
}

float3 accAlbedo(Acc a, float2 p, float k) {
    float3 c1 = segAlbedo(a.i1, p);
    if (a.i2 < 0) return c1;
    float3 c2 = segAlbedo(a.i2, p);
    float w = saturate(0.5 + 0.5 * (a.d2 - a.d1) / (k * 1.8));
    return lerp(c2, c1, w);
}

// Ebene zeichnen: Deckkraft mit Kantenglättung, Licht, dunklerer Rand, Randlicht
float4 layerColor(Acc a, float2 p, float upp, float k, float occl, float creaseAmt) {
    float pix = gShadow2.y;
    if (pix < 0.5) a.d += (vnoise(p * 1.7) - 0.5) * gPat.w * 2.2 + (vnoise(p * 4.3) - 0.5) * gPat.w * 1.1;
    float alpha = pix > 0.5 ? (a.d < 0.0 ? 1.0 : 0.0) : saturate(0.5 - a.d / (upp * 1.3));
    if (alpha <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float3 n = normalize(a.n + float3(0.0, 0.0, 0.3));
    float3 albedo = accAlbedo(a, p, k);
    float3 col = shadeFur(albedo, n, occl);
    // Falte, wo ein Bein vor dem Körper liegt
    float line = 1.0 - smoothstep(upp * 0.4, upp * 2.0, abs(a.dLeg + upp * 0.6));
    float nearSurface = smoothstep(-k * 2.2, -k * 0.2, a.dCore) * saturate(-a.d / (k * 0.6));
    float crease = line * nearSurface * creaseAmt;
    col *= 1.0 - crease * 0.3;
    // dunklerer Saum an der Silhouette (gemalte Kontur), Randlicht oben drauf
    float edge = smoothstep(-upp * 2.0, 0.0, a.d) * (1.0 - pix);
    col *= 1.0 - edge * 0.38;
    col += palette(11) * (rimMask(n) * gInfo.z * (1.0 - edge * 0.5));
    return float4(col * alpha, alpha);
}

// ------------------------------------------------------------------ Kopf

float3 headF() { return float3(gHead[1].x, gHead[1].y, gHead[1].z); }
float3 headU() { return float3(gHead[2].x, gHead[2].y, gHead[2].z); }
float3 headS() { return float3(gHead[3].x, gHead[3].y, gHead[3].z); }

// Kugel an Pixel p: Tiefe der Oberfläche (oder -1e5 außerhalb)
float sphereZ(float2 p, float4 s) {
    float2 q = p - float2(s.x, s.y);
    float rr = s.w * s.w - dot(q, q);
    return rr > 0.0 ? s.z + sqrt(rr) : -1e5;
}

float earDist(float2 p, int base) {
    float4 a = gHead[base];
    float4 b = gHead[base + 1];
    return sdTriangle(p, float2(a.x, a.y), float2(a.z, a.w), float2(b.x, b.y)) - b.w;
}

// innerer Teil des Ohrs (zur Basis hin verkleinert)
float earInnerDist(float2 p, int base) {
    float4 a = gHead[base];
    float4 b = gHead[base + 1];
    float2 p0 = float2(a.x, a.y);
    float2 p1 = float2(a.z, a.w);
    float2 p2 = float2(b.x, b.y);
    float2 baseMid = (p0 + p2) * 0.5;
    float2 q0 = lerp(p0, baseMid, 0.42);
    float2 q2 = lerp(p2, baseMid, 0.42);
    float2 q1 = lerp(p1, baseMid, 0.28);
    return sdTriangle(p, q0, q1, q2);
}

// Gesichtsfarbe an Kopfkoordinate l (vorwärts, oben, Seite; in Kopfradien)
float3 headFur(float3 l, float onMuzzle, float2 p) {
    float3 base = palette(0);
    float3 dark = palette(1);
    float3 light = palette(2);
    float3 col = base;
    float wob = vnoise(p * 0.6) - 0.5;
    if (gPat.x < 0.5) {
        // Kinn, Schnauze und Backenunterseite cremig, Stirn mit feinen "M"-Streifen, Hinterkopf etwas dunkler
        float chin = smoothstep(-0.18 + wob * 0.1, -0.62, l.y) * smoothstep(-0.1, 0.35, l.x);
        float muzzle = onMuzzle * smoothstep(0.15, -0.35, l.y - (-0.3));
        col = lerp(base, light, max(chin, muzzle) * 0.9);
        float fore = smoothstep(0.18, 0.55, l.y) * smoothstep(-0.2, 0.35, l.x) * (1.0 - onMuzzle);
        float s = sin(l.z * 21.0 + wob * 1.5);
        col = lerp(col, dark, smoothstep(0.35, 0.95, s) * fore * gPat.z * 0.9);
        float cheek = (1.0 - smoothstep(0.03, 0.07, abs(l.y + 0.02 + 0.25 * (l.x - 0.25)))) * smoothstep(0.1, 0.4, abs(l.z)) * smoothstep(0.6, 0.2, l.x) * (1.0 - onMuzzle);
        col = lerp(col, dark, cheek * gPat.z * 0.7);
        col = lerp(col, dark, smoothstep(-0.15, -0.75, l.x) * 0.2);
    } else {
        // Smoking-Gesicht: schwarz, Kinn und Unterlippe weiß
        float chin = smoothstep(-0.48 + wob * 0.12, -0.66, l.y) * smoothstep(0.0, 0.4, l.x);
        float lip = onMuzzle * smoothstep(-0.36, -0.46, l.y) * smoothstep(0.55, 0.8, l.x);
        col = lerp(base, light, max(chin, lip));
    }
    float grain = vnoise(float2(l.y * 30.0, l.z * 30.0 + l.x * 10.0)) - 0.5;
    col *= 1.0 + grain * gPat.w * 0.8;
    return col;
}

// Auge i (0 = nah, 1 = fern) an der Kopf-Einheitsrichtung l. Liefert Farbe (rgb) und Deckkraft (w).
float4 eyeAt(float3 l, int i, float uppL) {
    float3 e = float3(gEye[i * 3].x, gEye[i * 3].y, gEye[i * 3].z);
    float3 tH = float3(gEye[i * 3 + 1].x, gEye[i * 3 + 1].y, gEye[i * 3 + 1].z);
    float3 tV = float3(gEye[i * 3 + 2].x, gEye[i * 3 + 2].y, gEye[i * 3 + 2].z);
    if (dot(l, e) < 0.6) return float4(0.0, 0.0, 0.0, 0.0);
    float ax = gHead[11].x;
    float ay = gHead[11].y;
    float3 dd = l - e;
    float x = dot(dd, tH) / ax;
    float y = dot(dd, tV) / ay;
    float open = gHead[1].w;
    float squint = gHead[3].w;
    float aa = uppL / ax * 1.3;
    float3 lineCol = palette(9);
    if (open < 0.16) {
        // geschlossen: Lidlinie (beim Zufrieden-Blinzeln nach oben gebogen)
        float curve = -0.05 + (0.32 * squint - 0.12) * (1.0 - x * x);
        float dl = abs(y - curve) * ay / ax;
        float m = (1.0 - smoothstep(0.07, 0.07 + aa * 1.5, dl)) * (1.0 - smoothstep(0.85, 1.0, abs(x)));
        return float4(lineCol, m * 0.9);
    }
    // Mandelform: oben etwas flacher, Lid schließt von oben
    float top = open * (1.0 - 0.18 * x * x);
    float bottom = -(0.55 + 0.45 * open) * (1.0 - 0.12 * x * x);
    float yn = y > 0.0 ? y / max(top, 0.05) : y / min(bottom, -0.05) * -1.0;
    float r = sqrt(x * x + yn * yn);
    float mask = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
    if (mask <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    // Iris mit Verlauf, senkrechte Pupille, Lichtpunkt, dunkler Rand
    float rr = sqrt(x * x + y * y);
    float3 iris = lerp(palette(6), palette(7), smoothstep(0.15, 1.0, rr));
    float pw = gHead[2].w;
    float pupil = 1.0 - smoothstep(0.92, 1.08, sqrt((x / max(pw, 0.05)) * (x / max(pw, 0.05)) + (y / 0.86) * (y / 0.86)));
    float3 col = lerp(iris, palette(9) * 0.6, pupil);
    float hx = (x - gEye[i * 3].w) / 0.2;
    float hy = (y - gEye[i * 3 + 1].w) / 0.2;
    float hl = 1.0 - smoothstep(0.75, 1.05, sqrt(hx * hx + hy * hy));
    col = lerp(col, float3(1.0, 1.0, 1.0), hl * 0.92);
    float rim = smoothstep(0.72, 1.0, r);
    col = lerp(col, lineCol, rim * 0.85);
    // obere Lidkante als Schatten
    col *= 1.0 - smoothstep(0.3, 1.0, y / max(top, 0.05)) * 0.25;
    return float4(col, mask);
}

// Nase und Mund auf der Schnauzenkugel (m = Einheitsrichtung relativ zur Schnauzenmitte, in Kopfachsen)
float4 noseMouth(float3 m, float uppM) {
    float aa = uppM * 1.4;
    // Nase sitzt vorne oben auf der Schnauze; Koordinaten in ihrer Tangentialebene
    float3 nd = normalize(float3(0.82, 0.5, 0.0));
    float3 tV = normalize(float3(-0.5, 0.82, 0.0));
    if (dot(m, nd) < 0.3) return float4(0.0, 0.0, 0.0, 0.0);
    float nx = m.z;
    float ny = dot(m - nd, tV);
    float ns = gHead[12].y;
    float dn = sdTriangle(float2(nx, ny) / ns, float2(-0.28, 0.08), float2(0.28, 0.08), float2(0.0, -0.22)) * ns - 0.035;
    float nose = 1.0 - smoothstep(-aa, aa, dn);
    float3 noseCol = palette(4) * (1.0 - smoothstep(-0.12, 0.08, ny) * 0.25);
    // Mund: kurze Linie nach unten, dann zwei Bögen
    float mo = gHead[12].x;
    float2 q = float2(nx, ny);
    float d1 = sdSegment(q, float2(0.0, -0.21 * ns), float2(0.0, -0.36 * ns));
    float d2 = sdSegment(q, float2(0.0, -0.36 * ns), float2(0.17 * ns, -0.44 * ns - mo * 0.1));
    float d3 = sdSegment(q, float2(0.0, -0.36 * ns), float2(-0.17 * ns, -0.44 * ns - mo * 0.1));
    float dm = min(d1, min(d2, d3));
    float mouth = 1.0 - smoothstep(0.012, 0.012 + aa, dm);
    float4 o = float4(palette(9), mouth * 0.75 * smoothstep(0.3, 0.6, dot(m, nd)));
    if (nose > 0.0) o = float4(lerp(palette(9), noseCol, nose), max(o.w, nose));
    return o;
}

float4 headLayer(float2 p, float upp) {
    float R = gHead[0].z;
    float2 hc = float2(gHead[0].x, gHead[0].y);
    float4 sHead = float4(hc.x, hc.y, 0.0, R);
    float4 sMuz = gHead[4];
    float4 sCheekN = gHead[5];
    float4 sCheekF = gHead[6];
    float k = R * 0.16;
    // Silhouette: Kugeln weich vereint, Ohren dazu
    float dH = length(p - hc) - R;
    float dM = length(p - float2(sMuz.x, sMuz.y)) - sMuz.w;
    float dC1 = length(p - float2(sCheekN.x, sCheekN.y)) - sCheekN.w;
    float dC2 = length(p - float2(sCheekF.x, sCheekF.y)) - sCheekF.w;
    float d = dH;
    float h = saturate(0.5 + 0.5 * (d - dM) / k);
    d = lerp(d, dM, h) - k * h * (1.0 - h);
    h = saturate(0.5 + 0.5 * (d - dC1) / k);
    d = lerp(d, dC1, h) - k * h * (1.0 - h);
    h = saturate(0.5 + 0.5 * (d - dC2) / k);
    d = lerp(d, dC2, h) - k * h * (1.0 - h);
    float dSkull = d;
    float dEarN = earDist(p, 7);
    float dEarF = earDist(p, 9);
    float ke = R * 0.1;
    h = saturate(0.5 + 0.5 * (d - dEarN) / ke);
    d = lerp(d, dEarN, h) - ke * h * (1.0 - h);
    h = saturate(0.5 + 0.5 * (d - dEarF) / ke);
    d = lerp(d, dEarF, h) - ke * h * (1.0 - h);
    // Fell an der Kontur: weiche Büschel an Backen und Kinn
    float pix = gShadow2.y;
    float tuft = (vnoise(p * (7.0 / R)) - 0.5) * R * 0.3 * gPat.w * (1.0 - pix);
    d += tuft * smoothstep(0.1, -0.5, (p.y - hc.y) / R);
    float alpha = pix > 0.5 ? (d < 0.0 ? 1.0 : 0.0) : saturate(0.5 - d / (upp * 1.15));
    if (alpha <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);

    // vorderste Oberfläche bestimmen (0 Schädel, 1 Schnauze, 2/3 Backen)
    float zH = sphereZ(p, sHead);
    float zM = sphereZ(p, sMuz);
    float zC1 = sphereZ(p, sCheekN);
    float zC2 = sphereZ(p, sCheekF);
    float z = zH;
    float4 s = sHead;
    int sid = 0;
    if (zM > z) { z = zM; s = sMuz; sid = 1; }
    if (zC1 > z) { z = zC1; s = sCheekN; sid = 2; }
    if (zC2 > z) { z = zC2; s = sCheekF; sid = 3; }
    float3 n;
    float3 P;
    if (z < -1e4) {
        // Übergangszone der weichen Vereinigung: Normale zur Seite
        float2 g = normalize(p - hc);
        n = float3(g.x, g.y, 0.2);
        P = float3(p.x, p.y, 0.0);
        sid = -1;
    } else {
        P = float3(p.x, p.y, z);
        n = (P - float3(s.x, s.y, s.z)) / s.w;
    }
    float3 F = headF();
    float3 U = headU();
    float3 S = headS();
    float3 q = P - float3(hc.x, hc.y, 0.0);
    float3 l = float3(dot(q, F), dot(q, U), dot(q, S)) / R;
    float onMuzzle = sid == 1 ? 1.0 : 0.0;
    float3 albedo = headFur(l, onMuzzle, p);
    n = normalize(n + float3(0.0, 0.0, 0.22));
    float3 col = shadeFur(albedo, n, 1.0);
    float inSkull = 1.0 - smoothstep(-upp, upp, dSkull);
    float earMask = 1.0 - smoothstep(-upp, upp, min(dEarN, dEarF));
    if (inSkull < 0.5 && earMask > 0.0) {
        // Ohr: flache Form, Innenseite rosa (je nachdem, wie weit sie zu uns zeigt)
        bool nearEar = dEarN < dEarF;
        int eb = nearEar ? 7 : 9;
        float innerVis = gHead[eb + 1].z;
        float3 earN3 = nearEar ? normalize(float3(-0.2, 0.45, 0.85)) : normalize(float3(0.25, 0.35, 0.6));
        float3 earAlb = headFur(float3(-0.1, 0.95, 0.0), 0.0, p);
        float inner = (1.0 - smoothstep(-upp, upp, earInnerDist(p, eb))) * innerVis;
        earAlb = lerp(earAlb, palette(5), inner);
        col = shadeFur(earAlb, earN3, nearEar ? 1.0 : 0.7);
        n = earN3;
    } else if (sid >= 0) {
        if (sid != 1) {
            // Augen auf Schädel und Backen
            float3 lu = normalize(l);
            float uppL = upp / R;
            float4 e0 = eyeAt(lu, 0, uppL);
            float4 e1 = eyeAt(lu, 1, uppL);
            float4 e = e0;
            if (e1.w > e0.w) e = e1;
            if (e.w > 0.0) {
                float3 L = normalize(float3(-0.38, 0.78, 0.5));
                float3 ecol = float3(e.x, e.y, e.z) * (0.78 + 0.3 * saturate(dot(n, L)));
                col = lerp(col, ecol, e.w);
            }
        }
        // Nase und Mund liegen auf der Schnauze (auch knapp daneben, damit nichts abgeschnitten wird)
        float3 qm = P - float3(sMuz.x, sMuz.y, sMuz.z);
        float3 m = float3(dot(qm, F), dot(qm, U), dot(qm, S)) / sMuz.w;
        if (length(m) < 1.25) {
            float4 nm = noseMouth(normalize(m), upp / sMuz.w);
            if (nm.w > 0.0) {
                float3 L = normalize(float3(-0.38, 0.78, 0.5));
                col = lerp(col, float3(nm.x, nm.y, nm.z) * (0.75 + 0.35 * saturate(dot(n, L))), nm.w);
            }
        }
    }
    float edge = smoothstep(-upp * 2.0, 0.0, d) * (1.0 - pix);
    col *= 1.0 - edge * 0.38;
    col += palette(11) * (rimMask(n) * gInfo.z * (1.0 - edge * 0.5));
    return float4(col * alpha, alpha);
}

// ------------------------------------------------------------------ Schnurrhaare, Schatten, Zusammensetzen

float4 over(float4 top, float4 below) { return top + below * (1.0 - top.w); }

float4 whiskerLayer(float2 p, float upp, float headAlpha) {
    float cover = 0.0;
    for (int i = 0; i < 6; i++) {
        float4 a = gWhisk[i * 2];
        float4 b = gWhisk[i * 2 + 1];
        if (b.z <= 0.0) continue;
        float2 p0 = float2(a.x, a.y);
        float2 p1 = float2(a.z, a.w);
        float2 p2 = float2(b.x, b.y);
        float d = min(sdSegment(p, p0, p1), sdSegment(p, p1, p2));
        float along = saturate(length(p - p0) / max(length(p2 - p0), 1e-3));
        float w = upp * lerp(1.0, 0.4, along);
        float m = (1.0 - smoothstep(w * 0.5, w * 0.5 + upp, d)) * b.z * (1.0 - along * 0.55);
        if (b.w < 0.0) m *= 1.0 - headAlpha;
        cover = max(cover, m);
    }
    float3 c = palette(8);
    return float4(c * cover, cover);
}

float shadowAlpha(float2 p) {
    float2 q = (p - float2(gShadow.x, gShadow.y)) / float2(gShadow.z, gShadow.w);
    return gShadow2.x * (1.0 - smoothstep(0.15, 1.0, length(q)));
}

float4 catPixel(float2 pix) {
    float upp = gView.z;
    float2 p = float2(gView.x + (pix.x + 0.5) * upp, gView.y - (pix.y + 0.5) * upp);
    int count = (int)(gInfo.x + 0.5);
    Acc farA = accInit();
    Acc tailB = accInit();
    Acc body = accInit();
    Acc tailF = accInit();
    float kBody = 1.6;
    float kTail = 0.8;
    float reach = kBody + upp * 3.0;
    for (int i = 0; i < CAT_MAX_SEG; i++) {
        if (i >= count) break;
        float4 s0 = gSeg[i * 3];
        float4 s1 = gSeg[i * 3 + 1];
        float2 a = float2(s0.x, s0.y);
        float2 b = float2(s0.z, s0.w);
        float2 ab = b - a;
        float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
        float2 c = a + ab * t;
        float r = max(lerp(s1.x, s1.y, t), 1e-3);
        float2 dv = p - c;
        float dist = length(dv);
        float d = dist - r;
        if (d > reach) continue;
        float qr = dist / r;
        float3 nrm = float3(dv.x / r, dv.y / r, sqrt(saturate(1.0 - qr * qr)));
        int layer = (int)(s1.z + 0.5);
        int part = (int)(s1.w + 0.5);
        float isLeg = (part >= PART_FUPPER && part <= PART_HPAW) ? 1.0 : 0.0;
        float isCore = part <= PART_NECK ? 1.0 : 0.0;
        if (layer == 0) farA = accAdd(farA, d, nrm, i, kBody, 0.0, 0.0);
        else if (layer == 1) tailB = accAdd(tailB, d, nrm, i, kTail, 0.0, 0.0);
        else if (layer == 2) body = accAdd(body, d, nrm, i, part == PART_TAIL ? kTail : kBody, isLeg, isCore);
        else tailF = accAdd(tailF, d, nrm, i, kTail, 0.0, 0.0);
    }
    float4 o = float4(0.0, 0.0, 0.0, gShadow2.y > 0.5 ? 0.0 : shadowAlpha(p));
    if (farA.i1 >= 0) o = over(layerColor(farA, p, upp, kBody, 0.68, 0.0), o);
    if (tailB.i1 >= 0) o = over(layerColor(tailB, p, upp, kTail, 0.85, 0.0), o);
    if (body.i1 >= 0) o = over(layerColor(body, p, upp, kBody, 1.0, 1.0), o);
    if (tailF.i1 >= 0) o = over(layerColor(tailF, p, upp, kTail, 1.0, 0.0), o);
    float4 hd = float4(0.0, 0.0, 0.0, 0.0);
    if (gHead[0].z > 0.0) {
        hd = headLayer(p, upp);
        o = over(hd, o);
        if (gShadow2.y < 0.5) o = over(whiskerLayer(p, upp, hd.w), o);
    }
    o = o * gInfo.w;
    if (o.w > 0.0) {
        float3 c = toSrgb(float3(o.x, o.y, o.z) / o.w);
        o = float4(c * o.w, o.w);
    }
    return o;
}
