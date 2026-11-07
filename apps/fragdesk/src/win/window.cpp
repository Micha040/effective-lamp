// FragDesk – Hauptfenster: Seitenleiste, Seiten, Eingaben, Hintergrundmeldungen, Bildmodus
#include "window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "mobile_host.h"
#include "recorder.h"
#include "../../res/resource.h"

using namespace Gdiplus;

namespace fdw {

using namespace ui;

namespace {

constexpr const wchar_t* kWindowClass = L"FragDeskWindow";
constexpr float kSideW = 232;
constexpr float kMaxContent = 1320;

enum Timer { kTipTimer = 1, kScanTimer = 2, kBusyTimer = 3, kToastTimer = 7, kRecordTimer = 8, kReplayTimer = 9 };

bool windowsDark() {
    DWORD v = 1, size = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                     nullptr, &v, &size) != ERROR_SUCCESS)
        return true;
    return v == 0;
}

bool pngEncoder(CLSID& clsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            clsid = info[i].Clsid;
            return true;
        }
    return false;
}

// Dunkle Kontextmenüs (inoffizielle uxtheme-Funktionen, ab Windows 10 1903; sonst ohne Wirkung)
void setMenuTheme(bool dark) {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    typedef int(WINAPI * SetPreferredAppModeFn)(int);
    typedef void(WINAPI * FlushMenuThemesFn)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
    if (setMode) setMode(dark ? 2 : 3);
    if (flush) flush();
}

class Window {
public:
    Window(HINSTANCE inst, const Options& opt) : inst_(inst), opt_(opt) {}

    bool create();
    int loop();
    bool renderPng();

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

private:
    HINSTANCE inst_;
    Options opt_;
    HWND hwnd_ = nullptr;
    App app_;
    Theme th_;
    Fonts fonts_;
    bool dark_ = true;
    float scale_ = 1;
    UINT dpi_ = 96;

    std::vector<ui::Region> regions_;
    bool loaded_ = false;                 // Bibliothek geladen?
    std::vector<std::wstring> pending_;   // Demos, die vor dem Laden kamen
    void openDemos(const std::vector<std::wstring>& files);
    uint32_t hot_ = 0, pressed_ = 0;
    int hotIndex_ = -1;
    bool tracking_ = false, showTip_ = false;
    POINT mouse_{-1, -1};
    float contentH_ = 0, viewH_ = 0;
    HBITMAP backBmp_ = nullptr;
    HDC backDc_ = nullptr;
    void* backBits_ = nullptr;
    int backW_ = 0, backH_ = 0;
    bool animTimer_ = false;                      // 2D-Ansicht läuft (Timer aktiv)
    std::function<void(float, float)> dragFn_;   // gerade gezogene Fläche (Zeitleiste)

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void updateAnimation();
    void setScale(UINT dpi);
    void applyTheme();
    void paint(Graphics& g, float wPx, float hPx);
    void drawSidebar(Canvas& c, float h);
    void drawOverlays(Canvas& c, float w, float h);
    void onPaint();
    int regionAt(int x, int y) const;
    void updateHover(int x, int y);
    void refreshHover();
    void scrollBy(float dy);
    float& scroll() { return app_.scroll[int(app_.page)]; }
    void startup();
};

// ---------------------------------------------------------------------------------------------------------------

void Window::setScale(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    scale_ = float(dpi_) / 96.0f;
    clearMeasureCache();
}

void Window::applyTheme() {
    const std::string& t = app_.settings.theme;
    dark_ = t == "dark" ? true : t == "light" ? false : windowsDark();
    if (opt_.theme == L"dark") dark_ = true;
    if (opt_.theme == L"light") dark_ = false;
    th_ = makeTheme(dark_);
    if (hwnd_) {
        BOOL d = dark_ ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &d, sizeof d))) DwmSetWindowAttribute(hwnd_, 19, &d, sizeof d);
        COLORREF cap = RGB(th_.side.GetR(), th_.side.GetG(), th_.side.GetB());
        DwmSetWindowAttribute(hwnd_, 35, &cap, sizeof cap);
        COLORREF txt = RGB(th_.text.GetR(), th_.text.GetG(), th_.text.GetB());
        DwmSetWindowAttribute(hwnd_, 36, &txt, sizeof txt);
    }
    setMenuTheme(dark_);
}

void Window::startup() {
    app_.settings.load();
    app_.steam = detectSteam(app_.settings.cs2Dir);
    app_.applyFolders();
    app_.loadClips();
    logLine(L"FragDesk %ls gestartet; Steam: %ls; CS2: %ls; %zu Konten", kVersion, app_.steam.steamDir.c_str(), app_.steam.cs2Dir.c_str(),
            app_.steam.accounts.size());
}

void Window::openDemos(const std::vector<std::wstring>& files) {
    if (files.empty()) return;
    app_.lib.addFiles(files);
    app_.go(Page::Matches);
    app_.notify(files.size() == 1 ? L"Demo wird ausgewertet" : std::to_wstring(files.size()) + L" Demos werden ausgewertet");
    SetTimer(hwnd_, kBusyTimer, 400, nullptr);
}

bool Window::create() {
    startup();
    pending_ = opt_.demos;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &Window::wndProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    auto getDpi = shcore ? reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor"))) : nullptr;
    UINT dx = 96, dy = 96;
    if (getDpi && SUCCEEDED(getDpi(mon, 0, &dx, &dy))) setScale(dx);
    else setScale(96);
    fonts_.init();
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    const RECT& wa = mi.rcWork;
    int w = std::min(int(1440 * scale_), int(wa.right - wa.left - 40 * scale_));
    int h = std::min(int(920 * scale_), int(wa.bottom - wa.top - 40 * scale_));
    int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst_, this);
    if (!hwnd_) return false;
    app_.hwnd = hwnd_;
    app_.lib.attach(hwnd_);
    applyTheme();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    app_.lib.loadAsync();
    app_.mobile->apply();   // Handy-App, falls eingeschaltet
    return true;
}

int Window::loop() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}

bool Window::renderPng() {
    app_.renderMode = true;
    startup();
    setScale(UINT(96 * opt_.renderScale / 100));
    fonts_.init();
    applyTheme();
    app_.lib.loadNow();
    if (!opt_.me.empty()) app_.settings.steamId = narrow(opt_.me);
    app_.detectMe();
    app_.page = opt_.page;
    app_.matchTab = app_.statsTab = app_.highlightsTab = opt_.tab;
    if ((opt_.page == Page::Match || opt_.page == Page::Replay) && !app_.lib.matches.empty()) {
        size_t idx = size_t(std::max(0, opt_.matchIndex)) % app_.lib.matches.size();
        app_.openMatch(app_.lib.matches[idx]->checksum, opt_.page == Page::Replay ? 0 : opt_.tab);
        if (opt_.page == Page::Replay && app_.detail) {
            app_.page = Page::Replay;
            app_.replay.openNow(*app_.detail, opt_.tab);
            if (app_.replay.data) app_.replay.seek(app_.replay.freezeTick + opt_.replayAt * app_.replay.data->tickrate);
        }
    }
    if (!opt_.heat.empty()) {   // Heatmap-Auswahl: Liste wie "kills,deaths,me,t,points"
        uint32_t layers = 0;
        uint8_t who = 0, sides = 0;
        bool throws = false, points = false;
        std::wstring list = opt_.heat + L",";
        for (size_t a = 0, b; (b = list.find(L',', a)) != std::wstring::npos; a = b + 1) {
            std::wstring t = list.substr(a, b - a);
            const wchar_t* names[] = {L"presence", L"kills", L"deaths", L"smoke", L"molly", L"flash", L"he"};
            for (int i = 0; i < fd::HlCount; ++i)
                if (t == names[i]) layers |= 1u << i;
            if (t == L"me") who |= fd::WhoMe;
            if (t == L"team") who |= fd::WhoTeam;
            if (t == L"enemy") who |= fd::WhoEnemy;
            if (t == L"t") sides |= fd::SideT;
            if (t == L"ct") sides |= fd::SideCT;
            if (t == L"throws") throws = true;
            if (t == L"points") points = true;
        }
        for (HeatView* hv : {&app_.matchHeat, &app_.statsHeat}) {
            if (layers) hv->layers = layers;
            if (who) hv->filter.who = who;
            if (sides) hv->filter.sides = sides;
            hv->throws = throws;
            hv->marks = points;
        }
    }
    if (opt_.selectCount > 0)   // Momente auswählen (für Bilder der Auswahl)
        for (const auto& m : app_.lib.matches)
            for (const auto& h : m->highlights)
                if (h.player == app_.me && app_.selected.size() < size_t(opt_.selectCount)) app_.selected.insert(m->checksum + "/" + h.id);
    // Demo-Liste für Bilder: Dateien aus dem Datenordner "demos"
    app_.lib.setFolders({{joinPath(dataDir(), L"demos"), L"Downloads"}});
    int px = int(std::lround(opt_.renderWidth * scale_)), py = int(std::lround(opt_.renderHeight * scale_));
    Bitmap bmp(px, py, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        paint(g, float(px), float(py));
    }
    if (opt_.renderScroll > 0) {   // zweiter Durchgang mit Scrollposition
        scroll() = opt_.renderScroll;
        Graphics g(&bmp);
        paint(g, float(px), float(py));
    }
    CLSID png;
    return pngEncoder(png) && bmp.Save(opt_.renderPath.c_str(), &png, nullptr) == Ok;
}

// ---- Zeichnen -------------------------------------------------------------------------------------------------
void Window::drawSidebar(Canvas& c, float h) {
    c.fill(RectF(0, 0, kSideW, h), th_.side);
    c.line(kSideW - 0.5f, 0, kSideW - 0.5f, h, alpha(th_.border, 200), 1);
    // Logo
    RectF logo(22, 24, 38, 38);
    c.round(logo, 10, th_.accent);
    c.icon(Icon::Crosshair, RectF(logo.X + 8, logo.Y + 8, 22, 22), th_.onAccent, 2.2f);
    c.text(L"FragDesk", F::H2, th_.text, RectF(70, 22, 150, 24));
    c.text(L"efficient lamp", F::Tiny, th_.text3, RectF(70, 44, 150, 16));

    struct Nav {
        Page page;
        const wchar_t* label;
        Icon icon;
    } nav[] = {
        {Page::Dashboard, L"Übersicht", Icon::Home},
        {Page::Matches, L"Matches", Icon::List},
        {Page::Stats, L"Statistiken", Icon::Stats},
        {Page::Highlights, L"Momente", Icon::Film},
        {Page::Settings, L"Einstellungen", Icon::Gear},
    };
    float y = 96;
    Page current = app_.page == Page::Match || app_.page == Page::Replay ? app_.backPage : app_.page;
    for (const auto& n : nav) {
        RectF r(12, y, kSideW - 24, 42);
        uint32_t id = hashId(std::string("nav:") + std::to_string(int(n.page)));
        bool sel = current == n.page;
        if (sel) c.round(r, 10, th_.dark ? th_.card2 : th_.card);
        else if (c.hot(id)) c.round(r, 10, alpha(th_.hover, 200));
        if (sel) c.round(RectF(r.X, r.Y + 11, 3, 20), 1.5f, th_.accent);
        c.icon(n.icon, RectF(r.X + 14, r.Y + 11, 20, 20), sel ? th_.accentText : th_.text2, 1.7f);
        c.text(n.label, sel ? F::BodySemi : F::Body, sel ? th_.text : th_.text2, RectF(r.X + 46, r.Y, r.Width - 90, r.Height));
        if (n.page == Page::Matches && !app_.lib.demos.empty()) {
            std::wstring cnt = std::to_wstring(app_.lib.demos.size());
            float pw = std::max(22.0f, c.pillWidth(cnt));
            c.pill(RectF(r.X + r.Width - pw - 10, r.Y + 11, pw, 20), cnt, th_.accent, th_.onAccent);
        }
        if (n.page == Page::Highlights && app_.recording()) c.circle(r.X + r.Width - 20, r.Y + 21, 5, th_.danger);
        Page p = n.page;
        c.region(r, id, [this, p] {
            app_.go(p);
        });
        y += 46;
    }

    // Status der Auswertung
    float by = h - 92;
    if (app_.lib.busy()) {
        RectF r(12, by - 52, kSideW - 24, 44);
        c.round(r, 10, alpha(th_.accent, th_.dark ? 26 : 40));
        float t = float(GetTickCount64() % 1200) / 1200.0f;
        c.ring(r.X + 22, r.Y + 22, 7, alpha(th_.accentText, 80), 2);
        {
            Pen pen(th_.accentText, 2);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            c.g.DrawArc(&pen, r.X + 15, r.Y + 15, 14.0f, 14.0f, t * 360, 100);
        }
        int q = app_.lib.queued();
        c.text(q > 1 ? L"Wertet aus … (" + std::to_wstring(q) + L")" : L"Wertet aus …", F::SmallSemi, th_.text, RectF(r.X + 40, r.Y, r.Width - 48, r.Height));
    }
    // Konto
    RectF acc(12, by, kSideW - 24, 76);
    uint32_t aid = hashId("account");
    c.round(acc, 12, c.hot(aid) ? th_.hover : (th_.dark ? th_.card : th_.card));
    c.roundStroke(acc, 12, th_.border, 1);
    std::wstring name = app_.meName.empty() ? L"Kein Konto" : app_.meName;
    c.circle(acc.X + 30, acc.Y + 38, 18, th_.btn);
    std::wstring initial = name.substr(0, 1);
    c.text(initial, F::H3, th_.text, RectF(acc.X + 12, acc.Y + 20, 36, 36), Align::Center);
    c.text(name, F::BodySemi, th_.text, RectF(acc.X + 58, acc.Y + 14, acc.Width - 66, 22));
    const fd::Career& car = app_.career();
    if (car.premierCurrent > 0)
        c.text(widen(fd::fmtRating(car.premierCurrent)) + L" Premier", F::SmallSemi, rgb(fd::premierColor(car.premierCurrent)),
               RectF(acc.X + 58, acc.Y + 38, acc.Width - 66, 20));
    else
        c.text(app_.me.empty() ? L"In Einstellungen wählen" : L"Kein Premier-Rating", F::Small, th_.text3, RectF(acc.X + 58, acc.Y + 38, acc.Width - 66, 20));
    c.region(acc, aid, [this] { app_.go(Page::Settings); }, L"Konto und Einstellungen");
}

void Window::drawOverlays(Canvas& c, float w, float h) {
    // Toast
    if (!app_.toast.empty() && (app_.renderMode || GetTickCount64() < app_.toastUntil)) {
        float tw = std::min(c.measure(app_.toast, F::BodySemi) + 64, w - kSideW - 80);
        RectF r(kSideW + (w - kSideW - tw) / 2, h - 76, tw, 46);
        c.round(RectF(r.X, r.Y + 3, r.Width, r.Height), 12, Color(70, 0, 0, 0));
        c.round(r, 12, th_.dark ? rgb(0x2A3342) : rgb(0x1F2937));
        c.icon(app_.toastError ? Icon::Warning : Icon::Info, RectF(r.X + 16, r.Y + 14, 18, 18), app_.toastError ? th_.danger : th_.accent, 1.7f);
        c.text(app_.toast, F::BodySemi, rgb(0xF3F5F9), RectF(r.X + 44, r.Y, r.Width - 56, r.Height));
    }
    // Tooltip
    if (showTip_ && hotIndex_ >= 0 && hotIndex_ < int(regions_.size()) && !regions_[hotIndex_].tip.empty()) {
        const std::wstring& tip = regions_[hotIndex_].tip;
        float mx = mouse_.x / scale_, my = mouse_.y / scale_;
        float maxW = 360;
        float tw = 0;
        size_t start = 0;
        int lines = 0;
        while (start <= tip.size()) {
            size_t nl = tip.find(L'\n', start);
            std::wstring line = tip.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            tw = std::max(tw, c.measure(line, F::Small));
            ++lines;
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
        tw = std::min(tw, maxW);
        float th = c.textHeight(tip, F::Small, tw + 2) + 2;
        RectF r(mx + 14, my + 18, tw + 22, th + 16);
        if (r.X + r.Width > w - 8) r.X = mx - r.Width - 10;
        if (r.Y + r.Height > h - 8) r.Y = my - r.Height - 10;
        c.round(RectF(r.X, r.Y + 2, r.Width, r.Height), 8, Color(60, 0, 0, 0));
        c.round(r, 8, th_.dark ? rgb(0x2A3342) : rgb(0x1F2937));
        c.text(tip, F::Small, rgb(0xF3F5F9), RectF(r.X + 11, r.Y + 8, tw + 2, th), Align::Left, false, true);
        (void)lines;
    }
}

void Window::paint(Graphics& g, float wPx, float hPx) {
    regions_.clear();
    float w = wPx / scale_, h = hPx / scale_;
    {
        SolidBrush bg(th_.bg);
        g.FillRectangle(&bg, 0.0f, 0.0f, wPx, hPx);
    }
    Canvas c(g, th_, fonts_, scale_, regions_, hot_, pressed_);
    drawSidebar(c, h);

    // Inhalt
    float areaX = kSideW, areaW = w - kSideW;
    bool full = app_.page == Page::Replay;   // die 2D-Ansicht nutzt die ganze Breite und Höhe
    float pageW = full ? areaW : std::min(areaW, kMaxContent);
    float pageX = areaX + std::max(0.0f, (areaW - pageW) / 2);
    viewH_ = h;
    float& sc = scroll();
    sc = std::max(0.0f, std::min(sc, std::max(0.0f, contentH_ - viewH_)));
    c.clip(RectF(areaX, 0, areaW, h));
    c.translate(pageX, -sc);
    float ch = 0;
    switch (app_.page) {
        case Page::Dashboard: ch = drawDashboard(c, app_, pageW); break;
        case Page::Matches: ch = drawMatches(c, app_, pageW); break;
        case Page::Match: ch = drawMatch(c, app_, pageW); break;
        case Page::Stats: ch = drawStats(c, app_, pageW); break;
        case Page::Highlights: ch = drawHighlights(c, app_, pageW); break;
        case Page::Replay: ch = drawReplay(c, app_, pageW, h); break;
        default: ch = drawSettings(c, app_, pageW); break;
    }
    c.translate(-pageX, sc);
    c.unclip();
    bool changed = std::fabs(ch - contentH_) > 0.5f;
    contentH_ = ch;
    // Scrollanzeige
    if (contentH_ > viewH_ + 1) {
        float trackH = viewH_ - 16;
        float barH = std::max(40.0f, trackH * viewH_ / contentH_);
        float barY = 8 + (trackH - barH) * (sc / (contentH_ - viewH_));
        c.round(RectF(w - 9, barY, 5, barH), 2.5f, alpha(th_.text3, 110));
    }
    drawOverlays(c, w, h);
    if (changed && hwnd_ && sc > std::max(0.0f, contentH_ - viewH_)) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Window::onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = std::max(1L, rc.right - rc.left), h = std::max(1L, rc.bottom - rc.top);
    if (!backDc_ || w != backW_ || h != backH_) {
        if (backDc_) DeleteDC(backDc_);
        if (backBmp_) DeleteObject(backBmp_);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        backBmp_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &backBits_, nullptr, 0);
        backDc_ = CreateCompatibleDC(dc);
        SelectObject(backDc_, backBmp_);
        backW_ = w;
        backH_ = h;
    }
    {
        Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(backBits_));
        Graphics g(&bmp);
        paint(g, float(w), float(h));
    }
    BitBlt(dc, 0, 0, w, h, backDc_, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
    refreshHover();
    updateAnimation();
}

// Nach dem Zeichnen gibt es neue Flächen (z. B. nach einem Seitenwechsel ohne Mausbewegung): Fläche unter der Maus neu
// bestimmen, sonst zeigt der Tooltip den Text einer Fläche, die zufällig an derselben Stelle der alten Liste stand
void Window::refreshHover() {
    if (hot_ == 0 && hotIndex_ < 0) return;
    int i = regionAt(mouse_.x, mouse_.y);
    uint32_t id = i >= 0 ? regions_[i].id : 0;
    if (id == hot_) {
        hotIndex_ = i;
        return;
    }
    updateHover(mouse_.x, mouse_.y);
}

// 2D-Ansicht: Timer nur, solange abgespielt wird und das Fenster zu sehen ist (spart Rechenzeit)
void Window::updateAnimation() {
    bool want = app_.page == Page::Replay && app_.replay.playing && !IsIconic(hwnd_);
    if (want && !animTimer_) {
        SetTimer(hwnd_, kReplayTimer, 33, nullptr);   // ~30 Bilder pro Sekunde
        animTimer_ = true;
    } else if (!want && animTimer_) {
        KillTimer(hwnd_, kReplayTimer);
        animTimer_ = false;
    }
}

// ---- Eingaben -------------------------------------------------------------------------------------------------
int Window::regionAt(int x, int y) const {
    for (int i = int(regions_.size()) - 1; i >= 0; --i) {
        const RECT& r = regions_[i].px;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

void Window::updateHover(int x, int y) {
    mouse_ = {x, y};
    int i = regionAt(x, y);
    uint32_t id = i >= 0 ? regions_[i].id : 0;
    if (id != hot_ || i != hotIndex_) {
        bool hadTip = showTip_;
        hot_ = id;
        hotIndex_ = i;
        showTip_ = false;
        KillTimer(hwnd_, kTipTimer);
        if (i >= 0 && !regions_[i].tip.empty()) SetTimer(hwnd_, kTipTimer, 450, nullptr);
        (void)hadTip;
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (showTip_) {
        InvalidateRect(hwnd_, nullptr, FALSE);   // Tooltip folgt der Maus
    }
}

void Window::scrollBy(float dy) {
    float& sc = scroll();
    float max = std::max(0.0f, contentH_ - viewH_);
    float next = std::max(0.0f, std::min(max, sc + dy));
    if (next != sc) {
        sc = next;
        showTip_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        // Hover nach dem Neuzeichnen neu bestimmen
        PostMessageW(hwnd_, WM_MOUSEMOVE, 0, MAKELPARAM(mouse_.x, mouse_.y));
    }
}

LRESULT CALLBACK Window::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: onPaint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = LONG(1000 * scale_);
            mm->ptMinTrackSize.y = LONG(640 * scale_);
            return 0;
        }
        case WM_DPICHANGED: {
            setScale(HIWORD(wp));
            fonts_.init();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            applyTheme();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&t);
            }
            if (dragFn_ && (wp & MK_LBUTTON)) {
                dragFn_(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
                mouse_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            updateHover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            hot_ = 0;
            hotIndex_ = -1;
            showTip_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, hotIndex_ >= 0 && hotIndex_ < int(regions_.size()) && regions_[hotIndex_].hand ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetCapture(hwnd_);
            updateHover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            pressed_ = hot_;
            showTip_ = false;
            dragFn_ = nullptr;
            if (hotIndex_ >= 0 && hotIndex_ < int(regions_.size()) && regions_[hotIndex_].drag) {
                dragFn_ = regions_[hotIndex_].drag;
                dragFn_(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            dragFn_ = nullptr;
            int i = regionAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            uint32_t was = pressed_;
            pressed_ = 0;
            if (i >= 0 && regions_[i].id == was && regions_[i].click) {
                auto fn = regions_[i].click;   // Kopie: das Zeichnen danach ersetzt die Flächen
                Page before = app_.page;
                fn();
                if (app_.page != before) {
                    hot_ = 0;
                    hotIndex_ = -1;
                }
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(wp) == XBUTTON1 && app_.page == Page::Match) app_.go(app_.backPage);
            else if (GET_XBUTTON_WPARAM(wp) == XBUTTON1 && app_.page == Page::Replay) app_.go(Page::Match);
            return TRUE;
        case WM_MOUSEWHEEL:
            scrollBy(-float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 72.0f);
            return 0;
        case WM_KEYDOWN:
            if (app_.page == Page::Replay) {   // 2D-Ansicht: Abspielen, Springen, Runden
                ReplayView& rv = app_.replay;
                double tr = rv.data ? rv.data->tickrate : 64;
                bool handled = true;
                switch (wp) {
                    case VK_SPACE: rv.togglePlay(); break;
                    case VK_LEFT: rv.seek(rv.tick - 5 * tr); break;
                    case VK_RIGHT: rv.seek(rv.tick + 5 * tr); break;
                    case VK_UP:
                    case VK_DOWN:
                    case VK_PRIOR:
                    case VK_NEXT:
                        if (app_.detail && rv.data) rv.stepRound(*app_.detail, wp == VK_UP || wp == VK_PRIOR ? -1 : 1);
                        break;
                    case VK_ESCAPE: app_.go(Page::Match); break;
                    default: handled = false;
                }
                if (handled) {
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return 0;
                }
            }
            switch (wp) {
                case VK_ESCAPE:
                    if (app_.page == Page::Match) app_.go(app_.backPage);
                    return 0;
                case VK_PRIOR: scrollBy(-viewH_ * 0.85f); return 0;
                case VK_NEXT: scrollBy(viewH_ * 0.85f); return 0;
                case VK_HOME: scrollBy(-1e9f); return 0;
                case VK_END: scrollBy(1e9f); return 0;
                case VK_UP: scrollBy(-48); return 0;
                case VK_DOWN: scrollBy(48); return 0;
                case VK_F5: app_.rescan(); return 0;
            }
            break;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH * 2];
                if (DragQueryFileW(drop, i, buf, MAX_PATH * 2)) files.push_back(buf);
            }
            DragFinish(drop);
            if (loaded_) openDemos(files);
            else pending_.insert(pending_.end(), files.begin(), files.end());
            return 0;
        }
        case WM_TIMER:
            switch (wp) {
                case kTipTimer:
                    KillTimer(hwnd_, kTipTimer);
                    showTip_ = true;
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                case kScanTimer:
                    KillTimer(hwnd_, kScanTimer);
                    app_.lib.scanAsync();
                    break;
                case kBusyTimer:
                    if (!app_.lib.busy()) KillTimer(hwnd_, kBusyTimer);
                    if (!IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                case kToastTimer:
                    if (GetTickCount64() >= app_.toastUntil) {
                        KillTimer(hwnd_, kToastTimer);
                        app_.toast.clear();
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                case kReplayTimer:
                    if (app_.page != Page::Replay || !app_.replay.advance() || IsIconic(hwnd_)) {
                        KillTimer(hwnd_, kReplayTimer);
                        animTimer_ = false;
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                case kRecordTimer:
                    if (!app_.recording()) KillTimer(hwnd_, kRecordTimer);
                    if (!IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                case kMobileTimer:   // Restzeit des Kopplungscodes
                    if (app_.mobile->pairingCode().empty()) KillTimer(hwnd_, kMobileTimer);
                    if (app_.page == Page::Settings && !IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
            }
            return 0;
        case WM_APP_LIBRARY:
            app_.lib.onLoaded(reinterpret_cast<LibraryLoad*>(lp));
            app_.detectMe();
            app_.lib.startWatching();
            app_.lib.scanAsync();
            loaded_ = true;
            if (!pending_.empty()) {   // beim Start übergebene Demos
                openDemos(pending_);
                pending_.clear();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_COPYDATA: {   // zweiter Start mit Demos: die laufende FragDesk übernimmt sie
            const auto* cd = reinterpret_cast<const COPYDATASTRUCT*>(lp);
            if (!cd || cd->dwData != 1 || !cd->lpData || cd->cbData < sizeof(wchar_t) || cd->cbData > 64 * 1024) return FALSE;
            const auto* text = static_cast<const wchar_t*>(cd->lpData);
            std::wstring path(text, wcsnlen(text, cd->cbData / sizeof(wchar_t)));
            if (path.empty()) return FALSE;
            if (loaded_) openDemos({path});
            else pending_.push_back(path);
            return TRUE;
        }
        case WM_APP_DEMOS:
            app_.lib.onScanned(reinterpret_cast<ScanResult*>(lp));
            if (app_.settings.autoAnalyze && !app_.lib.demos.empty()) {
                app_.lib.analyzeAll(false);   // fehlgeschlagene nicht bei jeder Ordneränderung erneut versuchen
                SetTimer(hwnd_, kBusyTimer, 400, nullptr);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_APP_ANALYSIS: {
            auto* e = reinterpret_cast<AnalysisEvent*>(lp);
            AnalysisEvent::Kind kind = e->kind;
            std::wstring text = e->text, path = e->path;
            std::wstring title = e->match ? matchTitle(*e->match, app_.me) : L"";
            std::string checksum = e->match ? e->match->checksum : std::string();
            app_.lib.onEvent(e);
            if (kind == AnalysisEvent::Done) {
                if (app_.me.empty()) app_.detectMe();
                app_.invalidateCareer();
                if (!checksum.empty() && checksum == app_.matchId) app_.refreshDetail();   // neu ausgewertet, während es offen ist
                app_.notify(L"Ausgewertet: " + title);
            } else if (kind == AnalysisEvent::Failed) {
                app_.notify(fileName(path) + L": " + text, true);
            }
            if (app_.lib.busy()) SetTimer(hwnd_, kBusyTimer, 400, nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_FOLDER:
            SetTimer(hwnd_, kScanTimer, 1500, nullptr);
            return 0;
        case WM_APP_MOBILE:
            return app_.mobile->onMessage(wp, lp);
        case WM_APP_HEAT: {
            auto* e = reinterpret_cast<HeatEvent*>(lp);
            if (!app_.statsHeat.apply(e)) app_.matchHeat.apply(e);
            delete e;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_REPLAY: {
            auto* e = reinterpret_cast<ReplayEvent*>(lp);
            const fd::Match* m = app_.detail && e && app_.detail->checksum == e->match ? app_.detail.get() : nullptr;
            app_.replay.apply(e, m);
            delete e;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_APP_RECORD: {
            std::unique_ptr<RecordEvent> e(reinterpret_cast<RecordEvent*>(lp));
            switch (e->kind) {
                case RecordEvent::Status: SetTimer(hwnd_, kRecordTimer, 500, nullptr); break;
                case RecordEvent::ClipDone: {
                    Clip cl;
                    cl.path = e->job.out;
                    cl.title = e->job.title;
                    cl.match = e->job.match;
                    cl.highlight = e->job.highlight;
                    cl.map = e->job.map;
                    FILETIME ft;
                    GetSystemTimeAsFileTime(&ft);
                    ULARGE_INTEGER u;
                    u.LowPart = ft.dwLowDateTime;
                    u.HighPart = ft.dwHighDateTime;
                    cl.created = int64_t((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
                    cl.size = fileSize(cl.path);
                    cl.seconds = e->seconds;
                    app_.clips.erase(std::remove_if(app_.clips.begin(), app_.clips.end(), [&](const Clip& x) { return samePath(x.path, cl.path); }),
                                     app_.clips.end());
                    app_.clips.insert(app_.clips.begin(), cl);
                    app_.saveClips();
                    app_.notify(L"Video gespeichert: " + cl.title);
                    break;
                }
                case RecordEvent::Finished: app_.notify(e->text); break;
                case RecordEvent::Failed:
                    app_.recordError = e->text;   // bleibt unter "Momente" stehen, bis es weggeklickt wird
                    app_.notify(e->text, true);
                    break;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            if (app_.recording() &&
                MessageBoxW(hwnd_, L"Es läuft noch eine Aufnahme. Trotzdem beenden?", L"FragDesk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 0;
            break;
        case WM_DESTROY:
            app_.mobile->stop();
            app_.cancelRecording();
            releaseInstanceLock();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace

int runApp(HINSTANCE inst, const Options& opt) {
    Window w(inst, opt);
    if (!opt.renderPath.empty()) return w.renderPng() ? 0 : 1;
    if (!w.create()) return 1;
    return w.loop();
}

}  // namespace fdw
