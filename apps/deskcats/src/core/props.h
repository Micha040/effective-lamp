// DeskCats – kleine Gegenstände als Pixel-Art: Futternapf und Wollknäuel
#pragma once
#include "pixcat.h"

namespace cat {

// Futternapf (Seitenansicht). food 0..1 = Füllung. Nullpunkt: Boden-Mitte.
void renderBowl(double food, PixelImage& out);
// Wollknäuel, gedreht um `spin` (Umdrehungen). Nullpunkt: Boden-Mitte.
void renderYarn(float spin, PixelImage& out);

}  // namespace cat
