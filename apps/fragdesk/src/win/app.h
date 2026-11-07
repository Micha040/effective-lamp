// FragDesk – Zustand der App (Seiten, Filter, Auswahl) und die Seiten selbst
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../core/stats.h"
#include "gfx.h"
#include "library.h"
#include "widgets.h"
#include "settings.h"
#include "steam.h"

namespace fdw {

class Recorder;

enum class Page { Dashboard, Matches, Match, Stats, Highlights, Settings, Count };

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

    // eigener Spieler
    std::string me;
    std::wstring meName;
    void detectMe();
    void applyFolders();

    // Navigation
    Page page = Page::Dashboard, backPage = Page::Matches;
    void go(Page p);
    void openMatch(const std::string& checksum, int tab = 0);
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

    // Fenster
    HWND hwnd = nullptr;
    bool renderMode = false;
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
};

// Seiten: zeichnen in den Inhaltsbereich (Breite w), liefern die Höhe
float drawDashboard(ui::Canvas& c, App& app, float w);
float drawMatches(ui::Canvas& c, App& app, float w);
float drawMatch(ui::Canvas& c, App& app, float w);
float drawStats(ui::Canvas& c, App& app, float w);
float drawHighlights(ui::Canvas& c, App& app, float w);
float drawSettings(ui::Canvas& c, App& app, float w);

// gemeinsame Teile der Seiten
float pageTitle(ui::Canvas& c, float x, float y, float w, const std::wstring& title, const std::wstring& sub);
float filterRow(ui::Canvas& c, App& app, float x, float y, float w);   // Modus · Zeitraum · Map (page_dashboard.cpp)
std::wstring matchTitle(const fd::Match& m, const std::string& me = std::string());   // "Mirage · 13:11" (Team von me zuerst)
int64_t matchEpoch(const fd::Match& m);
std::vector<ui::ChartPoint> premierPoints(App& app, const fd::Career& c);

}  // namespace fdw
