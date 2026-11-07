// FragDesk – Heatmap-Ansicht: Radarkarte mit Dichte (Aufenthalt, Kills, Tode, Granaten) und anklickbaren Punkten.
// Für ein Match (Tab „Heatmap“, Datei synchron geladen) oder viele Matches einer Map (Statistiken, im Hintergrund).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../core/heat.h"
#include "gfx.h"
#include "radar_image.h"

namespace fdw {

class App;

struct HeatEvent {   // Ergebnis des Ladens im Hintergrund (WM_APP_HEAT)
    std::string key;
    std::shared_ptr<fd::HeatSet> set;
};

std::wstring heatFile(const std::string& checksum);   // matches\<Prüfsumme>.heat

class HeatView {
public:
    explicit HeatView(bool many) : marks(!many), many_(many) {}

    // Auswahl
    uint32_t layers = 1u << fd::HlPresence;
    fd::HeatFilter filter{fd::WhoMe, fd::SideT | fd::SideCT};
    bool throws = false;
    bool marks;

    // Daten
    enum class State { Idle, Working, Ready, Missing, NoRadar };
    State state = State::Idle;
    std::string key;   // was geladen ist oder gerade lädt
    std::shared_ptr<const fd::HeatSet> set;
    const fd::RadarMap* radar = nullptr;
    int radarIndex = -1;

    void loadMatch(const fd::Match& m, const std::string& me, const std::string& key);
    // now: gleich laden (Bilder für die Doku), sonst im Hintergrund mit WM_APP_HEAT an hwnd
    void loadMany(HWND hwnd, std::vector<fd::HeatMatchInfo> infos, const std::string& map, const std::string& me, const std::string& key,
                  bool now);
    bool apply(HeatEvent* e);   // true, wenn das Ergebnis zu dieser Ansicht gehört
    void release();             // Bilder freigeben (Seite verlassen)

    // Karte und Auswahl zeichnen; liefert die Höhe
    float draw(ui::Canvas& c, App& app, float x, float y, float w);

private:
    bool many_;
    RadarImages images_;
    std::string gridKey_, overlayKey_;
    std::unique_ptr<Gdiplus::Bitmap> grid_[2], overlay_[2];   // Dichte (Raster) und Punkte/Linien (Bildschirmpixel)
    int counts_[fd::HlCount] = {};
    void setMap(const std::string& map);
    void buildGrids();
    void buildOverlay(const std::vector<const fd::HeatMark*>& list, int px, float scale, int levels);
    float panel(ui::Canvas& c, App& app, float x, float y, float w);
};

}  // namespace fdw
