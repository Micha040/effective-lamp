// SnowDesk – HLSL (wird beim Start mit d3dcompiler_47.dll übersetzt). Formeln identisch zur Software-Vorschau
// (tests/preview.cpp), damit das Video zeigt, wie es auf dem Bildschirm aussieht.
#pragma once

namespace app {

static const char kShaderSource[] = R"HLSL(
cbuffer Frame : register(b0)
{
    float2 viewSize;   // Pixel
    float  scale;      // DPI-Faktor
    float  pad0;
};

static const float PI = 3.14159265;

// atan2 als Polynom (Fehler ~1e-5): unabhängig von Compiler-Eigenheiten (z. B. Wine/vkd3d kennt atan2 nicht)
float atan2p(float y, float x)
{
    float ax = abs(x), ay = abs(y);
    float t0 = max(ax, ay), t1 = min(ax, ay);
    float t3 = t1 / max(t0, 1e-12);
    float t4 = t3 * t3;
    float p = -0.013480470;
    p = p * t4 + 0.057477314;
    p = p * t4 - 0.121239071;
    p = p * t4 + 0.195635925;
    p = p * t4 - 0.332994597;
    p = p * t4 + 0.999995630;
    t3 = p * t3;
    t3 = ay > ax ? 1.570796327 - t3 : t3;
    t3 = x < 0.0 ? 3.141592654 - t3 : t3;
    return y < 0.0 ? -t3 : t3;
}

float4 toClip(float2 p)
{
    return float4(p.x / viewSize.x * 2.0 - 1.0, 1.0 - p.y / viewSize.y * 2.0, 0.0, 1.0);
}

// ---------------------------------------------------------------- Flocken, Kristalle, Klumpen, Puder

struct SpriteIn
{
    float2 pos   : POS;
    float  size  : SIZE;
    float  alpha : ALPHA;
    float  rot   : ROT;
    float  soft  : SOFT;
    float  seed  : SEED;
    uint   kind  : KIND;
    uint   vid   : SV_VertexID;
};

struct SpriteOut
{
    float4 pos : SV_Position;
    float2 local : LOCAL;                        // relativ zum Mittelpunkt, in Radien
    nointerpolation float4 params : PARAMS;      // alpha, soft, seed, radius
    nointerpolation float2 rotcs : ROTCS;
    nointerpolation uint kind : KIND;
};

SpriteOut vsSprite(SpriteIn i)
{
    float2 corner = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    SpriteOut o;
    o.rotcs = float2(cos(i.rot), sin(i.rot));
    o.kind = i.kind;
    if (i.kind == 9)
    {
        // Blitz: der ganze Bildschirm
        o.pos = float4(corner, 0.0, 1.0);
        o.local = corner;
        o.params = float4(i.alpha, 0.0, 0.0, 1.0);
        return o;
    }
    if (i.kind == 5)
    {
        // Regenspur: schmales Rechteck entlang der Flugrichtung, Kopf (pos) vorne
        float2 dir = float2(sin(i.rot), cos(i.rot));
        float2 nrm = float2(dir.y, -dir.x);
        float halfW = max(i.soft, 0.6) * 0.5 + 1.0;
        float along = (corner.y + 1.0) * 0.5;
        float2 tail = i.pos - dir * i.size;
        float2 head = i.pos + dir;
        o.pos = toClip(lerp(tail, head, along) + nrm * (corner.x * halfW));
        o.local = float2(corner.x * halfW / max(i.soft * 0.5, 0.3), along);
        o.params = float4(i.alpha, i.soft, i.seed, i.size);
        return o;
    }
    float radius, extent;
    if (i.kind == 6)
    {
        radius = max(i.size * 0.5, 0.5);                 // Tropfen: soft = senkrechte Streckung
        extent = radius * max(i.soft, 1.0) * 1.3 + 1.5;
    }
    else if (i.kind == 8)
    {
        radius = max(i.size * 0.5, 1.0);                 // Nebel
        extent = radius;
    }
    else
    {
        radius = max(i.size * 0.5 * (1.0 + i.soft * 0.8), 0.5);
        extent = radius * 1.2 + 1.5;
    }
    o.pos = toClip(i.pos + corner * extent);
    o.local = corner * extent / radius;
    o.params = float4(i.alpha, i.soft, i.seed, radius);
    return o;
}

float segDist(float2 p, float2 a, float2 b)
{
    float2 pa = p - a, ba = b - a;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h);
}

// Schneekristall: sechsfach symmetrisch, Arme mit Seitenästen (q in Radien)
float crystalCoverage(float2 q0, float px)
{
    float r = length(q0);
    if (r > 1.05) return 0.0;
    float ang = atan2p(q0.y, q0.x);
    float sector = PI / 3.0;
    float a = fmod(ang + 2.0 * PI, sector) - sector * 0.5;
    float2 q = float2(r * cos(a), abs(r * sin(a)));
    const float c60 = 0.5, s60 = 0.8660254;
    float d = segDist(q, float2(0.0, 0.0), float2(0.92, 0.0)) - 0.075;
    d = min(d, segDist(q, float2(0.42, 0.0), float2(0.42 + 0.30 * c60, 0.30 * s60)) - 0.055);
    d = min(d, segDist(q, float2(0.66, 0.0), float2(0.66 + 0.18 * c60, 0.18 * s60)) - 0.045);
    d = min(d, r - 0.16);
    return 1.0 - smoothstep(-px, px, d);
}

float4 psSprite(SpriteOut i) : SV_Target
{
    float alpha = i.params.x, soft = i.params.y, seed = i.params.z, radius = i.params.w;
    float2 p = i.local;
    float3 c = float3(0.95, 0.97, 1.0);
    float a;
    if (i.kind == 9)
    {
        // Blitz: oben heller
        a = alpha * (0.55 + 0.45 * (p.y + 1.0) * 0.5);
        return float4(float3(0.93, 0.95, 1.0) * a, a);
    }
    if (i.kind == 5)
    {
        // Regenspur: weiche Ränder, zum Kopf hin heller
        float halfPx = max(soft * 0.5, 0.3);
        float w = 1.0 - smoothstep(1.0 - 0.5 / halfPx, 1.0 + 1.0 / halfPx, abs(p.x));
        a = alpha * w * (0.12 + 0.88 * pow(saturate(p.y), 1.7));
        if (a <= 0.002) discard;
        return float4(float3(0.80, 0.86, 0.93) * a, a);
    }
    if (i.kind == 6)
    {
        // Wassertropfen: fast durchsichtig, dunkler Rand (Lichtbrechung), Glanzlicht oben links, Licht unten
        float st = max(soft, 0.5);
        float2 q = float2(p.x, p.y / st);
        if (st > 1.02) q.x *= 1.0 + (st - 1.0) * saturate(-q.y) * 1.2;   // hängend: oben schmaler (Träne)
        float d = length(q);
        float cov = 1.0 - smoothstep(1.0 - 1.2 / radius, 1.0, d);
        float rim = smoothstep(0.45, 1.0, d);
        float3 col = lerp(float3(0.80, 0.88, 0.97), float3(0.22, 0.30, 0.42), rim);
        float body = lerp(0.18, 0.66, rim);
        float hl = 1.0 - smoothstep(0.0, 0.34, length(q - float2(-0.34, -0.40)));
        float bl = (1.0 - smoothstep(0.0, 0.42, length(q - float2(0.22, 0.48)))) * 0.45;
        float lit = saturate(hl + bl);
        col = lerp(col, float3(1.0, 1.0, 1.0), lit);
        a = alpha * cov * max(body, lit * 0.95);
        if (a <= 0.002) discard;
        return float4(col * a, a);
    }
    if (i.kind == 8)
    {
        // Nebelschwade: sehr weicher Verlauf, flach gedrückt (soft = Höhe/Breite)
        float f = saturate(1.0 - length(float2(p.x, p.y / max(soft, 0.2))));
        a = alpha * f * f * (3.0 - 2.0 * f);
        if (a <= 0.002) discard;
        return float4(float3(0.86, 0.88, 0.92) * a, a);
    }
    if (i.kind == 7) c = float3(0.86, 0.91, 0.98);   // Spritzer: Wasser statt Schnee
    if (i.kind == 1)
    {
        float2 l = float2(p.x * i.rotcs.x - p.y * i.rotcs.y, p.x * i.rotcs.y + p.y * i.rotcs.x);
        a = alpha * crystalCoverage(l, 1.2 / radius);
    }
    else if (i.kind == 2 || i.kind == 3)
    {
        // weicher, unregelmäßiger Klumpen
        float d = length(p);
        float ang = atan2p(p.y, p.x);
        float lump = 1.0 + 0.07 * sin(3.0 * ang + seed * 6.28) + 0.04 * sin(5.0 * ang + seed * 12.6);
        float edge = 1.5 / radius;
        a = alpha * (1.0 - smoothstep(lump * (1.0 - soft * 0.6) - edge, lump, d));
        float shade = 0.9 + 0.1 * clamp(-p.y, -1.0, 1.0);
        c = float3(0.95 * shade, 0.97 * shade, min(1.0, shade + 0.04));
    }
    else
    {
        float d = length(p);
        float core = 1.0 - soft * 0.6;
        float e0 = soft < 0.5 ? core - 1.2 / radius : core * 0.999;
        a = alpha * (1.0 - smoothstep(e0, 1.0, d));
    }
    if (a <= 0.002) discard;
    return float4(c * a, a);
}

// ---------------------------------------------------------------- Schneedecken

struct PileIn
{
    float2 pos    : POSITION;
    float  dist   : DIST;
    float  height : HEIGHT;
};

struct PileOut
{
    float4 pos    : SV_Position;
    float  dist   : DIST;
    float  height : HEIGHT;
};

PileOut vsPile(PileIn i)
{
    PileOut o;
    o.pos = toClip(i.pos);
    o.dist = i.dist;
    o.height = i.height;
    return o;
}

float4 psPile(PileOut i) : SV_Target
{
    float edge = saturate(i.dist + 0.5);                       // weiche Oberkante
    float depthT = saturate(i.dist / max(i.height, 1.0));
    float thick = saturate(i.height / (14.0 * scale));
    float f = smoothstep(0.0, 1.0, depthT) * (0.35 + 0.65 * thick);
    float3 c = lerp(float3(0.985, 0.99, 1.0), float3(0.80, 0.865, 0.955), f);
    float glint = i.dist < 1.8 ? 0.03 * (1.0 - i.dist / 1.8) : 0.0;   // heller Rand oben
    c.rg = min(1.0, c.rg + glint);
    float a = 0.97 * edge;
    return float4(c * a, a);
}
)HLSL";

}  // namespace app
