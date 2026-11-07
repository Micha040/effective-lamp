// FragDesk – 2D-Ansicht einer Runde: Zustand, Laden der Rundendaten und Radarbilder
//
// Die Rundendaten (fd::Replay) erzeugt fdanalyze beim ersten Öffnen eines Matches aus der Demo und legt sie neben die
// Auswertung (matches\<Prüfsumme>.replay). Danach lädt FragDesk nur noch die Datei. Beides läuft im Hintergrund; das
// Ergebnis kommt als WM_APP_REPLAY (lParam: ReplayEvent*) zurück.
#pragma once

#include "gfx.h"   // windows.h und gdiplus.h in der richtigen Reihenfolge
#include "radar_image.h"

#include <memory>
#include <string>
#include <vector>

#include "../core/model.h"
#include "../core/radar.h"
#include "../core/replay.h"

namespace fdw {

struct ReplayEvent {
    std::string match;
    std::shared_ptr<fd::Replay> data;
    std::wstring error;
};

// Spieler einer Runde (für die Liste neben der Karte)
struct RosterEntry {
    uint8_t player = 0;
    bool ct = false;
};

class ReplayView {
public:
    // Zustand der Seite
    std::string match;   // Prüfsumme
    int round = 0;       // Rundennummer (1-basiert), 0 = noch keine
    double tick = 0;     // aktuelle Stelle
    bool playing = false;
    double speed = 1;
    std::string follow;   // hervorgehobener Spieler (siehe playerKey)
    bool names = true;

    enum class State { Idle, Working, Ready, Failed };
    State state = State::Idle;
    std::wstring error;
    std::shared_ptr<const fd::Replay> data;

    // Ausschnitt der aktuellen Runde
    int startTick = 0, freezeTick = 0, endTick = 0;
    std::vector<RosterEntry> roster;

    // Karte: Radarbild aus den Ressourcen, sonst ein Ausschnitt, der alle Positionen umfasst
    const fd::RadarMap* radar = nullptr;
    int radarIndex = -1;
    fd::RadarMap fallback{};

    // Rundendaten laden oder erzeugen (im Hintergrund); round 0 = erste Runde
    void open(HWND hwnd, const fd::Match& m, int round);
    void openNow(const fd::Match& m, int round);   // ohne Hintergrund (Bilder für die Doku)
    // Ergebnis aus dem Hintergrund übernehmen (UI-Thread); true, wenn es zum offenen Match gehört
    bool apply(ReplayEvent* e, const fd::Match* m);
    void setRound(const fd::Match& m, int n);
    bool stepRound(const fd::Match& m, int dir);   // vorige (-1) oder nächste (+1) Runde; false am Rand
    void seek(double t);
    void togglePlay();
    // Zeit weiterlaufen lassen (Timer); false, wenn nichts mehr läuft
    bool advance();

    const fd::RadarMap& map() const { return radar ? *radar : fallback; }
    RadarImages images;   // Radarbilder in Bildschirmpixeln
    void releaseBitmaps() { images.release(); }
    int pendingTick = -1;   // nach dem Laden dorthin springen (aus der Heatmap), −1 = nicht

    static std::string playerKey(const fd::ReplayPlayer& p) { return p.steamId.empty() ? "#" + p.name : p.steamId; }

private:
    double last_ = 0;   // Zeit des letzten Schritts (Sekunden, 0 = neu beginnen)
    void prepareMap();
    void applyPending();
};

}  // namespace fdw
