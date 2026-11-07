#include "radar_image.h"

#include <shlwapi.h>

#include <cmath>

#include "../core/radar.h"
#include "common.h"

using namespace Gdiplus;

namespace fdw {

namespace {

// PNG aus den Ressourcen
std::unique_ptr<Bitmap> loadPng(int id) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!res) return nullptr;
    HGLOBAL h = LoadResource(nullptr, res);
    DWORD size = SizeofResource(nullptr, res);
    const void* p = h ? LockResource(h) : nullptr;
    if (!p || !size) return nullptr;
    IStream* s = SHCreateMemStream(static_cast<const BYTE*>(p), size);
    if (!s) return nullptr;
    std::unique_ptr<Bitmap> bmp(Bitmap::FromStream(s));
    s->Release();
    if (!bmp || bmp->GetLastStatus() != Ok) return nullptr;
    return bmp;
}

}  // namespace

Bitmap* RadarImages::get(int index, bool lower, int px) {
    if (index < 0 || px <= 0) return nullptr;
    Entry& e = e_[lower ? 1 : 0];
    if (e.bmp && e.index == index && e.px == px) return e.bmp.get();
    int id = fd::kRadarResBase + 2 * index + (lower ? 1 : 0);
    std::unique_ptr<Bitmap> src = loadPng(id);
    if (!src) {
        logLine(L"Radarbild %d fehlt", id);
        return nullptr;
    }
    auto out = std::make_unique<Bitmap>(px, px, PixelFormat32bppPARGB);
    {
        Graphics g(out.get());
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.Clear(Color(0, 0, 0, 0));
        g.DrawImage(src.get(), 0, 0, px, px);
    }
    e.bmp = std::move(out);
    e.index = index;
    e.px = px;
    return e.bmp.get();
}

void RadarImages::release() {
    for (auto& e : e_) {
        e.bmp.reset();
        e.index = -1;
        e.px = 0;
    }
}

void drawRadarImage(ui::Canvas& c, RadarImages& images, int index, bool lower, const RectF& rr) {
    int px = int(std::lround(rr.Width * c.scale));
    Bitmap* bmp = images.get(index, lower, px);
    if (!bmp) {
        Pen pen(Color(26, 255, 255, 255), 1);
        for (int i = 0; i <= 8; ++i) {
            float gx = rr.X + rr.Width * i / 8, gy = rr.Y + rr.Height * i / 8;
            c.g.DrawLine(&pen, gx, rr.Y, gx, rr.Y + rr.Height);
            c.g.DrawLine(&pen, rr.X, gy, rr.X + rr.Width, gy);
        }
        return;
    }
    // schon in Bildschirmpixeln: ohne Transformation 1:1 zeichnen
    GraphicsState st = c.g.Save();
    float dx = std::round((rr.X + c.ox()) * c.scale), dy = std::round((rr.Y + c.oy()) * c.scale);
    c.g.ResetTransform();
    c.g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    c.g.DrawImage(bmp, dx, dy, REAL(px), REAL(px));
    c.g.Restore(st);
}

}  // namespace fdw
