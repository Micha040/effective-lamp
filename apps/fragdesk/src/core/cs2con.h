// FragDesk – Hilfen für Aufnahmen mit CS2, ohne Windows-Abhängigkeit (damit unter Linux testbar)
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace fd {

// Letzter Zustand der CS2-Oberfläche in der Konsolenausgabe ab Position from: "INGAME", "LOADINGSCREEN", "MAINMENU" …
// aus Zeilen wie "ChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_STATE_INGAME". Es zählen nur
// vollständige Zeilen (eine halb geschriebene könnte den Namen abschneiden). Leer: kein Wechsel seit from.
std::string lastUiState(const std::string& text, size_t from = 0);

struct IRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// Wohin mit dem Mauszeiger, damit er nicht im aufgenommenen Bereich area zu sehen ist. Der Zeiger ragt von seiner Spitze
// (x, y) aus bis zu size Pixel nach rechts und unten. Ergebnis: der nächste Platz neben area, der auf einem Bildschirm
// liegt; gibt es keinen (area füllt alle Bildschirme), die Ecke rechts unten in area – dann ist nur die Spitze im Bild.
// false: Der Zeiger berührt area gar nicht.
bool cursorParkPoint(const IRect& area, int x, int y, int size, const std::vector<IRect>& monitors, int& outX, int& outY);

}  // namespace fd
