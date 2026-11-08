// FragDesk – Zustand der App (Seiten, Filter, Auswahl) und die Seiten selbst
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../core/stats.h"
#include "gfx.h"
#include "heat_view.h"
#include "library.h"
#include "replay_view.h"
#include "widgets.h"
#include "settings.h"
#include "steam.h"

namespace fdw {

class Recorder;
class MobileHost;
class ServerSync;
class ServerAutodl;
class DemoFetch;

enum class Page { Dashboard, Matches, Match, Stats, Highlights, Settings, Replay, Count };

struct SortState {
    int col = -1;
    bool desc = true;
};

// Ein aufgenommenes Highlight-Video
struct Clip {
    std::wstring path, title;
    std::string match, highlight, map;
    int64_t created = 0, size = 0;
    double seconds = 0;
};

// Ein Moment zum Ansehen oder Aufnehmen
struct MomentRef {
    std::string match;       // Prüfsumme
    std::string highlight;   // ID im Match
};

class App {
public:
    App();
    ~App();

    Settings settings;
    SteamInfo steam;
    Library lib;
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr<MobileHost> mobile;   // Handy-App direkt vom PC (Einstellungen › Handy-App)
    std::unique_ptr<ServerSync> sync;     // Handy-App über den eigenen Server (Abgleich in beide Richtungen)
    std::unique_ptr<ServerAutodl> autodl; // eigener Server: Demos automatisch laden, Steam-Bot (Einstellungen)
    std::unique_ptr<DemoFetch> demoFetch; // „Demo von Valve laden“ für vom Server geholte Matches

    // eigener Spieler
    std::string me;
    std::wstring meName;
    void detectMe();
    void applyFolders();

    // Navigation
    Page page = Page::Dashboard, backPage = Page::Matches;
    void go(Page p);
    void openMatch(const std::string& checksum, int tab = 0);
    void openReplay(int round = 0, int tick = -1);   // 2D-Ansicht des offenen Matches (Runde 0 = erste; tick: dorthin springen)
    void openReplayAt(const std::string& checksum, int round, int tick);   // 2D-Ansicht kurz vor tick (aus der Heatmap)
    ReplayView replay;
    HeatView matchHeat{false}, statsHeat{true};   // Heatmap eines Matches / aller Matches einer Map
    std::string heatMap;                          // Map der Heatmap in den Statistiken (leer = meistgespielte)
    void refreshDetail();                         // offenes Match neu laden (nach dem Neu-Auswerten)
    std::string matchId;
    std::unique_ptr<fd::Match> detail;
    std::wstring detailError;
    int matchTab = 0, statsTab = 0, highlightsTab = 0;
    std::string focusPlayer;   // Spieler im Match, dessen Werte gezeigt werden (leer = ich)

    // Filter (Übersicht und Statistik teilen sie)
    int modeFilter = 0;     // 0 alle, 1 Premier, 2 FACEIT, 3 Wettkampf, 4 Wingman
    int periodFilter = 2;   // 0 letzte 10, 1 letzte 30, 2 alle
    std::string mapFilter;
    fd::Filter filter() const;
    static std::vector<std::wstring> modeLabels();
    static std::vector<std::wstring> periodLabels();
    int listMode = 0;   // Filter der Match-Liste

    // Karriere (zwischengespeichert)
    const fd::Career& career();
    void invalidateCareer() { careerDirty_ = true; }
    std::vector<const fd::Match*> matchPtrs() const;

    // Tabellen, Auswahl, Scrollen
    std::map<std::string, SortState> sorts;
    SortState& sort(const std::string& key, int defCol = -1, bool defDesc = true);
    std::set<std::string> selected;   // "Prüfsumme/Highlight-ID"
    bool allPlayers = false;          // Momente aller Spieler zeigen
    float scroll[int(Page::Count)] = {};

    // Aufnahmen
    std::vector<Clip> clips;
    void loadClips();
    void saveClips();
    const Clip* clipFor(const std::string& match, const std::string& highlight) const;
    void watch(const MomentRef& m);
    void record(const std::vector<MomentRef>& moments);
    bool recording() const;
    std::wstring recordStatus() const;
    void cancelRecording();
    // Grund, warum Momente dieses Matches gerade nicht angesehen/aufgenommen werden können (leer = es geht)
    std::wstring momentBlocker(const fd::Match& m);
    bool demoAvailable(const fd::Match& m);   // Demo-Datei noch da? (kurz zwischengespeichert)
    void demosChanged() { demoCache_.clear(); }   // z. B. Demo von Valve geladen: gleich neu nachsehen
    std::wstring noDemoText(const fd::Match& m);   // warum die Demo fehlt („nicht mehr da“, „nicht auf diesem PC“)
    Tools tools();                            // CS2 Workshop Tools installiert? (kurz zwischengespeichert)
    std::wstring recordError;                 // Grund, warum die letzte Aufnahme scheiterte (bis zum Wegklicken)
    std::wstring recordBlocker();             // Grund gegen Ansehen/Aufnehmen unabhängig vom Match (leer = es geht)

    // Matches aus einer älteren Version (ohne Heatmap und Crosshair-Werte) neu auswerten
    static bool outdated(const fd::Match& m) { return m.schema < fd::kMatchSchema; }
    int reanalyze(const std::vector<const fd::Match*>& matches);   // stellt die mit vorhandener Demo an; liefert ihre Zahl

    // Fenster
    HWND hwnd = nullptr;
    bool renderMode = false;
    bool uiDark = true;    // Farbschema und Maßstab des Fensters (für Dialoge)
    float uiScale = 1;
    void invalidate();
    void notify(const std::wstring& msg, bool error = false);
    std::wstring toast;
    bool toastError = false;
    ULONGLONG toastUntil = 0;

    // Befehle
    void addDemos();
    void rescan();
    void changeVideoDir();
    void changeCs2Dir();
    void addFolder();
    void chooseAccount(int x, int y);
    void deleteMatch(const std::string& checksum);
    void saveSettings();

private:
    fd::Career career_;
    bool careerDirty_ = true;
    std::string careerKey_;
    std::map<std::string, std::pair<bool, ULONGLONG>> demoCache_;
    Tools tools_ = Tools::Unknown;
    ULONGLONG toolsAt_ = 0;
};

// Seiten: zeichnen in den Inhaltsbereich (Breite w), liefern die Höhe
float drawDashboard(ui::Canvas& c, App& app, float w);
float drawMatches(ui::Canvas& c, App& app, float w);
float drawMatch(ui::Canvas& c, App& app, float w);
float drawStats(ui::Canvas& c, App& app, float w);
float drawHighlights(ui::Canvas& c, App& app, float w);
float drawSettings(ui::Canvas& c, App& app, float w);
float drawReplay(ui::Canvas& c, App& app, float w, float h);   // füllt die Höhe, kein Blättern

// gemeinsame Teile der Seiten
float pageTitle(ui::Canvas& c, float x, float y, float w, const std::wstring& title, const std::wstring& sub);
float filterRow(ui::Canvas& c, App& app, float x, float y, float w);   // Modus · Zeitraum · Map (page_dashboard.cpp)
std::wstring matchTitle(const fd::Match& m, const std::string& me = std::string());   // "Mirage · 13:11" (Team von me zuerst)
int64_t matchEpoch(const fd::Match& m);
std::vector<ui::ChartPoint> premierPoints(App& app, const fd::Career& c);
// Hinweis „mit älterer Version ausgewertet“ samt Knopf „Neu auswerten“ (nichts, wenn keins veraltet ist); liefert die Höhe
float reanalyzeNote(ui::Canvas& c, App& app, float x, float y, float w, const std::vector<const fd::Match*>& matches, const wchar_t* missing);

}  // namespace fdw
