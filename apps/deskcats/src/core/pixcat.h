// DeskCats – Pixel-Art-Darstellung der Katzen.
// Die Haltung (Pose) kommt aus dem Rig; daraus entsteht ein kleines Pixelbild mit harten Kanten, festen
// Farbstufen je Fellbereich, Kontur und gezeichneten Augen/Nase. Läuft auf der CPU (winzige Bilder).
#pragma once
#include "cat.h"

#include <cstdint>
#include <vector>

namespace cat {

struct PixelImage {
    int w = 0, h = 0;
    int originX = 0, originY = 0;        // Pixel, in dem der Katzen-Nullpunkt (Boden, x=0) liegt
    std::vector<uint32_t> px;            // 0xAARRGGBB, vormultipliziert (A = 0 oder 255)
};

struct PixelStyle {
    float pxPerCm = 1.25f;
    float headScale = 1.1f;              // Kopf etwas größer als echt (liest sich in Pixeln besser)
};

// Zeichnet die Katze. facingLeft spiegelt (Licht bleibt links oben). Das Bild wird so groß wie nötig angelegt.
void renderPixelCat(const Look& lk, const Pose& p, bool facingLeft, const PixelStyle& st, PixelImage& out);

}  // namespace cat
