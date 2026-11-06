// DeskCats – Einstellungen (HKCU\Software\DeskCats)
#pragma once
#include "common.h"

namespace app {

struct Settings {
    bool fatos = true, kosta = true;
    int size = 3;                    // Pixel-Vergrößerung bei 100 % Windows-Skalierung (2..5)
    int activity = 1;                // 0 ruhig, 1 normal, 2 verspielt
    bool onWindows = true;           // auf Fenster klettern
    bool onTaskbar = true;           // auf der Taskleiste laufen
    bool allMonitors = true;
    bool visible = true;             // nicht über das Menü ausgeblendet
    std::vector<std::wstring> games; // während diese Programme laufen: ausblenden und schlafen

    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

Settings loadSettings();
void saveSettings(const Settings& s);
std::vector<std::wstring> defaultGames();
std::wstring joinGames(const std::vector<std::wstring>& g);
std::vector<std::wstring> splitGames(const std::wstring& s);

// Autostart (HKCU\...\Run)
bool autostartEnabled();
void setAutostart(bool on);

// Kleine Zustandswerte für das Kontextmenü (Explorer liest sie beim Öffnen)
void publishMenuState(bool visible, bool sleeping);

}  // namespace app
