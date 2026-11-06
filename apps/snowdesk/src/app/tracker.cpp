// SnowDesk – Fenster beobachten (siehe tracker.h)
#include "tracker.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace app {

namespace {

WindowTracker* g_tracker = nullptr;

// Fenster der Windows-Oberfläche, die weder Schnee tragen noch den Desktop "verdecken"
const wchar_t* const kIgnoredClasses[] = {
    L"Progman",
    L"WorkerW",
    L"Shell_TrayWnd",
    L"Shell_SecondaryTrayWnd",
    L"Windows.UI.Core.CoreWindow",
    L"XamlExplorerHostIslandWindow",
    L"Xaml_WindowedPopupClass",
    L"#32768",
    L"tooltips_class32",
    L"SysShadow",
    L"NotifyIconOverflowWindow",
    L"TopLevelWindowForOverflowXamlIsland",
    L"ForegroundStaging",
    L"MultitaskingViewFrame",
    L"TaskListThumbnailWnd",
    L"Shell_InputSwitchTopLevelWindow",
    L"EdgeUiInputTopWndClass",
    L"EdgeUiInputWndClass",
    L"ThumbnailDeviceHelperWnd",
    L"Shell_LightDismissOverlay",
    L"ApplicationManager_ImmersiveShellWindow",
    L"DummyDWMListenerWindow",
    L"IME",
    L"MSCTFIME UI",
    L"SnowDeskLayer",
    L"SnowDeskHost",
};

LONG rectArea(const RECT& r) { return std::max<LONG>(0, r.right - r.left) * std::max<LONG>(0, r.bottom - r.top); }

// a minus c: bis zu vier Reststücke
void subtract(const RECT& a, const RECT& c, std::vector<RECT>& out) {
    RECT i;
    if (!IntersectRect(&i, &a, &c)) {
        out.push_back(a);
        return;
    }
    if (a.top < i.top) out.push_back(RECT{a.left, a.top, a.right, i.top});
    if (i.bottom < a.bottom) out.push_back(RECT{a.left, i.bottom, a.right, a.bottom});
    if (a.left < i.left) out.push_back(RECT{a.left, i.top, i.left, i.bottom});
    if (i.right < a.right) out.push_back(RECT{i.right, i.top, a.right, i.bottom});
}

}  // namespace

bool WindowTracker::start() {
    stop();
    g_tracker = this;
    const DWORD ranges[][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {0x8017 /*EVENT_OBJECT_CLOAKED*/, 0x8018 /*EVENT_OBJECT_UNCLOAKED*/},
    };
    for (const auto& r : ranges) {
        HWINEVENTHOOK h = SetWinEventHook(r[0], r[1], nullptr, hookProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (h) hooks_.push_back(h);
        else logLine(L"WinEvent-Hook %04lX-%04lX fehlgeschlagen", (unsigned long)r[0], (unsigned long)r[1]);
    }
    dirty_ = refreshAll_ = true;
    return !hooks_.empty();
}

void WindowTracker::stop() {
    for (HWINEVENTHOOK h : hooks_) UnhookWinEvent(h);
    hooks_.clear();
    dragging_ = false;
    if (g_tracker == this) g_tracker = nullptr;
}

void CALLBACK WindowTracker::hookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    WindowTracker* t = g_tracker;
    if (!t || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART:
            t->dragging_ = true;
            t->dragStart_ = GetTickCount64();
            t->dirty_ = true;
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            t->dragging_ = false;
            t->dirty_ = true;
            break;
        case 0x8017:
        case 0x8018:
            t->refreshAll_ = true;   // virtueller Desktop gewechselt, App ein-/ausgeblendet
            t->dirty_ = true;
            break;
        case EVENT_OBJECT_DESTROY:
            if (t->cache_.count(hwnd)) t->dirty_ = true;
            break;
        default:
            // nur Hauptfenster interessieren (Steuerelemente in Programmen feuern viel öfter)
            if (GetAncestor(hwnd, GA_ROOT) == hwnd) t->dirty_ = true;
            break;
    }
}

void WindowTracker::classify(HWND h, Entry& e) {
    e.ignored = false;
    e.platform = false;
    e.coverer = false;

    wchar_t cls[96] = {0};
    GetClassNameW(h, cls, 95);
    for (const wchar_t* c : kIgnoredClasses) {
        if (wcscmp(cls, c) == 0) {
            e.ignored = true;
            return;
        }
    }
    DWORD cloaked = 0;
    e.cloaked = SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &e.vis, sizeof(RECT))) || IsRectEmpty(&e.vis)) e.vis = e.wr;

    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (style & WS_CHILD) {
        e.ignored = true;
        return;
    }
    bool transparent = (ex & WS_EX_TRANSPARENT) != 0;            // durchklickbare Overlays (FPS-Anzeigen, Discord …)
    bool layered = (ex & WS_EX_LAYERED) != 0;
    bool invisibleLayer = false, opaqueLayer = false;
    if (layered) {
        COLORREF key = 0;
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(h, &key, &alpha, &flags)) {
            if ((flags & LWA_ALPHA) && alpha == 0) invisibleLayer = true;
            if ((flags & LWA_ALPHA) && alpha == 255 && !(flags & LWA_COLORKEY)) opaqueLayer = true;
        }
    }
    if (transparent || invisibleLayer) {
        e.ignored = true;
        return;
    }
    bool topmost = (ex & WS_EX_TOPMOST) != 0;
    bool tool = (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW);
    bool noActivate = (ex & WS_EX_NOACTIVATE) != 0;
    bool frame = (style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME);
    HWND owner = GetWindow(h, GW_OWNER);
    bool appLike = frame || (!owner && !tool) || (ex & WS_EX_APPWINDOW);
    LONG w = e.vis.right - e.vis.left, ht = e.vis.bottom - e.vis.top;
    bool bigEnough = w >= 40 && ht >= 20;

    // Schnee liegt auf normalen Fenstern und Dialogen, nicht auf Overlays, Menüs und "immer im Vordergrund"-Widgets
    e.platform = bigEnough && !topmost && !tool && !noActivate && appLike;
    // Verdecken: alles Undurchsichtige, was ein echtes Fenster ist (auch Vollbild-Spiele, die "topmost" sind)
    e.coverer = bigEnough && !tool && !noActivate && (!layered || opaqueLayer);
}

void WindowTracker::visit(HWND h) {
    if (!IsWindowVisible(h) || IsIconic(h)) return;
    if (ignore_) {
        for (HWND w : *ignore_) {
            if (w == h) return;
        }
    }
    RECT wr;
    if (!GetWindowRect(h, &wr) || wr.right - wr.left < 2 || wr.bottom - wr.top < 2) return;
    auto it = cache_.find(h);
    if (it == cache_.end() || refreshAll_ || !EqualRect(&it->second.wr, &wr)) {
        Entry e{};
        e.wr = wr;
        classify(h, e);
        it = cache_.insert_or_assign(h, e).first;
    }
    Entry& e = it->second;
    e.gen = gen_;
    if (e.ignored || e.cloaked) return;
    if (!e.platform && !e.coverer) return;
    list_.push_back(TrackedWindow{h, e.vis, e.platform, e.coverer, IsZoomed(h) != FALSE});
}

BOOL CALLBACK WindowTracker::enumProc(HWND h, LPARAM lp) {
    reinterpret_cast<WindowTracker*>(lp)->visit(h);
    return TRUE;
}

void WindowTracker::scan(const std::vector<HWND>& ignore) {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    ++gen_;
    list_.clear();
    ignore_ = &ignore;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(this));
    ignore_ = nullptr;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.gen != gen_) it = cache_.erase(it);
        else ++it;
    }
    refreshAll_ = false;
    dirty_ = false;
    QueryPerformanceCounter(&b);
    lastScanUs_ = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;
}

void WindowTracker::platformsFor(const MonitorInfo& m, std::vector<snow::PlatformIn>& out) const {
    out.clear();
    float floorTop = (float)(m.work.bottom - m.rect.top);
    for (const TrackedWindow& t : list_) {
        if (!t.platform) continue;
        const RECT& v = t.vis;
        if (v.right <= m.rect.left || v.left >= m.rect.right || v.bottom <= m.rect.top || v.top >= m.rect.bottom) continue;
        snow::PlatformIn p;
        p.id = (uint64_t)(uintptr_t)t.hwnd;
        p.left = (float)(std::max(v.left, m.rect.left) - m.rect.left);
        p.right = (float)(std::min(v.right, m.rect.right) - m.rect.left);
        p.top = (float)(std::max(v.top, m.rect.top) - m.rect.top);
        if (p.top >= floorTop - 1.0f) continue;   // Oberkante hinter der Taskleiste
        p.corner = t.maximized ? 0.0f : 8.0f * m.scale;
        p.floor = false;
        out.push_back(p);
    }
    snow::PlatformIn f;
    f.id = 1;
    f.left = 0.0f;
    f.right = (float)m.width();
    f.top = floorTop;
    f.floor = true;
    out.push_back(f);
}

bool WindowTracker::covered(const MonitorInfo& m) const {
    std::vector<RECT> rest{m.work}, next;
    for (const TrackedWindow& t : list_) {
        if (!t.coverer) continue;
        RECT c = t.vis;
        InflateRect(&c, 2, 2);   // kleine Spalten zwischen eingerasteten Fenstern zählen nicht
        next.clear();
        for (const RECT& r : rest) subtract(r, c, next);
        rest.swap(next);
        if (rest.empty()) return true;
        if (rest.size() > 256) return false;
    }
    long long left = 0;
    for (const RECT& r : rest) left += rectArea(r);
    return left * 1000 < (long long)rectArea(m.work) * 2;   // weniger als 0,2 % sichtbar
}

std::wstring WindowTracker::describeWindows() const {
    std::wstring s;
    for (const TrackedWindow& t : list_) {
        wchar_t cls[96] = {0};
        GetClassNameW(t.hwnd, cls, 95);   // bewusst ohne Fenstertitel (könnten private Dokumentnamen enthalten)
        wchar_t line[400];
        _snwprintf(line, 399, L"  %p [%ld,%ld-%ld,%ld] %ls%ls%ls(%ls)\r\n", (void*)t.hwnd, (long)t.vis.left, (long)t.vis.top,
                   (long)t.vis.right, (long)t.vis.bottom, t.platform ? L"Schnee " : L"", t.coverer ? L"verdeckt " : L"",
                   t.maximized ? L"maximiert " : L"", cls);
        line[399] = 0;
        s += line;
    }
    return s;
}

}  // namespace app
