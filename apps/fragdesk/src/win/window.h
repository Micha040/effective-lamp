// FragDesk – Hauptfenster und Bildmodus
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "app.h"

namespace fdw {

struct Options {
    std::wstring theme;                 // "", dark, light
    // Bildmodus (für Tests und die README): Seite ohne Fenster als PNG zeichnen
    std::wstring renderPath;
    Page page = Page::Dashboard;
    int tab = 0, matchIndex = 0, selectCount = 0;
    int renderWidth = 1440, renderHeight = 900, renderScale = 100;
    float renderScroll = 0;
    std::wstring me;
    std::vector<std::wstring> demos;    // beim Start übergebene Demos (z. B. „Öffnen mit“)
};

int runApp(HINSTANCE inst, const Options& opt);
void releaseInstanceLock();   // main.cpp: "nur eine Instanz"-Sperre freigeben

}  // namespace fdw
