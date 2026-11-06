// DeskCats – Monitore (physische Pixel, virtuelle Bildschirmkoordinaten)
#pragma once
#include "common.h"

namespace app {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::wstring device;      // \\.\DISPLAY1
    RECT rect{};              // ganzer Monitor
    RECT work{};              // ohne Taskleiste
    float scale = 1.0f;       // DPI / 96
    int hz = 60;
    bool primary = false;

    int width() const { return rect.right - rect.left; }
    int height() const { return rect.bottom - rect.top; }
    bool sameLayout(const MonitorInfo& o) const;   // gleiche Lage/Größe/Skalierung 
};

std::vector<MonitorInfo> enumMonitors(bool queryHz = true);
bool sameMonitors(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b);   // Lage, Arbeitsbereich, Skalierung
std::wstring describe(const MonitorInfo& m);

}  // namespace app
