// DeskCats – Einstellungsfenster (schlicht, Windows-Steuerelemente, an die Bildschirmskalierung angepasst)
#include "settings_ui.h"

#include <commctrl.h>

#include <algorithm>

namespace app {

namespace {

const wchar_t* kClass = L"DeskCatsSettings";
enum : int {
    kIdFatos = 101,
    kIdKosta,
    kIdSize,
    kIdActivity,
    kIdWindows,
    kIdTaskbar,
    kIdMonitors,
    kIdAutostart,
    kIdGames,
    kIdOk,
    kIdCancel,
    kIdApply,
};

struct State {
    HWND wnd = nullptr;
    HFONT font = nullptr, bold = nullptr;
    Settings s;
    std::function<void(const Settings&, bool)> onApply;
    UINT dpi = 96;
};
State g;

int px(int v) { return MulDiv(v, (int)g.dpi, 96); }

HWND add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, bool bold = false) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(bold ? g.bold : g.font), TRUE);
    return c;
}

void check(int id, bool on) { CheckDlgButton(g.wnd, id, on ? BST_CHECKED : BST_UNCHECKED); }
bool checked(int id) { return IsDlgButtonChecked(g.wnd, id) == BST_CHECKED; }

Settings collect() {
    Settings s = g.s;
    s.fatos = checked(kIdFatos);
    s.kosta = checked(kIdKosta);
    if (!s.fatos && !s.kosta) s.fatos = true;
    s.size = 2 + (int)SendDlgItemMessageW(g.wnd, kIdSize, CB_GETCURSEL, 0, 0);
    s.size = std::max(2, std::min(5, s.size));
    s.activity = std::max(0, (int)SendDlgItemMessageW(g.wnd, kIdActivity, CB_GETCURSEL, 0, 0));
    s.onWindows = checked(kIdWindows);
    s.onTaskbar = checked(kIdTaskbar);
    s.allMonitors = checked(kIdMonitors);
    wchar_t buf[2048];
    GetDlgItemTextW(g.wnd, kIdGames, buf, 2047);
    buf[2047] = 0;
    s.games = splitGames(buf);
    return s;
}

void build(HWND w) {
    int y = 14;
    add(w, L"STATIC", L"Katzen", SS_LEFT, 16, y, 300, 20, 0, true);
    y += 24;
    add(w, L"BUTTON", L"Fatos", BS_AUTOCHECKBOX | WS_TABSTOP, 24, y, 120, 22, kIdFatos);
    add(w, L"BUTTON", L"Kosta", BS_AUTOCHECKBOX | WS_TABSTOP, 150, y, 120, 22, kIdKosta);
    y += 34;
    add(w, L"STATIC", L"Größe", SS_LEFT, 16, y + 3, 110, 20, 0);
    HWND size = add(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 130, y, 220, 200, kIdSize);
    for (const wchar_t* t : {L"Klein", L"Mittel", L"Groß", L"Sehr groß"}) SendMessageW(size, CB_ADDSTRING, 0, (LPARAM)t);
    y += 32;
    add(w, L"STATIC", L"Lebhaftigkeit", SS_LEFT, 16, y + 3, 110, 20, 0);
    HWND act = add(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 130, y, 220, 200, kIdActivity);
    for (const wchar_t* t : {L"Ruhig", L"Normal", L"Verspielt"}) SendMessageW(act, CB_ADDSTRING, 0, (LPARAM)t);
    y += 40;
    add(w, L"STATIC", L"Wo sie unterwegs sein dürfen", SS_LEFT, 16, y, 330, 20, 0, true);
    y += 24;
    add(w, L"BUTTON", L"Auf Fenster klettern", BS_AUTOCHECKBOX | WS_TABSTOP, 24, y, 330, 22, kIdWindows);
    y += 26;
    add(w, L"BUTTON", L"Auf der Taskleiste", BS_AUTOCHECKBOX | WS_TABSTOP, 24, y, 330, 22, kIdTaskbar);
    y += 26;
    add(w, L"BUTTON", L"Auf allen Monitoren (sonst nur Hauptmonitor)", BS_AUTOCHECKBOX | WS_TABSTOP, 24, y, 330, 22, kIdMonitors);
    y += 36;
    add(w, L"STATIC", L"Ausblenden, während diese Programme laufen", SS_LEFT, 16, y, 340, 20, 0, true);
    y += 24;
    add(w, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 24, y, 326, 24, kIdGames);
    y += 28;
    add(w, L"STATIC", L"Mit ; trennen. Bei Vollbild und gesperrtem Bildschirm sind sie ohnehin weg.", SS_LEFT, 24, y, 330, 36, 0);
    y += 44;
    add(w, L"BUTTON", L"Mit Windows starten", BS_AUTOCHECKBOX | WS_TABSTOP, 16, y, 330, 22, kIdAutostart);
    y += 40;
    add(w, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 106, y, 76, 28, kIdOk);
    add(w, L"BUTTON", L"Abbrechen", BS_PUSHBUTTON | WS_TABSTOP, 188, y, 80, 28, kIdCancel);
    add(w, L"BUTTON", L"Übernehmen", BS_PUSHBUTTON | WS_TABSTOP, 274, y, 80, 28, kIdApply);

    check(kIdFatos, g.s.fatos);
    check(kIdKosta, g.s.kosta);
    SendMessageW(size, CB_SETCURSEL, (WPARAM)std::max(0, std::min(3, g.s.size - 2)), 0);
    SendMessageW(act, CB_SETCURSEL, (WPARAM)std::max(0, std::min(2, g.s.activity)), 0);
    check(kIdWindows, g.s.onWindows);
    check(kIdTaskbar, g.s.onTaskbar);
    check(kIdMonitors, g.s.allMonitors);
    check(kIdAutostart, autostartEnabled());
    SetDlgItemTextW(w, kIdGames, joinGames(g.s.games).c_str());
}

void makeFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.bold) DeleteObject(g.bold);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -MulDiv(9, (int)g.dpi, 72);
    g.font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    g.bold = CreateFontIndirectW(&lf);
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.wnd = w;
            build(w);
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == kIdOk || id == kIdApply) {
                Settings s = collect();
                bool autostart = checked(kIdAutostart);
                g.s = s;
                if (g.onApply) g.onApply(s, autostart);
                if (id == kIdOk) DestroyWindow(w);
            } else if (id == kIdCancel || id == IDCANCEL) {
                DestroyWindow(w);
            }
            return 0;
        }
        case WM_DPICHANGED: {   // auf einen Bildschirm mit anderer Skalierung gezogen: neu aufbauen, Eingaben behalten
            g.s = collect();
            bool autostart = checked(kIdAutostart);
            std::wstring games;
            {
                wchar_t buf[4096] = {0};
                GetDlgItemTextW(w, kIdGames, buf, 4095);
                games = buf;
            }
            while (HWND child = GetWindow(w, GW_CHILD)) DestroyWindow(child);
            g.dpi = HIWORD(wp) >= 48 ? HIWORD(wp) : 96;
            makeFonts();
            build(w);
            check(kIdAutostart, autostart);
            SetDlgItemTextW(w, kIdGames, games.c_str());
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_CLOSE: DestroyWindow(w); return 0;
        case WM_DESTROY:
            g.wnd = nullptr;
            if (g.font) DeleteObject(g.font);
            if (g.bold) DeleteObject(g.bold);
            g.font = g.bold = nullptr;
            return 0;
        default: break;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

bool settingsWindowOpen() { return g.wnd != nullptr; }

bool settingsDialogMessage(MSG* m) { return g.wnd && IsDialogMessageW(g.wnd, m); }

void showSettingsWindow(HINSTANCE inst, const Settings& current, std::function<void(const Settings&, bool)> onApply) {
    if (g.wnd) {
        ShowWindow(g.wnd, SW_RESTORE);
        SetForegroundWindow(g.wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        registered = true;
    }
    g.s = current;
    g.onApply = std::move(onApply);
    POINT cur{0, 0};
    GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    UINT dx = 96, dy = 96;
    if (auto fn = (GetDpiForMonitorFn)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor")) fn(mon, 0, &dx, &dy);
    g.dpi = dx >= 48 ? dx : 96;
    makeFonts();
    RECT r{0, 0, px(372), px(436)};   // Inhalt endet bei y = 420 (Knöpfe)
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"DeskCats – Einstellungen", style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!wnd) return;
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
}

}  // namespace app
