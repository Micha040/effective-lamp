// FragDesk – Radarbilder aus den Ressourcen (2D-Ansicht und Heatmap), passend auf Bildschirmpixel skaliert
#pragma once

#include <memory>

#include "gfx.h"

namespace fdw {

class RadarImages {
public:
    // Bild der Karte index (untere Ebene: lower) in px × px Bildschirmpixeln; nullptr ohne Bild.
    // Merkt sich je Ebene das zuletzt skalierte Bild (das große Original wird danach gleich wieder freigegeben).
    Gdiplus::Bitmap* get(int index, bool lower, int px);
    void release();

private:
    struct Entry {
        int index = -1, px = 0;
        std::unique_ptr<Gdiplus::Bitmap> bmp;
    };
    Entry e_[2];   // obere, untere Ebene
};

// Radarbild pixelgenau in rr zeichnen; ohne Bild ein dezentes Raster
void drawRadarImage(ui::Canvas& c, RadarImages& images, int index, bool lower, const Gdiplus::RectF& rr);

}  // namespace fdw
