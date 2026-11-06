// DeskCats – HLSL-Kompatibilität für C++.
// Der Katzen-Shader (catshade.hlsl) wird zweimal übersetzt: als Pixel-Shader für die Grafikkarte und als C++ für
// Tests und Vorschauvideos. Dieses Header liefert dafür die HLSL-Typen und -Funktionen, die der Shader benutzt.
// Alles bewusst als float-Überladungen (keine Templates), damit Literale wie 0.5 wie in HLSL einfach passen.
#pragma once
#include <cmath>

namespace hlsl {

struct float2 {
    float x, y;
    float2() : x(0), y(0) {}
    float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
    float x, y, z;
    float3() : x(0), y(0), z(0) {}
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
    float3(const float2& v, float c) : x(v.x), y(v.y), z(c) {}
};
struct float4 {
    float x, y, z, w;
    float4() : x(0), y(0), z(0), w(0) {}
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float4(const float3& v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

// ---- float2
inline float2 operator+(float2 a, float2 b) { return float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(float2 a, float2 b) { return float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(float2 a, float2 b) { return float2(a.x * b.x, a.y * b.y); }
inline float2 operator/(float2 a, float2 b) { return float2(a.x / b.x, a.y / b.y); }
inline float2 operator*(float2 a, float s) { return float2(a.x * s, a.y * s); }
inline float2 operator*(float s, float2 a) { return float2(a.x * s, a.y * s); }
inline float2 operator/(float2 a, float s) { return float2(a.x / s, a.y / s); }
inline float2 operator+(float2 a, float s) { return float2(a.x + s, a.y + s); }
inline float2 operator-(float2 a, float s) { return float2(a.x - s, a.y - s); }
inline float2 operator-(float s, float2 a) { return float2(s - a.x, s - a.y); }
inline float2 operator-(float2 a) { return float2(-a.x, -a.y); }
inline float2& operator+=(float2& a, float2 b) { a = a + b; return a; }
inline float2& operator-=(float2& a, float2 b) { a = a - b; return a; }
inline float2& operator*=(float2& a, float s) { a = a * s; return a; }
// ---- float3
inline float3 operator+(float3 a, float3 b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline float3 operator-(float3 a, float3 b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float3 operator*(float3 a, float3 b) { return float3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float3 operator/(float3 a, float3 b) { return float3(a.x / b.x, a.y / b.y, a.z / b.z); }
inline float3 operator*(float3 a, float s) { return float3(a.x * s, a.y * s, a.z * s); }
inline float3 operator*(float s, float3 a) { return float3(a.x * s, a.y * s, a.z * s); }
inline float3 operator/(float3 a, float s) { return float3(a.x / s, a.y / s, a.z / s); }
inline float3 operator+(float3 a, float s) { return float3(a.x + s, a.y + s, a.z + s); }
inline float3 operator-(float3 a, float s) { return float3(a.x - s, a.y - s, a.z - s); }
inline float3 operator-(float s, float3 a) { return float3(s - a.x, s - a.y, s - a.z); }
inline float3 operator-(float3 a) { return float3(-a.x, -a.y, -a.z); }
inline float3& operator+=(float3& a, float3 b) { a = a + b; return a; }
inline float3& operator+=(float3& a, float s) { a = a + s; return a; }
inline float3& operator-=(float3& a, float3 b) { a = a - b; return a; }
inline float3& operator*=(float3& a, float s) { a = a * s; return a; }
inline float3& operator*=(float3& a, float3 b) { a = a * b; return a; }

// ---- float4
inline float4 operator+(float4 a, float4 b) { return float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline float4 operator-(float4 a, float4 b) { return float4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline float4 operator*(float4 a, float4 b) { return float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline float4 operator*(float4 a, float s) { return float4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline float4 operator*(float s, float4 a) { return float4(a.x * s, a.y * s, a.z * s, a.w * s); }

// ---- Funktionen (nur float, keine Templates)
inline float abs(float x) { return std::fabs(x); }
inline float min(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline float sqrt(float x) { return std::sqrt(x); }
inline float sin(float x) { return std::sin(x); }
inline float cos(float x) { return std::cos(x); }
inline float exp(float x) { return std::exp(x); }
inline float pow(float a, float b) { return std::pow(a, b); }
inline float floor(float x) { return std::floor(x); }
inline float frac(float x) { return x - std::floor(x); }
inline float sign(float x) { return x > 0.f ? 1.f : (x < 0.f ? -1.f : 0.f); }
inline float saturate(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float clamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}
inline float step(float edge, float x) { return x >= edge ? 1.f : 0.f; }

inline float2 abs(float2 v) { return float2(abs(v.x), abs(v.y)); }
inline float2 min(float2 a, float2 b) { return float2(min(a.x, b.x), min(a.y, b.y)); }
inline float2 max(float2 a, float2 b) { return float2(max(a.x, b.x), max(a.y, b.y)); }
inline float2 floor(float2 v) { return float2(floor(v.x), floor(v.y)); }
inline float2 frac(float2 v) { return float2(frac(v.x), frac(v.y)); }
inline float2 saturate(float2 v) { return float2(saturate(v.x), saturate(v.y)); }
inline float2 lerp(float2 a, float2 b, float t) { return a + (b - a) * t; }
inline float dot(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
inline float length(float2 v) { return sqrt(dot(v, v)); }
inline float2 normalize(float2 v) { float l = length(v); return l > 0.f ? v / l : v; }

inline float3 abs(float3 v) { return float3(abs(v.x), abs(v.y), abs(v.z)); }
inline float3 min(float3 a, float3 b) { return float3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
inline float3 max(float3 a, float3 b) { return float3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
inline float3 frac(float3 v) { return float3(frac(v.x), frac(v.y), frac(v.z)); }
inline float3 saturate(float3 v) { return float3(saturate(v.x), saturate(v.y), saturate(v.z)); }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float3 lerp(float3 a, float3 b, float3 t) { return a + (b - a) * t; }
inline float3 pow(float3 v, float e) { return float3(pow(v.x, e), pow(v.y, e), pow(v.z, e)); }
inline float3 sqrt(float3 v) { return float3(sqrt(v.x), sqrt(v.y), sqrt(v.z)); }
inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(float3 v) { return sqrt(dot(v, v)); }
inline float3 normalize(float3 v) { float l = length(v); return l > 0.f ? v / l : v; }
inline float3 cross(float3 a, float3 b) { return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

}  // namespace hlsl
