// SnowDesk – Desktop-Ebene (siehe desktop.h)
#include "desktop.h"

#include <cstdio>

namespace app {

namespace {

const wchar_t* kLayerClass = L"SnowDeskLayer";
int g_repairLogs = 0;   // Reparatur-Meldungen nur ein paar Mal ins Protokoll (z. B. Tauziehen mit Wallpaper Engine)
HWND g_host = nullptr;
bool g_detaching = false;
bool g_registered = false;

LRESULT CALLBACK layerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            // Eigener Fensterinhalt bleibt schwarz = per Farbschlüssel durchsichtig; der Schnee kommt per DirectComposition
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            if (dc) FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
            EndPaint(h, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            if (!g_detaching && g_host) PostMessageW(g_host, WM_APP_LAYERLOST, 0, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void registerClass() {
    if (g_registered) return;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = layerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kLayerClass;
    g_registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

std::wstring classOf(HWND h) {
    wchar_t buf[128] = {0};
    if (h) GetClassNameW(h, buf, 127);
    return buf;
}

std::wstring hwndText(HWND h) {
    wchar_t buf[200];
    if (!h) return L"-";
    RECT r{};
    GetWindowRect(h, &r);
    _snwprintf(buf, 199, L"%p %ls [%ld,%ld-%ld,%ld] style=%08lX ex=%08lX", (void*)h, classOf(h).c_str(), (long)r.left, (long)r.top, (long)r.right,
               (long)r.bottom, (unsigned long)GetWindowLongPtrW(h, GWL_STYLE), (unsigned long)GetWindowLongPtrW(h, GWL_EXSTYLE));
    buf[199] = 0;
    return buf;
}

struct FindClassic {
    HWND workerW = nullptr;
};

BOOL CALLBACK findClassicProc(HWND top, LPARAM lp) {
    auto* f = reinterpret_cast<FindClassic*>(lp);
    if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
        // Das WorkerW-Fenster direkt nach dem Fenster mit den Symbolen zeichnet den Hintergrund
        f->workerW = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    }
    return TRUE;
}

// Das Hintergrund-WorkerW unter Progman (24H2+): das sichtbare mit der größten Fläche – es kann kleine Hilfsfenster
// gleichen Namens geben
HWND wallpaperWorkerW(HWND progman) {
    HWND best = nullptr;
    long long bestArea = -1;
    for (HWND c = FindWindowExW(progman, nullptr, L"WorkerW", nullptr); c; c = FindWindowExW(progman, c, L"WorkerW", nullptr)) {
        if (!IsWindowVisible(c)) continue;
        RECT r{};
        GetWindowRect(c, &r);
        long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
        if (area > bestArea) {
            bestArea = area;
            best = c;
        }
    }
    return best;
}

// Progman bitten, das Hintergrund-Fenster anzulegen (wie Wallpaper Engine/Lively) – nur wenn es noch fehlt,
// weil ein erneutes Senden die Ebenen umbauen kann
void requestWorkerW(HWND progman) {
    DWORD_PTR res = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &res);
}

const UINT kKeepPos = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
const UINT kKeepPosAsync = kKeepPos | SWP_ASYNCWINDOWPOS;   // für Explorers Fenster: nie auf Explorer warten

}  // namespace

const wchar_t* layerName(int mode) {
    switch (mode) {
        case LayerBehind: return L"hinter den Symbolen";
        case LayerWallpaper: return L"im Hintergrundbild-Fenster";
        case LayerFront: return L"vor den Symbolen";
        case LayerClassic: return L"hinter den Symbolen (klassisch)";
        default: return L"keine";
    }
}

bool DesktopLayer::owns(HWND h) const {
    for (HWND w : wins_) {
        if (w == h) return true;
    }
    return false;
}

HWND DesktopLayer::createLayerWindow(bool layered, bool noRedirection) {
    registerClass();
    // WS_EX_NOPARENTNOTIFY: beim Anlegen/Zerstören keine (blockierende) Nachricht an Explorers Progman
    DWORD ex = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY;
    if (layered) ex |= WS_EX_LAYERED;
    if (noRedirection) ex |= WS_EX_NOREDIRECTIONBITMAP;
    HWND w = CreateWindowExW(ex, kLayerClass, L"SnowDesk", WS_POPUP | WS_DISABLED, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!w) {
        logLine(L"Ebenen-Fenster konnte nicht angelegt werden (%lu)", GetLastError());
        return nullptr;
    }
    // Geschichtet mit voller Deckkraft (schnell) + Schwarz als Farbschlüssel: der eigene, leere Fensterinhalt ist
    // garantiert durchsichtig, egal ob Windows dafür eine Umleitungsfläche anlegt oder nicht.
    if (layered && !SetLayeredWindowAttributes(w, RGB(0, 0, 0), 255, LWA_ALPHA | LWA_COLORKEY))
        logLine(L"SetLayeredWindowAttributes fehlgeschlagen (%lu)", GetLastError());
    wins_.push_back(w);
    return w;
}

void DesktopLayer::place(HWND w, const MonitorInfo& m, HWND parent, HWND insertAfter) {
    if (parent) {
        // erst zum Kindfenster machen, dann umhängen (Reihenfolge laut Microsoft wichtig)
        SetWindowLongPtrW(w, GWL_STYLE, WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS);
        if (!SetParent(w, parent)) logLine(L"SetParent fehlgeschlagen (%lu)", GetLastError());
        POINT pt{m.rect.left, m.rect.top};
        MapWindowPoints(HWND_DESKTOP, parent, &pt, 1);
        SetWindowPos(w, insertAfter, pt.x, pt.y, m.width(), m.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    } else {
        SetWindowPos(w, insertAfter, m.rect.left, m.rect.top, m.width(), m.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

bool DesktopLayer::attachBehind(const std::vector<MonitorInfo>& mons, bool wallpaperChild) {
    progman_ = FindWindowW(L"Progman", nullptr);
    if (!progman_) return false;
    defView_ = FindWindowExW(progman_, nullptr, L"SHELLDLL_DefView", nullptr);
    bool raised = defView_ && (GetWindowLongPtrW(progman_, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP);
    if (!raised) return false;
    workerW_ = wallpaperWorkerW(progman_);
    if (!workerW_) {
        requestWorkerW(progman_);
        workerW_ = wallpaperWorkerW(progman_);
    }
    if (wallpaperChild) {
        if (!workerW_) return false;
        parent_ = workerW_;
        HWND after = HWND_TOP;
        for (const MonitorInfo& m : mons) {
            HWND w = createLayerWindow(true, true);
            if (!w) return false;
            place(w, m, workerW_, after);
            after = w;
        }
    } else {
        parent_ = progman_;
        HWND after = defView_;
        for (const MonitorInfo& m : mons) {
            HWND w = createLayerWindow(true, true);
            if (!w) return false;
            place(w, m, progman_, after);
            after = w;
        }
        // Das Hintergrund-Fenster muss ganz unten bleiben
        HWND first = GetWindow(progman_, GW_CHILD);
        if (workerW_ && first && GetWindow(first, GW_HWNDLAST) != workerW_) SetWindowPos(workerW_, HWND_BOTTOM, 0, 0, 0, 0, kKeepPosAsync);
    }
    for (HWND w : wins_) {
        if (GetParent(w) != parent_) return false;
    }
    return true;
}

bool DesktopLayer::attachClassic(const std::vector<MonitorInfo>& mons) {
    progman_ = FindWindowW(L"Progman", nullptr);
    if (!progman_) return false;
    FindClassic f;
    EnumWindows(findClassicProc, reinterpret_cast<LPARAM>(&f));
    if (!f.workerW) {
        requestWorkerW(progman_);
        EnumWindows(findClassicProc, reinterpret_cast<LPARAM>(&f));
    }
    if (!f.workerW) return false;
    workerW_ = f.workerW;
    parent_ = workerW_;
    HWND after = HWND_TOP;
    for (const MonitorInfo& m : mons) {
        HWND w = createLayerWindow(true, true);
        if (!w) return false;
        place(w, m, workerW_, after);
        after = w;
    }
    for (HWND w : wins_) {
        if (GetParent(w) != parent_) return false;
    }
    return true;
}

bool DesktopLayer::attachFront(const std::vector<MonitorInfo>& mons) {
    progman_ = FindWindowW(L"Progman", nullptr);
    parent_ = nullptr;
    for (const MonitorInfo& m : mons) {
        HWND w = createLayerWindow(true, true);
        if (!w) return false;
        place(w, m, nullptr, HWND_BOTTOM);
    }
    // direkt über den Desktop legen (unter alle normalen Fenster)
    HWND above = progman_ ? GetWindow(progman_, GW_HWNDPREV) : nullptr;
    while (above && owns(above)) above = GetWindow(above, GW_HWNDPREV);
    HWND after = above ? above : HWND_TOP;
    for (HWND w : wins_) {
        SetWindowPos(w, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        after = w;
    }
    return true;
}

bool DesktopLayer::attach(int wanted, const std::vector<MonitorInfo>& mons, HWND hostWnd) {
    detach();
    host_ = hostWnd;
    g_host = hostWnd;
    mons_ = mons;
    if (mons.empty()) return false;
    // Direkt nach der Anmeldung ist Explorer evtl. noch nicht fertig: dann später erneut versuchen,
    // statt dauerhaft auf "vor den Symbolen" auszuweichen.
    if (wanted != LayerFront) {
        HWND pm = FindWindowW(L"Progman", nullptr);
        FindClassic probe;
        if (pm && !FindWindowExW(pm, nullptr, L"SHELLDLL_DefView", nullptr)) EnumWindows(findClassicProc, reinterpret_cast<LPARAM>(&probe));
        bool ready = pm && (FindWindowExW(pm, nullptr, L"SHELLDLL_DefView", nullptr) || probe.workerW);
        if (!ready) {
            logLine(L"Desktop noch nicht bereit (Explorer startet?)");
            return false;
        }
    }

    int order[4];
    int n = 0;
    if (wanted == LayerFront) {
        order[n++] = LayerFront;
    } else if (wanted == LayerWallpaper) {
        order[n++] = LayerWallpaper;
        order[n++] = LayerBehind;
        order[n++] = LayerClassic;
        order[n++] = LayerFront;
    } else {
        order[n++] = LayerBehind;
        order[n++] = LayerWallpaper;
        order[n++] = LayerClassic;
        order[n++] = LayerFront;
    }
    for (int k = 0; k < n; ++k) {
        bool ok = false;
        switch (order[k]) {
            case LayerBehind: ok = attachBehind(mons, false); break;
            case LayerWallpaper: ok = attachBehind(mons, true); break;
            case LayerClassic: ok = attachClassic(mons); break;
            case LayerFront: ok = attachFront(mons); break;
            default: break;
        }
        if (ok) {
            mode_ = order[k];
            logLine(L"Desktop-Ebene: %ls (gewünscht: %ls)", layerName(mode_), layerName(wanted));
            return true;
        }
        logLine(L"Desktop-Ebene %ls nicht möglich, nächster Versuch", layerName(order[k]));
        detach();
        host_ = hostWnd;
        g_host = hostWnd;
        mons_ = mons;
    }
    return false;
}

void DesktopLayer::detach() {
    g_detaching = true;
    for (HWND w : wins_) {
        if (IsWindow(w)) DestroyWindow(w);
    }
    g_detaching = false;
    wins_.clear();
    mode_ = LayerNone;
    progman_ = defView_ = workerW_ = parent_ = nullptr;
}

bool DesktopLayer::check() {
    if (mode_ == LayerNone || wins_.empty()) return false;
    for (HWND w : wins_) {
        if (!IsWindow(w)) return false;
    }
    const UINT keep = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    switch (mode_) {
        case LayerBehind: {
            if (!IsWindow(progman_) || !IsWindow(defView_) || GetParent(defView_) != progman_) return false;
            if (GetParent(wins_[0]) != progman_) return false;
            // unsere Fenster direkt unter den Symbolen (Wallpaper Engine o. Ä. könnte sich dazwischen geschoben haben)
            HWND cur = GetWindow(defView_, GW_HWNDNEXT);
            bool ok = true;
            for (HWND w : wins_) {
                if (cur != w) {
                    ok = false;
                    break;
                }
                cur = GetWindow(cur, GW_HWNDNEXT);
            }
            if (!ok) {
                HWND after = defView_;
                for (HWND w : wins_) {
                    SetWindowPos(w, after, 0, 0, 0, 0, keep);
                    after = w;
                }
                if (g_repairLogs++ < 5) logLine(L"Z-Reihenfolge unter den Symbolen repariert");
            }
            HWND first = GetWindow(progman_, GW_CHILD);
            if (workerW_ && IsWindow(workerW_) && first && GetWindow(first, GW_HWNDLAST) != workerW_)
                SetWindowPos(workerW_, HWND_BOTTOM, 0, 0, 0, 0, kKeepPosAsync);
            return true;
        }
        case LayerWallpaper:
        case LayerClassic: {
            if (!IsWindow(workerW_) || GetParent(wins_[0]) != workerW_) return false;
            if (GetWindow(workerW_, GW_CHILD) != wins_[0]) {
                HWND after = HWND_TOP;
                for (HWND w : wins_) {
                    SetWindowPos(w, after, 0, 0, 0, 0, keep);
                    after = w;
                }
                if (g_repairLogs++ < 5) logLine(L"Z-Reihenfolge im Hintergrund-Fenster repariert");
            }
            return true;
        }
        case LayerFront: {
            if (!progman_ || !IsWindow(progman_)) {
                progman_ = FindWindowW(L"Progman", nullptr);
                if (!progman_) return true;
            }
            // liegt unser Fenster unter dem Desktop, wäre es unsichtbar
            bool below = false;
            for (HWND cur = GetWindow(progman_, GW_HWNDNEXT); cur; cur = GetWindow(cur, GW_HWNDNEXT)) {
                if (owns(cur)) {
                    below = true;
                    break;
                }
            }
            if (below) {
                HWND above = GetWindow(progman_, GW_HWNDPREV);
                while (above && owns(above)) above = GetWindow(above, GW_HWNDPREV);
                HWND after = above ? above : HWND_TOP;
                for (HWND w : wins_) {
                    SetWindowPos(w, after, 0, 0, 0, 0, keep);
                    after = w;
                }
                if (g_repairLogs++ < 5) logLine(L"Ebene vor den Symbolen wieder über den Desktop gelegt");
            }
            return true;
        }
        default:
            return false;
    }
}

std::wstring DesktopLayer::diagnostics() const {
    std::wstring s = L"Ebene: ";
    s += layerName(mode_);
    s += L"\r\n  Progman: " + hwndText(progman_);
    s += L"\r\n  DefView: " + hwndText(defView_);
    s += L"\r\n  WorkerW: " + hwndText(workerW_);
    for (size_t i = 0; i < wins_.size(); ++i) {
        s += L"\r\n  SnowDesk " + std::to_wstring(i) + L": " + hwndText(wins_[i]);
        HWND parent = GetParent(wins_[i]);
        s += L" Eltern=" + classOf(parent);
    }
    // Kinder von Progman in Z-Reihenfolge (zur Fehlersuche)
    HWND pm = progman_ ? progman_ : FindWindowW(L"Progman", nullptr);
    if (pm) {
        s += L"\r\n  Progman-Kinder (oben nach unten):";
        int k = 0;
        for (HWND c = GetWindow(pm, GW_CHILD); c && k < 12; c = GetWindow(c, GW_HWNDNEXT), ++k) s += L"\r\n    " + hwndText(c);
    }
    return s;
}

}  // namespace app
