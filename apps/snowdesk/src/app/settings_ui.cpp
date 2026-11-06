// SnowDesk – Einstellungsfenster (siehe settings_ui.h)
#include "settings_ui.h"

#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>

namespace app {

using namespace Gdiplus;

namespace {

const wchar_t* kClass = L"SnowDeskSettings";
enum Type { TToggle = 1, TSlider = 2, TSegment = 3, TButton = 4 };
enum Id {
    IdEnabled = 1,
    IdPreset,
    IdAmount,
    IdWind,
    IdSize,
    IdSpeed,
    IdCrystals,
    IdMaxPile,
    IdFill,
    IdRelease,
    IdFps,
    IdRender,
    IdMouse,
    IdPauseGames,
    IdAutostart,
    IdShake,
    IdDefaults,
    IdRealWeather,
    IdSearch
};
const UINT_PTR kStatusTimer = 1;
const int kEditId = 100;
const int kPlaceEditId = 101;
const WORD kPlaceEnter = 0x7001;   // Enter im Ortsfeld

WNDPROC g_placeEditProc = nullptr;
LRESULT CALLBACK placeEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR && (w == VK_RETURN || w == L'\n')) {
        PostMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(kPlaceEditId, kPlaceEnter), (LPARAM)h);
        return 0;
    }
    return CallWindowProcW(g_placeEditProc, h, m, w, l);
}
const float kWidth = 800.0f;
const float kHeader = 84.0f;

Color rgb(DWORD c, BYTE a = 255) { return Color(a, (BYTE)((c >> 16) & 0xFF), (BYTE)((c >> 8) & 0xFF), (BYTE)(c & 0xFF)); }
COLORREF ref(DWORD c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

bool appsDark() {
    DWORD v = 1, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

// Akzentfarbe des Systems (helle Variante im dunklen Modus, dunkle im hellen – wie Windows-11-Steuerelemente)
DWORD accentColor(bool dark) {
    BYTE buf[32];
    DWORD size = sizeof(buf), type = 0;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_QUERY_VALUE, &k) ==
        ERROR_SUCCESS) {
        LONG rc = RegQueryValueExW(k, L"AccentPalette", nullptr, &type, buf, &size);
        RegCloseKey(k);
        if (rc == ERROR_SUCCESS && type == REG_BINARY && size >= 32) {
            int i = dark ? 1 : 4;
            return ((DWORD)buf[i * 4] << 16) | ((DWORD)buf[i * 4 + 1] << 8) | buf[i * 4 + 2];
        }
    }
    return dark ? 0x4CC2FF : 0x005FB8;
}

void roundPath(GraphicsPath& p, const RectF& r, float rad) {
    float d = rad * 2.0f;
    d = std::min(d, std::min(r.Width, r.Height));
    if (d <= 0.01f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

void fillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    roundPath(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void strokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w) {
    GraphicsPath p;
    roundPath(p, r, rad);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

void fillCircle(Graphics& g, float cx, float cy, float r, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

void drawText(Graphics& g, const std::wstring& s, const RectF& r, const Font* f, const Color& c, StringAlignment h,
              StringAlignment v = StringAlignmentCenter, bool wrap = false) {
    StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    if (!wrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, r, &fmt, &b);
}

std::wstring fmt(const wchar_t* f, double v) {
    wchar_t buf[64];
    _snwprintf(buf, 63, f, v);
    buf[63] = 0;
    std::wstring s = buf;
    for (wchar_t& c : s) {
        if (c == L'.') c = L',';   // deutsches Komma
    }
    return s;
}

}  // namespace

struct SettingsWindow::Theme {
    bool dark = false;
    Color bg, card, cardBorder, text, text2, accent, accentText, rail, control, controlBorder, hover, pressed, thumb, thumbBorder;
    COLORREF textRef = 0, editBgRef = 0, bgRef = 0;
};

SettingsWindow::SettingsWindow() : theme_(new Theme) {}

SettingsWindow::~SettingsWindow() { close(); }

void SettingsWindow::loadTheme() {
    Theme& t = *theme_;
    t.dark = appsDark();
    DWORD acc = accentColor(t.dark);
    if (t.dark) {
        t.bg = rgb(0x202020);
        t.card = rgb(0x2B2B2B);
        t.cardBorder = rgb(0x1C1C1C);
        t.text = rgb(0xFFFFFF);
        t.text2 = rgb(0xC8C8C8);
        t.accent = rgb(acc);
        t.accentText = rgb(0x000000);
        t.rail = rgb(0x8A8A8A);
        t.control = rgb(0x373737);
        t.controlBorder = rgb(0x444444);
        t.hover = rgb(0x3F3F3F);
        t.pressed = rgb(0x333333);
        t.thumb = rgb(0x454545);
        t.thumbBorder = rgb(0x505050);
        t.textRef = ref(0xFFFFFF);
        t.editBgRef = ref(0x373737);
        t.bgRef = ref(0x202020);
    } else {
        t.bg = rgb(0xF3F3F3);
        t.card = rgb(0xFFFFFF);
        t.cardBorder = rgb(0xE5E5E5);
        t.text = rgb(0x1B1B1B);
        t.text2 = rgb(0x5F5F5F);
        t.accent = rgb(acc);
        t.accentText = rgb(0xFFFFFF);
        t.rail = rgb(0x8C8C8C);
        t.control = rgb(0xFBFBFB);
        t.controlBorder = rgb(0xDCDCDC);
        t.hover = rgb(0xF0F0F0);
        t.pressed = rgb(0xE8E8E8);
        t.thumb = rgb(0xFFFFFF);
        t.thumbBorder = rgb(0xD0D0D0);
        t.textRef = ref(0x1B1B1B);
        t.editBgRef = ref(0xFBFBFB);
        t.bgRef = ref(0xF3F3F3);
    }
    if (editBrush_) DeleteObject(editBrush_);
    editBrush_ = CreateSolidBrush(t.editBgRef);
}

void SettingsWindow::applyWindowTheme() {
    if (!hwnd_) return;
    if (edit_) SetWindowTheme(edit_, theme_->dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (placeEdit_) SetWindowTheme(placeEdit_, theme_->dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    BOOL dark = theme_->dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = theme_->bgRef;
    DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR (Windows 11)*/, &caption, sizeof(caption));
}

void SettingsWindow::createFonts() {
    FontFamily regular(L"Segoe UI");
    const FontFamily* fam = regular.GetLastStatus() == Ok ? &regular : FontFamily::GenericSansSerif();
    FontFamily semi(L"Segoe UI Semibold");
    bool haveSemi = semi.GetLastStatus() == Ok;
    // Schriftgrößen in DIP; gezeichnet wird mit Skalierung (scale_)
    fTitle_.reset(haveSemi ? new Font(&semi, 22.0f, FontStyleRegular, UnitPixel) : new Font(fam, 22.0f, FontStyleBold, UnitPixel));
    fCard_.reset(haveSemi ? new Font(&semi, 14.0f, FontStyleRegular, UnitPixel) : new Font(fam, 14.0f, FontStyleBold, UnitPixel));
    fBody_.reset(new Font(fam, 13.5f, FontStyleRegular, UnitPixel));
    fBodyBold_.reset(haveSemi ? new Font(&semi, 13.0f, FontStyleRegular, UnitPixel) : new Font(fam, 13.0f, FontStyleBold, UnitPixel));
    fSmall_.reset(new Font(fam, 12.0f, FontStyleRegular, UnitPixel));

    if (editFont_) DeleteObject(editFont_);
    editFont_ = CreateFontW(-(int)std::lround(13.5f * scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (edit_) SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    if (placeEdit_) SendMessageW(placeEdit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
}

void SettingsWindow::layout() {
    ctls_.clear();
    cards_.clear();
    const float m = 24.0f, gap = 16.0f, colW = (kWidth - 2 * m - gap) / 2, pad = 16.0f, titleH = 44.0f, cardGap = 12.0f;
    const float inner = colW - 2 * pad;
    auto toggleRow = [&](int id, const wchar_t* label, float x, float& y) {
        Ctl c;
        c.id = id;
        c.type = TToggle;
        c.x = x;
        c.y = y;
        c.w = inner;
        c.h = 38;
        c.hw = 40;
        c.hh = 20;
        c.hx = x + inner - c.hw;
        c.hy = y + (c.h - c.hh) / 2;
        c.label = label;
        ctls_.push_back(c);
        y += c.h;
    };
    auto sliderRow = [&](int id, const wchar_t* label, float minV, float maxV, float step, float x, float& y) {
        Ctl c;
        c.id = id;
        c.type = TSlider;
        c.x = x;
        c.y = y;
        c.w = inner;
        c.h = 52;
        c.hx = x;
        c.hy = y + 22;
        c.hw = inner;
        c.hh = 26;
        c.label = label;
        c.minV = minV;
        c.maxV = maxV;
        c.step = step;
        ctls_.push_back(c);
        y += c.h;
    };
    auto segRow = [&](int id, const wchar_t* label, std::vector<std::wstring> opts, float x, float& y, std::vector<float> weights = {}) {
        Ctl c;
        c.id = id;
        c.type = TSegment;
        c.x = x;
        c.y = y;
        c.w = inner;
        c.label = label ? label : L"";
        float top = c.label.empty() ? 4.0f : 26.0f;
        c.hx = x;
        c.hy = y + top;
        c.hw = inner;
        c.hh = 32;
        c.h = top + 32 + 10;
        c.options = std::move(opts);
        c.weights = std::move(weights);
        ctls_.push_back(c);
        y += c.h;
    };
    auto beginCard = [&](const wchar_t* title, float x, float y) {
        Card cd{x, y, colW, 0, title};
        cards_.push_back(cd);
        return y + titleH;
    };
    auto endCard = [&](float y) {
        Card& cd = cards_.back();
        cd.h = y + 10 - cd.y;
        return cd.y + cd.h + cardGap;
    };

    // linke Spalte
    float x = m + pad, y = kHeader;
    y = beginCard(L"Schnee", m, y);
    toggleRow(IdEnabled, L"Schnee fällt", x, y);
    segRow(IdPreset, nullptr, {L"Leicht", L"Mittel", L"Schneesturm", L"Eigene"}, x, y, {0.95f, 0.95f, 1.45f, 0.95f});
    sliderRow(IdAmount, L"Menge", 10, 400, 5, x, y);
    sliderRow(IdWind, L"Wind (mit Böen)", 0, 100, 1, x, y);
    y = endCard(y);
    y = beginCard(L"Flocken", m, y);
    sliderRow(IdSize, L"Flockengröße", 50, 200, 5, x, y);
    sliderRow(IdSpeed, L"Fallgeschwindigkeit", 50, 200, 5, x, y);
    sliderRow(IdCrystals, L"Schneekristalle", 0, 30, 1, x, y);
    y = endCard(y);
    y = beginCard(L"Liegen bleiben", m, y);
    sliderRow(IdMaxPile, L"Maximale Höhe", 0, 120, 1, x, y);
    sliderRow(IdFill, L"Zeit bis voll", 1, 30, 0.5f, x, y);
    segRow(IdRelease, L"Beim Wegziehen fällt der Schnee als", {L"Mischung", L"Klumpen", L"Brösel", L"Sanft"}, x, y);
    float leftBottom = endCard(y);

    // rechte Spalte
    float rx = m + colW + gap;
    x = rx + pad;
    y = kHeader;
    y = beginCard(L"Echtes Wetter", rx, y);
    toggleRow(IdRealWeather, L"Wetter vom Ort übernehmen", x, y);
    placeX_ = x;
    placeY_ = y + 4;
    placeW_ = inner - 104;
    placeH_ = 32;
    {
        Ctl bs;
        bs.id = IdSearch;
        bs.type = TButton;
        bs.x = bs.hx = x + inner - 96;
        bs.y = bs.hy = placeY_;
        bs.w = bs.hw = 96;
        bs.h = bs.hh = 32;
        bs.label = L"Ort suchen";
        ctls_.push_back(bs);
    }
    y += 44;
    wxTextY_ = y;
    y += 38;
    y = endCard(y);
    y = beginCard(L"Darstellung", rx, y);
    int hz = cb_.monitorHz ? cb_.monitorHz() : 60;
    segRow(IdFps, L"Bilder pro Sekunde", {L"30", L"60", L"Monitor (" + std::to_wstring(hz) + L" Hz)"}, x, y, {0.7f, 0.7f, 1.6f});
    segRow(IdRender, L"Schnee-Ebene", {L"Hinter Symbolen", L"Alternative", L"Vor Symbolen"}, x, y, {1.3f, 0.9f, 1.1f});
    toggleRow(IdMouse, L"Maus pustet Flocken weg", x, y);
    y = endCard(y);
    y = beginCard(L"Pausieren", rx, y);
    toggleRow(IdPauseGames, L"In Spielen pausieren", x, y);
    editX_ = x;
    editY_ = y + 4;
    editW_ = inner;
    editH_ = 104;
    y += 112;
    hintX_ = x;
    hintY_ = y;
    hintW_ = inner;
    y += 50;
    y = endCard(y);
    y = beginCard(L"System", rx, y);
    toggleRow(IdAutostart, L"Mit Windows starten", x, y);
    y = endCard(y);
    // Knöpfe
    float bw = (colW - 12) / 2;
    Ctl b1;
    b1.id = IdShake;
    b1.type = TButton;
    b1.x = b1.hx = rx;
    b1.y = b1.hy = y;
    b1.w = b1.hw = bw;
    b1.h = b1.hh = 34;
    b1.label = L"Alles abschütteln";
    ctls_.push_back(b1);
    Ctl b2 = b1;
    b2.id = IdDefaults;
    b2.x = b2.hx = rx + bw + 12;
    b2.label = L"Standardwerte";
    ctls_.push_back(b2);
    float rightBottom = y + 34 + cardGap;
    contentH_ = std::max(leftBottom, rightBottom) + 12;
}

int SettingsWindow::clientHeightPx() const {
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    return rc.bottom;
}

bool SettingsWindow::open(const Settings& s, const Callbacks& cb) {
    if (hwnd_) {
        refresh(s);
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        return true;
    }
    s_ = s;
    cb_ = cb;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        registered = RegisterClassExW(&wc) != 0;
    }
    loadTheme();

    // Fenster auf dem Monitor mit dem Mauszeiger öffnen
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    UINT dpi = 96;
    {
        // DPI des Zielmonitors (Fenster existiert noch nicht)
        typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            auto fn = (GetDpiForMonitorFn)(void*)GetProcAddress(shcore, "GetDpiForMonitor");
            UINT dx = 96, dy = 96;
            if (fn && SUCCEEDED(fn(mon, 0, &dx, &dy))) dpi = dx;
            FreeLibrary(shcore);
        }
    }
    scale_ = dpi / 96.0f;
    layout();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    float maxH = (mi.rcWork.bottom - mi.rcWork.top) / scale_ - 60.0f;
    float hDip = std::min(contentH_, std::max(300.0f, maxH));
    RECT rc{0, 0, (LONG)std::lround(kWidth * scale_), (LONG)std::lround(hDip * scale_)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    hwnd_ = CreateWindowExW(0, kClass, L"SnowDesk – Einstellungen", style, x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        logLine(L"Einstellungsfenster konnte nicht geöffnet werden (%lu)", GetLastError());
        return false;
    }
    UINT realDpi = GetDpiForWindow(hwnd_);
    if (realDpi && realDpi != dpi) scale_ = realDpi / 96.0f;
    applyWindowTheme();
    iconBig_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXICON, realDpi ? realDpi : dpi),
                                 GetSystemMetricsForDpi(SM_CYICON, realDpi ? realDpi : dpi), 0);
    iconSmall_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, realDpi ? realDpi : dpi),
                                   GetSystemMetricsForDpi(SM_CYSMICON, realDpi ? realDpi : dpi), 0);
    if (iconBig_) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)iconBig_);
    if (iconSmall_) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall_);

    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10,
                            hwnd_, (HMENU)(INT_PTR)kEditId, inst, nullptr);
    placeEdit_ = CreateWindowExW(0, L"EDIT", s_.place.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
                                 (HMENU)(INT_PTR)kPlaceEditId, inst, nullptr);
    if (placeEdit_) {
        g_placeEditProc = (WNDPROC)SetWindowLongPtrW(placeEdit_, GWLP_WNDPROC, (LONG_PTR)placeEditProc);
        SendMessageW(placeEdit_, EM_SETLIMITTEXT, 80, 0);
        SendMessageW(placeEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"Ort, z. B. Hamburg");
    }
    createFonts();
    applyWindowTheme();
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((int)(6 * scale_), (int)(6 * scale_)));
    if (placeEdit_) SendMessageW(placeEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((int)(6 * scale_), (int)(6 * scale_)));
    syncEditFromSettings();
    positionEdit();
    SetTimer(hwnd_, kStatusTimer, 1000, nullptr);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    SetForegroundWindow(hwnd_);
    return true;
}

void SettingsWindow::close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void SettingsWindow::refresh(const Settings& s) {
    std::wstring keepGames = s_.games;
    s_ = s;
    if (edit_ && GetFocus() == edit_) s_.games = keepGames;   // nicht mitten ins Tippen funken
    else syncEditFromSettings();
    if (placeEdit_ && GetFocus() != placeEdit_) {
        wchar_t cur[128] = {0};
        GetWindowTextW(placeEdit_, cur, 127);
        if (s_.place != cur) SetWindowTextW(placeEdit_, s_.place.c_str());
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::startSearch() {
    if (!placeEdit_) return;
    wchar_t buf[128] = {0};
    GetWindowTextW(placeEdit_, buf, 127);
    std::wstring q = buf;
    while (!q.empty() && iswspace(q.back())) q.pop_back();
    while (!q.empty() && iswspace(q.front())) q.erase(q.begin());
    if (q.empty()) {
        searchNote_ = L"Bitte einen Ort eingeben.";
    } else if (cb_.searchPlace && cb_.searchPlace(q)) {
        searchNote_ = L"Suche „" + q + L"“ …";
    } else {
        searchNote_ = L"Die Suche ließ sich nicht starten.";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::choosePlace(const wx::Place& p) {
    s_.place = p.name;
    s_.placeDetail = p.detail;
    s_.lat = p.lat;
    s_.lon = p.lon;
    s_.realWeather = true;   // wer einen Ort aussucht, will sein Wetter
    s_.enabled = true;
    searchNote_.clear();
    if (placeEdit_) SetWindowTextW(placeEdit_, p.name.c_str());
    changed();
}

void SettingsWindow::placesFound(bool ok, const std::vector<wx::Place>& places, const std::wstring& error) {
    if (!hwnd_) return;
    if (!ok) {
        searchNote_ = L"Ortssuche: " + error;
    } else if (places.empty()) {
        searchNote_ = L"Kein Ort mit diesem Namen gefunden.";
    } else if (places.size() == 1) {
        choosePlace(places[0]);
        return;
    } else {
        // mehrere Treffer: kleines Menü unter dem Suchknopf
        HMENU m = CreatePopupMenu();
        for (size_t i = 0; i < places.size() && i < 8; ++i) AppendMenuW(m, MF_STRING, 1 + i, places[i].detail.c_str());
        POINT pt{0, 0};
        for (const Ctl& c : ctls_) {
            if (c.id != IdSearch) continue;
            pt.x = (LONG)std::lround(c.hx * scale_);
            pt.y = (LONG)std::lround((c.hy + c.hh - scrollY_) * scale_);
        }
        ClientToScreen(hwnd_, &pt);
        SetForegroundWindow(hwnd_);
        UINT pick = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(m);
        if (pick >= 1 && pick <= places.size()) {
            choosePlace(places[pick - 1]);
            return;
        }
        searchNote_.clear();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::syncEditFromSettings() {
    if (!edit_) return;
    std::wstring text;
    for (const std::wstring& g : s_.gameList()) {
        if (!text.empty()) text += L"\r\n";
        text += g;
    }
    int len = GetWindowTextLengthW(edit_);
    std::wstring cur(len + 1, L'\0');
    GetWindowTextW(edit_, &cur[0], len + 1);
    cur.resize(len);
    if (cur == text) return;
    updatingEdit_ = true;
    SetWindowTextW(edit_, text.c_str());
    updatingEdit_ = false;
}

void SettingsWindow::positionEdit() {
    if (!edit_) return;
    int x = (int)std::lround(editX_ * scale_), y = (int)std::lround((editY_ - scrollY_) * scale_);
    int w = (int)std::lround(editW_ * scale_), h = (int)std::lround(editH_ * scale_);
    // innen 1 px Abstand für den eigenen, runden Rahmen
    SetWindowPos(edit_, nullptr, x + 2, y + 2, w - 4, h - 4, SWP_NOZORDER | SWP_NOACTIVATE);
    if (placeEdit_) {
        // einzeilig: Text senkrecht mittig (Höhe = Schrift + Rand)
        int px = (int)std::lround(placeX_ * scale_), pw = (int)std::lround(placeW_ * scale_);
        int ph = (int)std::lround(placeH_ * scale_), eh = (int)std::lround(20 * scale_);
        int py = (int)std::lround((placeY_ - scrollY_) * scale_) + (ph - eh) / 2;
        SetWindowPos(placeEdit_, nullptr, px + 2, py, pw - 4, eh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SettingsWindow::scrollTo(float y) {
    float viewH = clientHeightPx() / scale_;
    float maxY = std::max(0.0f, contentH_ - viewH);
    float ny = std::max(0.0f, std::min(maxY, y));
    if (std::fabs(ny - scrollY_) < 0.01f) return;
    scrollY_ = ny;
    positionEdit();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ensureBackBuffer(int w, int h) {
    if (memDC_ && memW_ == w && memH_ == h) return;
    freeBackBuffer();
    HDC screen = GetDC(nullptr);
    memDC_ = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    memBmp_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &memBits_, nullptr, 0);
    ReleaseDC(nullptr, screen);
    memOld_ = SelectObject(memDC_, memBmp_);
    memW_ = w;
    memH_ = h;
}

void SettingsWindow::freeBackBuffer() {
    if (memDC_) {
        SelectObject(memDC_, memOld_);
        DeleteDC(memDC_);
    }
    if (memBmp_) DeleteObject(memBmp_);
    memDC_ = nullptr;
    memBmp_ = nullptr;
    memW_ = memH_ = 0;
}

// ---------------------------------------------------------------- Werte

float SettingsWindow::sliderValue(int id) const {
    switch (id) {
        case IdAmount: return s_.amount * 100.0f;
        case IdWind: return s_.wind * 100.0f;
        case IdSize: return s_.flakeSize * 100.0f;
        case IdSpeed: return s_.fallSpeed * 100.0f;
        case IdCrystals: return s_.crystals * 100.0f;
        case IdMaxPile: return s_.maxPile;
        case IdFill: return s_.fillMinutes;
        default: return 0.0f;
    }
}

std::wstring SettingsWindow::sliderText(int id) const {
    float v = sliderValue(id);
    switch (id) {
        case IdMaxPile:
            return v < 0.5f ? std::wstring(L"aus – nichts bleibt liegen") : fmt(L"%.0f px", v) + (std::fabs(v - 32.0f) < 0.5f ? L" (≈ Titelleiste)" : L"");
        case IdFill: {
            std::wstring t = std::fabs(v - std::round(v)) < 0.01f ? fmt(L"%.0f min", v) : fmt(L"%.1f min", v);
            return t + L" (bei Mittel)";
        }
        default:
            return fmt(L"%.0f %%", v);
    }
}

void SettingsWindow::setSliderFromX(const Ctl& c, float dipX) {
    float x0 = c.hx + 10.0f, x1 = c.hx + c.hw - 10.0f;
    float t = std::max(0.0f, std::min(1.0f, (dipX - x0) / std::max(1.0f, x1 - x0)));
    float v = c.minV + t * (c.maxV - c.minV);
    v = std::round(v / c.step) * c.step;
    v = std::max(c.minV, std::min(c.maxV, v));
    float old = sliderValue(c.id);
    if (std::fabs(old - v) < 1e-4f) return;
    switch (c.id) {
        case IdAmount:
            s_.amount = v / 100.0f;
            s_.preset = s_.matchingPreset();
            s_.realWeather = false;
            break;
        case IdWind:
            s_.wind = v / 100.0f;
            s_.preset = s_.matchingPreset();
            s_.realWeather = false;
            break;
        case IdSize: s_.flakeSize = v / 100.0f; break;
        case IdSpeed: s_.fallSpeed = v / 100.0f; break;
        case IdCrystals: s_.crystals = v / 100.0f; break;
        case IdMaxPile: s_.maxPile = v; break;
        case IdFill: s_.fillMinutes = v; break;
        default: break;
    }
    changed();
}

int SettingsWindow::segmentIndex(int id) const {
    switch (id) {
        case IdPreset: return s_.realWeather ? -1 : s_.preset;   // echtes Wetter: keine feste Stufe
        case IdRelease: return s_.release;
        case IdFps: return s_.fps == 30 ? 0 : (s_.fps == 60 ? 1 : 2);
        case IdRender: return s_.render;
        default: return 0;
    }
}

void SettingsWindow::selectSegment(int id, int index) {
    if (segmentIndex(id) == index) return;
    switch (id) {
        case IdPreset:
            if (index <= PresetStorm) s_.applyPreset(index);
            else s_.preset = PresetCustom;
            s_.realWeather = false;   // feste Stärke gewählt
            break;
        case IdRelease: s_.release = index; break;
        case IdFps: s_.fps = index == 0 ? 30 : (index == 1 ? 60 : 0); break;
        case IdRender: s_.render = index; break;
        default: break;
    }
    changed();
}

bool SettingsWindow::toggleValue(int id) const {
    switch (id) {
        case IdEnabled: return s_.enabled;
        case IdMouse: return s_.mouseBlow;
        case IdPauseGames: return s_.pauseGames;
        case IdAutostart: return s_.autostart;
        case IdRealWeather: return s_.realWeather;
        default: return false;
    }
}

void SettingsWindow::toggle(int id) {
    switch (id) {
        case IdEnabled: s_.enabled = !s_.enabled; break;
        case IdMouse: s_.mouseBlow = !s_.mouseBlow; break;
        case IdPauseGames: s_.pauseGames = !s_.pauseGames; break;
        case IdAutostart: s_.autostart = !s_.autostart; break;
        case IdRealWeather:
            s_.realWeather = !s_.realWeather;
            if (s_.realWeather) s_.enabled = true;
            break;
        default: return;
    }
    changed();
}

void SettingsWindow::changed() {
    s_.clampAll();
    if (cb_.changed) cb_.changed(s_);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------- Zeichnen

void SettingsWindow::drawControl(Graphics& g, const Ctl& c, bool hovered, bool pressed) {
    const Theme& t = *theme_;
    switch (c.type) {
        case TToggle: {
            drawText(g, c.label, RectF(c.x, c.y, c.w - 100, c.h), fBody_.get(), t.text, StringAlignmentNear);
            bool on = toggleValue(c.id);
            drawText(g, on ? L"An" : L"Aus", RectF(c.hx - 60, c.y, 50, c.h), fBody_.get(), t.text2, StringAlignmentFar);
            RectF r(c.hx, c.hy, c.hw, c.hh);
            if (on) {
                fillRound(g, r, c.hh / 2, t.accent);
                fillCircle(g, r.X + r.Width - 10, r.Y + 10, hovered ? 7.0f : 6.0f, t.accentText);
            } else {
                if (hovered) fillRound(g, r, c.hh / 2, t.hover);
                RectF o(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1);
                strokeRound(g, o, c.hh / 2 - 0.5f, t.text2, 1.0f);
                fillCircle(g, r.X + 10, r.Y + 10, hovered ? 6.0f : 5.0f, t.text2);
            }
            break;
        }
        case TSlider: {
            std::wstring value = sliderText(c.id);
            RectF box;
            g.MeasureString(value.c_str(), -1, fBody_.get(), PointF(0, 0), &box);
            float vw = std::min(c.w * 0.7f, box.Width + 4.0f);
            drawText(g, c.label, RectF(c.x, c.y + 2, c.w - vw - 8.0f, 20), fBody_.get(), t.text, StringAlignmentNear);
            drawText(g, value, RectF(c.x + c.w - vw, c.y + 2, vw, 20), fBody_.get(), t.text2, StringAlignmentFar);
            float x0 = c.hx + 10, x1 = c.hx + c.hw - 10, cy = c.hy + c.hh / 2;
            float v = sliderValue(c.id);
            float k = (v - c.minV) / (c.maxV - c.minV);
            k = std::max(0.0f, std::min(1.0f, k));
            float tx = x0 + k * (x1 - x0);
            fillRound(g, RectF(x0, cy - 2, x1 - x0, 4), 2, t.rail);
            fillRound(g, RectF(x0, cy - 2, std::max(4.0f, tx - x0), 4), 2, t.accent);
            fillCircle(g, tx, cy, 10.0f, t.thumbBorder);
            fillCircle(g, tx, cy, 9.0f, t.thumb);
            fillCircle(g, tx, cy, pressed ? 5.0f : (hovered ? 7.0f : 6.0f), t.accent);
            break;
        }
        case TSegment: {
            if (!c.label.empty()) drawText(g, c.label, RectF(c.x, c.y, c.w, 22), fBody_.get(), t.text, StringAlignmentNear);
            RectF r(c.hx, c.hy, c.hw, c.hh);
            fillRound(g, r, 5, t.control);
            strokeRound(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), 4.5f, t.controlBorder, 1.0f);
            int n = (int)c.options.size();
            int sel = segmentIndex(c.id);
            for (int i = 0; i < n; ++i) {
                float sx, sw;
                segmentSpan(c, i, sx, sw);
                RectF sr(sx + 2, c.hy + 2, sw - 4, c.hh - 4);
                if (i == sel) fillRound(g, sr, 4, t.accent);
                else if (hovered && hover_ >= 0) {
                    // Maus über genau diesem Segment?
                    POINT p;
                    GetCursorPos(&p);
                    ScreenToClient(hwnd_, &p);
                    float mx = p.x / scale_, my = p.y / scale_ + scrollY_;
                    if (mx >= sr.X && mx < sr.X + sr.Width && my >= sr.Y && my < sr.Y + sr.Height) fillRound(g, sr, 4, t.hover);
                }
                drawText(g, c.options[i], sr, i == sel ? fBodyBold_.get() : fBody_.get(), i == sel ? t.accentText : t.text, StringAlignmentCenter);
            }
            break;
        }
        case TButton: {
            RectF r(c.hx, c.hy, c.hw, c.hh);
            fillRound(g, r, 5, pressed ? t.pressed : (hovered ? t.hover : t.control));
            strokeRound(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), 4.5f, t.controlBorder, 1.0f);
            drawText(g, c.label, r, fBody_.get(), t.text, StringAlignmentCenter);
            break;
        }
        default:
            break;
    }
}

void SettingsWindow::paint(HDC dc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int W = std::max<LONG>(1, rc.right), H = std::max<LONG>(1, rc.bottom);
    ensureBackBuffer(W, H);
    const Theme& t = *theme_;
    {
        Graphics g(memDC_);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.Clear(t.bg);
        g.ScaleTransform(scale_, scale_);
        g.TranslateTransform(0.0f, -scrollY_);

        // Kopf: Titel + Status
        drawText(g, L"SnowDesk", RectF(76, 16, 400, 30), fTitle_.get(), t.text, StringAlignmentNear);
        std::wstring status = cb_.status ? cb_.status() : std::wstring();
        drawText(g, status, RectF(77, 46, kWidth - 100, 20), fSmall_.get(), t.text2, StringAlignmentNear);

        // Karten
        for (const Card& c : cards_) {
            RectF r(c.x, c.y, c.w, c.h);
            fillRound(g, r, 8, t.card);
            strokeRound(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), 7.5f, t.cardBorder, 1.0f);
            drawText(g, c.title, RectF(c.x + 16, c.y + 12, c.w - 32, 22), fCard_.get(), t.text, StringAlignmentNear);
        }
        // Bedienelemente
        for (size_t i = 0; i < ctls_.size(); ++i) {
            const Ctl& c = ctls_[i];
            drawControl(g, c, (int)i == hover_ || (int)i == drag_, (int)i == press_ || (int)i == drag_);
            // bei echtem Wetter gelten die festen Stufen nicht: abgeblendet (anklicken schaltet zurück)
            if (s_.realWeather && (c.id == IdPreset || c.id == IdAmount || c.id == IdWind)) {
                Color k = t.card;
                fillRound(g, RectF(c.x - 2, c.y - 1, c.w + 4, c.h + 2), 4, Color(150, k.GetR(), k.GetG(), k.GetB()));
            }
        }
        // Ortsfeld + Wetter
        {
            RectF pr(placeX_, placeY_, placeW_, placeH_);
            fillRound(g, pr, 5, t.control);
            strokeRound(g, RectF(pr.X + 0.5f, pr.Y + 0.5f, pr.Width - 1, pr.Height - 1), 4.5f, t.controlBorder, 1.0f);
            std::wstring wt = !searchNote_.empty() ? searchNote_ : (cb_.weatherText ? cb_.weatherText() : std::wstring());
            float cw = 0;
            for (const Card& cd : cards_)
                if (cd.title == L"Echtes Wetter") cw = cd.w - 32;
            drawText(g, wt, RectF(placeX_, wxTextY_, cw > 0 ? cw : placeW_, 36), fSmall_.get(), t.text2, StringAlignmentNear, StringAlignmentNear, true);
        }

        // Rahmen um das Eingabefeld + Hinweis
        RectF er(editX_, editY_, editW_, editH_);
        fillRound(g, er, 5, t.control);
        strokeRound(g, RectF(er.X + 0.5f, er.Y + 0.5f, er.Width - 1, er.Height - 1), 4.5f, t.controlBorder, 1.0f);
        drawText(g,
                 L"Ein Programm pro Zeile (z. B. cs2.exe). Läuft eins davon, pausiert der Schnee komplett. "
                 L"Vollbild und verdeckte Bildschirme pausieren automatisch.",
                 RectF(hintX_, hintY_, hintW_, 48), fSmall_.get(), t.text2, StringAlignmentNear, StringAlignmentNear, true);

        // Bildlaufanzeige
        g.ResetTransform();
        float viewH = H / scale_;
        if (contentH_ > viewH + 1) {
            float trackH = (float)H - 8 * scale_;
            float thumbH = std::max(24 * scale_, trackH * viewH / contentH_);
            float ty = 4 * scale_ + (trackH - thumbH) * (scrollY_ / (contentH_ - viewH));
            fillRound(g, RectF(W - 7 * scale_, ty, 3 * scale_, thumbH), 1.5f * scale_, t.rail);
        }
    }
    // App-Symbol links neben dem Titel
    HICON icon = iconBig_ ? iconBig_ : iconSmall_;
    if (icon) {
        int sz = (int)std::lround(40 * scale_);
        DrawIconEx(memDC_, (int)std::lround(24 * scale_), (int)std::lround((20 - scrollY_) * scale_), icon, sz, sz, 0, nullptr, DI_NORMAL);
    }
    BitBlt(dc, 0, 0, W, H, memDC_, 0, 0, SRCCOPY);
}

void SettingsWindow::segmentSpan(const Ctl& c, int i, float& x, float& w) {
    int n = (int)c.options.size();
    float total = 0.0f, before = 0.0f;
    for (int k = 0; k < n; ++k) {
        float wt = k < (int)c.weights.size() ? c.weights[k] : 1.0f;
        if (k < i) before += wt;
        total += wt;
    }
    float wi = i < (int)c.weights.size() ? c.weights[i] : 1.0f;
    x = c.hx + c.hw * before / total;
    w = c.hw * wi / total;
}

int SettingsWindow::hitTest(int px, int py) const {
    float x = px / scale_, y = py / scale_ + scrollY_;
    for (size_t i = 0; i < ctls_.size(); ++i) {
        const Ctl& c = ctls_[i];
        float pad = c.type == TSlider ? 6.0f : 3.0f;
        float hx = c.hx - pad, hy = c.hy - pad, hw = c.hw + 2 * pad, hh = c.hh + 2 * pad;
        if (c.type == TToggle) {   // ganze Zeile klickbar
            hx = c.x;
            hy = c.y;
            hw = c.w;
            hh = c.h;
        }
        if (x >= hx && x < hx + hw && y >= hy && y < hy + hh) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------- Nachrichten

LRESULT CALLBACK SettingsWindow::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

LRESULT SettingsWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SIZE:
            positionEdit();
            scrollTo(scrollY_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (drag_ >= 0) {
                setSliderFromX(ctls_[drag_], x / scale_);
                return 0;
            }
            int k = hitTest(x, y);
            bool segment = k >= 0 && ctls_[k].type == TSegment;
            if (k != hover_ || segment) {
                hover_ = k;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_ >= 0) {
                hover_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);   // Eingabefeld verlassen
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int k = hitTest(x, y);
            if (k < 0) return 0;
            const Ctl& c = ctls_[k];
            if (c.type == TToggle) {
                toggle(c.id);
            } else if (c.type == TSegment) {
                int n = (int)c.options.size();
                int idx = n - 1;
                for (int i = 0; i < n; ++i) {
                    float sx, sw;
                    segmentSpan(c, i, sx, sw);
                    if (x / scale_ < sx + sw) {
                        idx = i;
                        break;
                    }
                }
                selectSegment(c.id, idx);
            } else if (c.type == TSlider) {
                drag_ = k;
                SetCapture(hwnd_);
                setSliderFromX(c, x / scale_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (c.type == TButton) {
                press_ = k;
                SetCapture(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (drag_ >= 0) {
                drag_ = -1;
                ReleaseCapture();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (press_ >= 0) {
                int k = press_;
                press_ = -1;
                ReleaseCapture();
                if (hitTest(x, y) == k) {
                    if (ctls_[k].id == IdShake) {
                        if (cb_.shake) cb_.shake();
                    } else if (ctls_[k].id == IdSearch) {
                        startSearch();
                    } else if (ctls_[k].id == IdDefaults) {
                        Settings d;
                        d.enabled = s_.enabled;
                        d.autostart = s_.autostart;
                        d.games = s_.games;
                        d.pauseGames = s_.pauseGames;
                        d.render = s_.render;
                        d.realWeather = s_.realWeather;   // Ort und Wetter-Wahl bleiben
                        d.place = s_.place;
                        d.placeDetail = s_.placeDetail;
                        d.lat = s_.lat;
                        d.lon = s_.lon;
                        s_ = d;
                        changed();
                    }
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if ((HWND)lp != hwnd_) {
                drag_ = -1;
                press_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            scrollTo(scrollY_ - delta / 120.0f * 48.0f);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd_);
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, theme_->textRef);
            SetBkColor(dc, theme_->editBgRef);
            return (LRESULT)editBrush_;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kPlaceEditId && HIWORD(wp) == kPlaceEnter) {
                startSearch();
                return 0;
            }
            if (LOWORD(wp) == kPlaceEditId && HIWORD(wp) == EN_CHANGE) {
                if (!searchNote_.empty()) {
                    searchNote_.clear();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            }
            if (LOWORD(wp) == kEditId && HIWORD(wp) == EN_CHANGE && !updatingEdit_) {
                int len = GetWindowTextLengthW(edit_);
                std::wstring text(len + 1, L'\0');
                GetWindowTextW(edit_, &text[0], len + 1);
                text.resize(len);
                std::wstring games;
                for (wchar_t ch : text) {
                    if (ch == L'\r') continue;
                    games += ch == L'\n' ? L';' : ch;
                }
                s_.games = games;
                if (cb_.changed) cb_.changed(s_);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kStatusTimer) {
                RECT r{0, 0, (LONG)std::lround(kWidth * scale_), (LONG)std::lround((kHeader - scrollY_) * scale_)};
                if (r.bottom > 0) InvalidateRect(hwnd_, &r, FALSE);
                RECT w2{0, (LONG)std::lround((wxTextY_ - 2 - scrollY_) * scale_), (LONG)std::lround(kWidth * scale_),
                        (LONG)std::lround((wxTextY_ + 40 - scrollY_) * scale_)};
                if (w2.bottom > 0) InvalidateRect(hwnd_, &w2, FALSE);
                return 0;
            }
            break;
        case WM_ENTERSIZEMOVE:
            if (cb_.modalLoop) cb_.modalLoop(true);
            return 0;
        case WM_EXITSIZEMOVE:
            if (cb_.modalLoop) cb_.modalLoop(false);
            return 0;
        case WM_DPICHANGED: {
            scale_ = HIWORD(wp) / 96.0f;
            auto* r = reinterpret_cast<RECT*>(lp);
            createFonts();
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((int)(6 * scale_), (int)(6 * scale_)));
            if (placeEdit_) SendMessageW(placeEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((int)(6 * scale_), (int)(6 * scale_)));
            positionEdit();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                loadTheme();
                applyWindowTheme();
                InvalidateRect(hwnd_, nullptr, FALSE);
                InvalidateRect(edit_, nullptr, TRUE);
                if (placeEdit_) InvalidateRect(placeEdit_, nullptr, TRUE);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kStatusTimer);
            if (drag_ >= 0 || press_ >= 0) ReleaseCapture();
            drag_ = press_ = hover_ = -1;
            return 0;
        case WM_NCDESTROY: {
            freeBackBuffer();
            if (editFont_) DeleteObject(editFont_);
            editFont_ = nullptr;
            if (iconBig_) DestroyIcon(iconBig_);
            if (iconSmall_) DestroyIcon(iconSmall_);
            iconBig_ = iconSmall_ = nullptr;
            fTitle_.reset();
            fCard_.reset();
            fBody_.reset();
            fBodyBold_.reset();
            fSmall_.reset();
            edit_ = nullptr;
            placeEdit_ = nullptr;
            searchNote_.clear();
            HWND h = hwnd_;
            hwnd_ = nullptr;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            return DefWindowProcW(h, msg, wp, lp);
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace app
