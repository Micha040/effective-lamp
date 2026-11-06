// SnowDesk – Monitore
#include "monitors.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cstdio>

namespace app {

bool MonitorInfo::sameLayout(const MonitorInfo& o) const {
    return device == o.device && EqualRect(&rect, &o.rect) && std::abs(scale - o.scale) < 0.001f;
}

namespace {
struct EnumCtx {
    std::vector<MonitorInfo>* list;
    bool queryHz;
};
BOOL CALLBACK enumProc(HMONITOR mon, HDC, LPRECT, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumCtx*>(param);
    auto* list = ctx->list;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    MonitorInfo m;
    m.handle = mon;
    m.device = mi.szDevice;
    m.rect = mi.rcMonitor;
    m.work = mi.rcWork;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx >= 48) m.scale = dx / 96.0f;
    if (ctx->queryHz) {
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) m.hz = (int)dm.dmDisplayFrequency;
    }
    if (m.width() > 0 && m.height() > 0) list->push_back(m);
    return TRUE;
}
}  // namespace

std::vector<MonitorInfo> enumMonitors(bool queryHz) {
    std::vector<MonitorInfo> list;
    EnumCtx ctx{&list, queryHz};
    EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&ctx));
    // stabile Reihenfolge (für Vergleiche nach Änderungen)
    std::sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    return list;
}

bool sameMonitors(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!a[i].sameLayout(b[i]) || !EqualRect(&a[i].work, &b[i].work)) return false;
    }
    return true;
}

std::wstring describe(const MonitorInfo& m) {
    wchar_t buf[256];
    _snwprintf(buf, 255, L"%ls %ldx%ld @ (%ld,%ld), Arbeitsbereich bis y=%ld, %.0f %%, %d Hz%ls", m.device.c_str(), (long)m.width(),
               (long)m.height(), (long)m.rect.left, (long)m.rect.top, (long)m.work.bottom, (double)(m.scale * 100.0f), m.hz,
               m.primary ? L", Hauptmonitor" : L"");
    buf[255] = 0;
    return buf;
}

}  // namespace app
