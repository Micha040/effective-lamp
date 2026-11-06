// DeskCats – echte Fenster als Welt (siehe scene.h)
#include "scene.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace app {

namespace {

Scene* g_scene = nullptr;

// Fenster der Windows-Oberfläche: weder Fläche noch Abdeckung
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
    L"DeskCatsSprite",
    L"DeskCatsHost",
    L"SnowDeskLayer",
    L"SnowDeskHost",
};

bool isDesktopClass(const wchar_t* cls) { return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0; }

}  // namespace

bool Scene::start() {
    stop();
    g_scene = this;
    const DWORD ranges[][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {0x8017 /*EVENT_OBJECT_CLOAKED*/, 0x8018 /*EVENT_OBJECT_UNCLOAKED*/},
        {EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER},
    };
    for (const auto& r : ranges) {
        HWINEVENTHOOK h = SetWinEventHook(r[0], r[1], nullptr, hookProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (h) hooks_.push_back(h);
        else logLine(L"WinEvent-Hook %04lX-%04lX fehlgeschlagen", (unsigned long)r[0], (unsigned long)r[1]);
    }
    dirty_ = refreshAll_ = zChanged_ = true;
    return !hooks_.empty();
}

void Scene::stop() {
    for (HWINEVENTHOOK h : hooks_) UnhookWinEvent(h);
    for (HWINEVENTHOOK h : watchHooks_) UnhookWinEvent(h);
    hooks_.clear();
    watchHooks_.clear();
    watchPids_.clear();
    watched_.clear();
    if (g_scene == this) g_scene = nullptr;
}

bool Scene::dragging() const {
    // Sicherheitsnetz: verpasstes "Ziehen beendet" nach 20 s vergessen
    return dragWnd_ != nullptr && GetTickCount() - dragSince_ < 20000;
}

void Scene::watch(const std::vector<HWND>& windows) {
    std::vector<DWORD> pids;
    for (HWND h : windows) {
        DWORD pid = 0;
        if (h && GetWindowThreadProcessId(h, &pid) && pid && std::find(pids.begin(), pids.end(), pid) == pids.end()) pids.push_back(pid);
    }
    watched_ = windows;
    std::sort(pids.begin(), pids.end());
    if (pids == watchPids_) return;
    for (HWINEVENTHOOK h : watchHooks_) UnhookWinEvent(h);
    watchHooks_.clear();
    watchPids_ = pids;
    for (DWORD pid : pids) {
        HWINEVENTHOOK h = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, hookProc, pid, 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (h) watchHooks_.push_back(h);
    }
}

void CALLBACK Scene::hookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    Scene* s = g_scene;
    if (!s || !hwnd) return;
    if (event == EVENT_OBJECT_REORDER) {   // Stapelreihenfolge der Hauptfenster geändert (z. B. ohne Aktivierung nach vorne)
        if (hwnd == GetDesktopWindow()) {
            s->zChanged_ = true;
            s->notify();
        }
        return;
    }
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    bool changed = false;
    switch (event) {
        case EVENT_OBJECT_LOCATIONCHANGE:
            // nur die beobachteten Fenster (Flächen der Katzen) interessieren
            changed = std::find(s->watched_.begin(), s->watched_.end(), hwnd) != s->watched_.end();
            if (changed) s->dirty_ = true;
            break;
        case EVENT_SYSTEM_MOVESIZESTART:
            s->dragWnd_ = hwnd;
            s->dragSince_ = GetTickCount();
            s->dirty_ = s->zChanged_ = changed = true;
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            s->dragWnd_ = nullptr;
            s->dirty_ = s->zChanged_ = changed = true;
            break;
        case EVENT_SYSTEM_FOREGROUND:
            s->dirty_ = s->zChanged_ = changed = true;
            break;
        case 0x8017:
        case 0x8018:
            s->refreshAll_ = true;   // virtueller Desktop gewechselt
            s->dirty_ = s->zChanged_ = changed = true;
            break;
        case EVENT_OBJECT_DESTROY:
            changed = s->cache_.count(hwnd) != 0;
            if (changed) s->dirty_ = true;
            break;
        default: {
            // Ein-/Ausblenden, Minimieren: nur echte Fenster (Tooltips, Menüs, Popups ändern nichts an den Flächen)
            if (GetAncestor(hwnd, GA_ROOT) != hwnd) return;
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if ((ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) && !s->cache_.count(hwnd)) return;
            wchar_t cls[48] = {0};
            GetClassNameW(hwnd, cls, 47);
            for (const wchar_t* c : kIgnoredClasses)
                if (wcscmp(cls, c) == 0) return;
            s->dirty_ = s->zChanged_ = changed = true;
            break;
        }
    }
    if (changed) s->notify();
}

void Scene::classify(HWND h, Entry& e) {
    e.ignored = e.platform = e.coverer = e.topmost = false;
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
    bool transparent = (ex & WS_EX_TRANSPARENT) != 0;   // durchklickbare Overlays
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
    e.topmost = (ex & WS_EX_TOPMOST) != 0;
    bool tool = (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW);
    bool noActivate = (ex & WS_EX_NOACTIVATE) != 0;
    bool frame = (style & WS_CAPTION) == WS_CAPTION || (style & WS_THICKFRAME);
    HWND owner = GetWindow(h, GW_OWNER);
    bool appLike = frame || (!owner && !tool) || (ex & WS_EX_APPWINDOW);
    LONG w = e.vis.right - e.vis.left, ht = e.vis.bottom - e.vis.top;
    // Katzen laufen auf normalen Fenstern und größeren Dialogen, nicht auf Menüs, Overlays und "immer oben"-Widgets
    e.platform = w >= 160 && ht >= 60 && !e.topmost && !tool && !noActivate && appLike;
    e.coverer = w >= 40 && ht >= 20 && !tool && !noActivate && (!layered || opaqueLayer);
}

void Scene::visit(HWND h) {
    if (!IsWindowVisible(h)) return;
    wchar_t cls[32] = {0};
    if (!desktopTop_) {
        GetClassNameW(h, cls, 31);
        if (isDesktopClass(cls)) desktopTop_ = h;
    }
    if (IsIconic(h)) return;
    if (own_)
        for (HWND w : *own_)
            if (w == h) return;
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
    list_.push_back(Item{h, e.vis, e.platform, e.coverer, IsZoomed(h) != FALSE});
}

BOOL CALLBACK Scene::enumProc(HWND h, LPARAM lp) {
    reinterpret_cast<Scene*>(lp)->visit(h);
    return TRUE;
}

void Scene::build(const std::vector<MonitorInfo>& mons, const std::vector<HWND>& own, const Settings& set, cat::World& out) {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    ++gen_;
    list_.clear();
    desktopTop_ = nullptr;
    own_ = &own;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(this));
    own_ = nullptr;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.gen != gen_) it = cache_.erase(it);
        else ++it;
    }
    refreshAll_ = false;
    dirty_ = false;

    out.plats.clear();
    out.monitors.clear();
    platHwnd_.clear();
    // Monitore (Vergrößerung je Monitor: Wunschgröße mal Windows-Skalierung, ganzzahlig für scharfe Pixel)
    std::vector<int> monIdx;   // Index in mons -> Index in out.monitors
    for (size_t i = 0; i < mons.size(); ++i) {
        const MonitorInfo& m = mons[i];
        if (!set.allMonitors && !m.primary) {
            monIdx.push_back(-1);
            continue;
        }
        int sc = std::max(1, std::min(10, (int)std::lround(set.size * m.scale)));
        monIdx.push_back((int)out.monitors.size());
        out.monitors.push_back(cat::Monitor{(int)m.rect.left, (int)m.rect.top, (int)m.rect.right, (int)m.rect.bottom, sc});
    }
    auto monitorOf = [&](int x, int y) {
        for (size_t i = 0; i < mons.size(); ++i)
            if (x >= mons[i].rect.left && x < mons[i].rect.right && y >= mons[i].rect.top && y < mons[i].rect.bottom) return (int)i;
        return -1;
    };
    // Taskleisten (unten) bzw. Boden je Monitor
    std::vector<int> groundY(mons.size(), 0);
    for (size_t i = 0; i < mons.size(); ++i) groundY[i] = mons[i].work.bottom;
    std::vector<char> hasTaskbar(mons.size(), 0);
    auto addTaskbar = [&](HWND tb) {
        if (!tb || !IsWindowVisible(tb)) return;
        RECT r;
        if (!GetWindowRect(tb, &r)) return;
        int mi = monitorOf((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        if (mi < 0 || monIdx[mi] < 0) return;
        const MonitorInfo& m = mons[mi];
        bool bottom = r.bottom >= m.rect.bottom - 2 && r.top > (m.rect.top + m.rect.bottom) / 2;
        bool shown = r.top < m.rect.bottom - 6;   // automatisch ausgeblendet: nur ein Streifen
        if (!bottom || !shown || !set.onTaskbar) return;
        cat::Platform p;
        p.id = kTaskbarBase + (uint64_t)mi;
        p.kind = cat::PlatKind::Taskbar;
        p.x0 = std::max(r.left, m.rect.left);
        p.x1 = std::min(r.right, m.rect.right);
        p.y = r.top;
        p.monitor = monIdx[mi];
        p.z = -1;
        p.visible.push_back({p.x0, p.x1});
        out.plats.push_back(p);
        platHwnd_[p.id] = tb;
        hasTaskbar[mi] = 1;
        groundY[mi] = r.top;
    };
    addTaskbar(FindWindowW(L"Shell_TrayWnd", nullptr));
    for (HWND h = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); h; h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr))
        addTaskbar(h);
    for (size_t i = 0; i < mons.size(); ++i) {
        if (monIdx[i] < 0 || hasTaskbar[i]) continue;
        cat::Platform p;
        p.id = kFloorBase + (uint64_t)i;
        p.kind = cat::PlatKind::Floor;
        p.x0 = mons[i].rect.left;
        p.x1 = mons[i].rect.right;
        p.y = mons[i].work.bottom;
        p.monitor = monIdx[i];
        p.z = -1;
        p.visible.push_back({p.x0, p.x1});
        out.plats.push_back(p);
        groundY[i] = p.y;
    }
    // Fenster-Oberkanten
    if (set.onWindows) {
        for (size_t zi = 0; zi < list_.size(); ++zi) {
            const Item& it = list_[zi];
            if (!it.platform || it.maximized) continue;
            const RECT& v = it.vis;
            int mi = monitorOf((v.left + v.right) / 2, v.top);
            if (mi < 0) mi = monitorOf(std::max(v.left, 0L), v.top);
            if (mi < 0 || monIdx[mi] < 0) continue;
            const MonitorInfo& m = mons[mi];
            int sc = out.monitors[monIdx[mi]].scale;
            // genug Platz über der Kante für eine Katze, nicht hinter der Taskleiste, nicht Vollbild
            if (v.top < m.rect.top + 12 * sc || v.top >= groundY[mi] - 8) continue;
            if (v.left <= m.rect.left && v.right >= m.rect.right && v.top <= m.rect.top) continue;
            cat::Platform p;
            p.id = (uint64_t)(uintptr_t)it.hwnd;
            p.kind = cat::PlatKind::Window;
            p.x0 = std::max(v.left, m.rect.left);
            p.x1 = std::min(v.right, m.rect.right);
            if (p.x1 - p.x0 < 40 * sc / 3 + 40) continue;
            p.y = v.top;
            p.monitor = monIdx[mi];
            p.z = (int)zi;
            // Sichtbare Stücke: was Fenster davor (direkt über der Kante) nicht verdecken
            std::vector<cat::Span> spans{{p.x0, p.x1}}, next;
            const int lineY = p.y - 3;
            for (size_t zj = 0; zj < zi && !spans.empty(); ++zj) {
                const Item& f = list_[zj];
                if (!f.coverer || !(f.vis.top <= lineY && f.vis.bottom > lineY)) continue;
                next.clear();
                for (const cat::Span& s : spans) {
                    if (f.vis.right <= s.x0 || f.vis.left >= s.x1) next.push_back(s);
                    else {
                        if (f.vis.left > s.x0) next.push_back({s.x0, (int)f.vis.left});
                        if (f.vis.right < s.x1) next.push_back({(int)f.vis.right, s.x1});
                    }
                }
                spans.swap(next);
            }
            p.visible = spans;
            out.plats.push_back(p);
            platHwnd_[p.id] = it.hwnd;
        }
    }
    QueryPerformanceCounter(&b);
    lastUs_ = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;
}

HWND Scene::hwndOf(uint64_t platform) const {
    auto it = platHwnd_.find(platform);
    return it == platHwnd_.end() ? nullptr : it->second;
}

int Scene::fullscreenMonitor(const std::vector<MonitorInfo>& mons) const {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetDesktopWindow() || fg == GetShellWindow()) return -1;   // Desktop selbst ist kein Vollbild-Programm
    wchar_t cls[64] = {0};
    GetClassNameW(fg, cls, 63);
    if (isDesktopClass(cls) || wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"#32769") == 0) return -1;
    RECT r;
    if (!GetWindowRect(fg, &r)) return -1;
    for (size_t i = 0; i < mons.size(); ++i) {
        const RECT& m = mons[i].rect;
        if (r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom) return (int)i;
    }
    return -1;
}

std::wstring Scene::describe() const {
    std::wstring s;
    for (const Item& t : list_) {
        wchar_t cls[96] = {0};
        GetClassNameW(t.hwnd, cls, 95);   // bewusst ohne Fenstertitel
        wchar_t line[300];
        _snwprintf(line, 299, L"  %p [%ld,%ld-%ld,%ld] %ls%ls%ls(%ls)\r\n", (void*)t.hwnd, (long)t.vis.left, (long)t.vis.top, (long)t.vis.right,
                   (long)t.vis.bottom, t.platform ? L"Fläche " : L"", t.coverer ? L"verdeckt " : L"", t.maximized ? L"maximiert " : L"", cls);
        line[299] = 0;
        s += line;
    }
    return s;
}

}  // namespace app
