// DeskCats – wann pausieren? Spiele (Prozessliste, ohne Zugriff auf fremde Prozesse), exklusives Vollbild,
// Präsentationsmodus, gesperrter Bildschirm, Bildschirm aus.
#pragma once
#include "common.h"

namespace app {

class PauseWatch {
public:
    void init(HWND host);
    void shutdown();
    void onSessionChange(WPARAM code);
    void onPowerBroadcast(WPARAM wp, LPARAM lp);
    bool locked() const { return locked_; }
    bool displayOff() const { return displayOff_; }

    // Läuft eines der Spiele? (Name der .exe zurück). Nutzt nur die Prozessliste (Toolhelp), öffnet keine Prozesse.
    bool gameRunning(const std::vector<std::wstring>& games, std::wstring* which);
    // Exklusives Vollbild (D3D), Präsentationsmodus oder Sitzung nicht aktiv
    bool systemBusy(std::wstring* why);

private:
    HWND host_ = nullptr;
    HPOWERNOTIFY power_ = nullptr;
    bool sessionRegistered_ = false;
    bool locked_ = false, displayOff_ = false;
};

}  // namespace app
