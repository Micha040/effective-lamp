// FragDesk – Hilfen rund um CS2 (Aufnahmen, Rücksicht aufs Spiel), ohne Windows-Abhängigkeit (damit unter Linux testbar)
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace fd {

// Performance-Modus (Einstellungen › Counter-Strike 2): was FragDesk tut, solange CS2 läuft. Gilt für den Abgleich mit
// dem eigenen Server, „Demo von Valve laden“ und die Handy-App direkt vom PC – Auswerten, Aufnehmen und Ansehen in CS2
// betrifft er nicht.
enum class Cs2Mode {
    Performance,   // alles wartet, bis CS2 zu ist (Standard, wie bisher)
    Manual,        // nur der Hintergrund wartet; was du anstößt (Klick, Anfrage vom Handy), läuft sofort
    Auto,          // CS2 hält nichts an
};

// Wie in settings.json: "performance", "manual", "auto"; alles andere (auch leer) ist Performance
Cs2Mode parseCs2Mode(const std::string& name);
const char* cs2ModeName(Cs2Mode mode);

// Muss diese Arbeit warten, solange CS2 läuft? userStarted: per Klick oder vom Handy angestoßen (nicht im Hintergrund)
bool pausesForCs2(Cs2Mode mode, bool userStarted);

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
