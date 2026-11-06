// DeskCats – CPU-Fassung des Katzen-Shaders: derselbe Quelltext wie auf der Grafikkarte (catshade.hlsl).
#include "catdraw.h"

namespace hlsl {
static thread_local const cat::CatCB* g_cb = nullptr;
#define gView (g_cb->view)
#define gInfo (g_cb->info)
#define gShadow (g_cb->shadow)
#define gShadow2 (g_cb->shadow2)
#define gPat (g_cb->pat)
#define gPal (g_cb->pal)
#define gSeg (g_cb->seg)
#define gHead (g_cb->head)
#define gEye (g_cb->eye)
#define gWhisk (g_cb->whisk)
#define gPatE (g_cb->patE)
#include "../shade/catshade.hlsl"
#undef gView
#undef gInfo
#undef gShadow
#undef gShadow2
#undef gPat
#undef gPal
#undef gSeg
#undef gHead
#undef gEye
#undef gWhisk
#undef gPatE
}  // namespace hlsl

namespace cat {

hlsl::float4 shadePixel(const CatCB& cb, float x, float y) {
    hlsl::g_cb = &cb;
    return hlsl::catPixel(hlsl::float2(x, y));
}

void renderCpu(const CatCB& cb, int width, int height, unsigned char* rgba) {
    hlsl::g_cb = &cb;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            hlsl::float4 c = hlsl::catPixel(hlsl::float2((float)x, (float)y));
            unsigned char* o = rgba + ((size_t)y * width + x) * 4;
            auto q = [](float v) {
                v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
                return (unsigned char)(v * 255.f + 0.5f);
            };
            o[0] = q(c.x);
            o[1] = q(c.y);
            o[2] = q(c.z);
            o[3] = q(c.w);
        }
    }
}

}  // namespace cat
