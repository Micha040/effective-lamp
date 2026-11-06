// DeskCats – Daten für den Katzen-Shader (gleiches Layout wie der cbuffer in HLSL) + CPU-Fassung des Shaders.
#pragma once
#include "../shade/hlslcompat.h"

namespace cat {

constexpr int kMaxSeg = 48;

// Reihenfolge und Größe müssen exakt dem cbuffer CatCB im Shader entsprechen (alles float4).
struct CatCB {
    hlsl::float4 view;       // x,y = Katzen-Koordinate von Pixel (0,0), z = Einheiten pro Pixel
    hlsl::float4 info;       // x = Anzahl Röhren, y = Zeit, z = Randlicht, w = Deckkraft
    hlsl::float4 shadow;     // Kontaktschatten: Mitte x,y, Radien x,y
    hlsl::float4 shadow2;    // x = Stärke
    hlsl::float4 pat;        // Muster
    hlsl::float4 pal[16];
    hlsl::float4 seg[kMaxSeg * 3];
    hlsl::float4 head[16];
    hlsl::float4 eye[6];
    hlsl::float4 whisk[12];
    hlsl::float4 patE[6];    // bis zu 3 Fell-Flecken (Ellipsen): (Mitte x,y, Radien), (cos, sin, Stärke, -)
};

// Rendert die Katze wie der Pixel-Shader in ein Bild (RGBA, vormultipliziertes Alpha, sRGB, 8 Bit).
void renderCpu(const CatCB& cb, int width, int height, unsigned char* rgba);
// Einzelnes Pixel (für Tests)
hlsl::float4 shadePixel(const CatCB& cb, float x, float y);

}  // namespace cat
