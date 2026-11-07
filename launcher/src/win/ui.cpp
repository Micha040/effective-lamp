// efficient lamp – Launcher-Fenster (siehe ui.h)
#include "ui.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
// GDI+ erwartet min/max – lokal bereitstellen (NOMINMAX ist gesetzt)
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include "../../res/resource.h"
#include "../core/catalog.h"
#include "../core/sha256.h"
#include "common.h"
#include "jobs.h"
#include "net.h"
#include "selfinstall.h"
#include "system.h"

namespace lamp {

using namespace Gdiplus;

namespace {

const wchar_t* kWindowClass = L"EfficientLampWindow";

// ------------------------------------------------------------------ Farben

Color rgb(uint32_t hex, BYTE a = 255) { return Color(a, BYTE(hex >> 16), BYTE(hex >> 8), BYTE(hex)); }
Color withAlpha(const Color& c, BYTE a) { return Color(a, c.GetR(), c.GetG(), c.GetB()); }

struct Theme {
    bool dark = true;
    Color bg, card, cardHover, border, text, text2, text3;
    Color accent, accentHover, accentPressed, onAccent;
    Color btn, btnHover, btnPressed;
    Color success, danger, track;
};

Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.bg = rgb(0x0F141B);
        t.card = rgb(0x161C25);
        t.cardHover = rgb(0x1A212C);
        t.border = rgb(0x242D3A);
        t.text = rgb(0xE9EEF5);
        t.text2 = rgb(0x9AA6B7);
        t.text3 = rgb(0x6C788A);
        t.accent = rgb(0xFFB547);
        t.accentHover = rgb(0xFFC56E);
        t.accentPressed = rgb(0xEFA132);
        t.onAccent = rgb(0x1E1404);
        t.btn = rgb(0x232B37);
        t.btnHover = rgb(0x2B3542);
        t.btnPressed = rgb(0x1E252F);
        t.success = rgb(0x4ADE80);
        t.danger = rgb(0xF87171);
        t.track = rgb(0x262F3C);
    } else {
        t.bg = rgb(0xF3F5F9);
        t.card = rgb(0xFFFFFF);
        t.cardHover = rgb(0xFBFCFE);
        t.border = rgb(0xE1E6EE);
        t.text = rgb(0x111827);
        t.text2 = rgb(0x536175);
        t.text3 = rgb(0x8592A5);
        t.accent = rgb(0xFFB547);
        t.accentHover = rgb(0xFFC56E);
        t.accentPressed = rgb(0xF0A12F);
        t.onAccent = rgb(0x1E1404);
        t.btn = rgb(0xEDF0F5);
        t.btnHover = rgb(0xE3E8EF);
        t.btnPressed = rgb(0xD8DEE7);
        t.success = rgb(0x15A34A);
        t.danger = rgb(0xDC2626);
        t.track = rgb(0xE5E9F0);
    }
    return t;
}

// Textfarbe für Akzent-Text auf hellem Grund etwas dunkler, damit er lesbar bleibt
Color accentText(const Theme& t) { return t.dark ? t.accent : rgb(0xB86E00); }

// ------------------------------------------------------------------ Zeichenhilfen

void roundRect(GraphicsPath& p, const RectF& r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    roundRect(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void strokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w) {
    GraphicsPath p;
    roundRect(p, r, rad);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

std::unique_ptr<FontFamily> pickFamily(std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* n : names) {
        auto f = std::make_unique<FontFamily>(n);
        if (f->GetLastStatus() == Ok && f->IsAvailable()) return f;
    }
    return std::unique_ptr<FontFamily>(FontFamily::GenericSansSerif()->Clone());
}

Bitmap* loadPngResource(HINSTANCE inst, int id) {
    HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return nullptr;
    HGLOBAL h = LoadResource(inst, r);
    const void* data = h ? LockResource(h) : nullptr;
    DWORD size = SizeofResource(inst, r);
    if (!data || !size) return nullptr;
    IStream* s = SHCreateMemStream(static_cast<const BYTE*>(data), size);
    if (!s) return nullptr;
    Bitmap* src = Bitmap::FromStream(s);
    Bitmap* out = nullptr;
    if (src && src->GetLastStatus() == Ok) {   // in ein eigenes Bild kopieren, damit der Stream freigegeben werden kann
        out = new Bitmap(int(src->GetWidth()), int(src->GetHeight()), PixelFormat32bppPARGB);
        Graphics g(out);
        g.DrawImage(src, 0, 0, int(src->GetWidth()), int(src->GetHeight()));
    }
    delete src;
    s->Release();
    return out;
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

std::wstring timeNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[16];
    swprintf(b, 16, L"%02u:%02u", st.wHour, st.wMinute);
    return b;
}

std::wstring fileTimeText(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return std::wstring();
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&fa.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t b[32];
    swprintf(b, 32, L"%02u.%02u. %02u:%02u", st.wDay, st.wMonth, st.wHour, st.wMinute);
    return b;
}

// Dunkle Kontextmenüs (inoffizielle uxtheme-Funktionen, ab Windows 10 1903; sonst ohne Wirkung)
void setMenuTheme(bool dark) {
    static int build = -1;
    if (build < 0) {
        build = 0;
        typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
        OSVERSIONINFOW vi = {};
        vi.dwOSVersionInfoSize = sizeof vi;
        if (fn && fn(&vi) == 0 && vi.dwMajorVersion >= 10) build = int(vi.dwBuildNumber);
    }
    if (build < 18362) return;
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    typedef int(WINAPI * SetPreferredAppModeFn)(int);
    typedef void(WINAPI * FlushMenuThemesFn)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
    if (setMode) setMode(dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    if (flush) flush();
}

// ------------------------------------------------------------------ Daten

struct CatalogResult {
    bool ok = false;
    std::string text;
    std::wstring error;
};

struct SelfUpdateResult {
    bool ok = false;
    std::string data;
    std::wstring error;
};

struct AppView {
    cat::App app;
    InstallInfo inst;
    bool running = false;
    bool busy = false;
    JobKind kind = JobKind::Install;
    JobPhase phase = JobPhase::Queued;
    double progress = -1;
    std::wstring error;
    std::wstring note;          // kurze Erfolgsmeldung
    ULONGLONG noteUntil = 0;

    bool updateAvailable() const {
        return inst.installed && !inst.version.empty() && cat::compareVersions(app.version, narrow(inst.version)) > 0;
    }
};

enum class HitKind { None, Primary, More, UpdateAll, Refresh, Menu, BannerAction, BannerClose, Footer, Retry };

struct Hit {
    RectF rc;
    HitKind kind = HitKind::None;
    int index = -1;
    bool enabled = true;
};

enum class BannerKind { None, Welcome, Updated, LauncherUpdate, Updating, Error };

enum MenuId { kMenuOpen = 1, kMenuReadme, kMenuFolder, kMenuReinstall, kMenuUninstall, kMenuInstall, kMenuCheck, kMenuGithub, kMenuLog, kMenuAbout, kMenuRemoveLauncher };

// ------------------------------------------------------------------ Fenster

class Ui {
public:
    Ui(HINSTANCE inst, const Options& opt) : inst_(inst), opt_(opt) {
        catalogUrl_ = opt.catalogUrl.empty() ? std::wstring(kCatalogUrl) : opt.catalogUrl;
        dark_ = opt.theme == -1 ? systemUsesDarkMode() : opt.theme == 1;
        theme_ = makeTheme(dark_);
        logo_.reset(loadPngResource(inst, IDR_LOGO));
        const std::pair<const char*, int> tiles[] = {
            {"deskcats", IDR_TILE_DESKCATS},           {"snowdesk", IDR_TILE_SNOWDESK},         {"triggerswitch", IDR_TILE_TRIGGERSWITCH},
            {"wavemutesync", IDR_TILE_WAVEMUTESYNC}, {"fragdesk", IDR_TILE_FRAGDESK}};
        for (const auto& t : tiles) tiles_[t.first].reset(loadPngResource(inst, t.second));
        if (opt.welcome) setBanner(BannerKind::Welcome, L"efficient lamp ist eingerichtet – du findest es ab jetzt im Startmenü.");
        if (opt.updated) setBanner(BannerKind::Updated, std::wstring(L"Der Launcher ist jetzt auf Version ") + kVersion + L".");
    }

    ~Ui() {
        if (jobs_->stop(1500)) delete jobs_;   // sonst bewusst stehen lassen: der Thread endet mit dem Prozess
    }

    bool create() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Ui::wndProc;
        wc.hInstance = inst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_APP));
        wc.hIconSm = static_cast<HICON>(LoadImageW(inst_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        // Größe passend zur DPI des Bildschirms, auf dem das Fenster erscheint
        POINT pt;
        GetCursorPos(&pt);
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        UINT dpi = 96;
        typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        auto getDpi = shcore ? reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor"))) : nullptr;
        UINT dx = 96, dy = 96;
        if (getDpi && SUCCEEDED(getDpi(mon, 0, &dx, &dy))) dpi = dx;
        setScale(dpi);
        MONITORINFO mi = {};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(mon, &mi);
        loadCachedCatalog();   // Höhe passend zur Zahl der Programme (beim allerersten Start: 4)
        size_t n = apps_.empty() ? 4 : apps_.size();
        float contentH = 92 + (banner_ != BannerKind::None ? 60 : 0) + (2 + n * 100.0f + (n - 1) * 12.0f + 10) + 44;
        RECT r = {0, 0, px(760), px(std::max(420.0f, contentH))};
        typedef BOOL(WINAPI * AdjustForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto adjust = reinterpret_cast<AdjustForDpiFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi")));
        if (!adjust || !adjust(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi)) AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
        int w = r.right - r.left, h = r.bottom - r.top;
        const RECT& wa = mi.rcWork;
        h = std::min<int>(h, wa.bottom - wa.top - px(16));
        int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
        hwnd_ = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst_, this);
        if (!hwnd_) return false;
        applyWindowTheme();
        jobs_->start(hwnd_);
        if (banner_ == BannerKind::Welcome || banner_ == BannerKind::Updated) SetTimer(hwnd_, kTimerBanner, 12000, nullptr);
        refreshStates();
        fetchCatalog();
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
        SetTimer(hwnd_, kTimerPoll, 3000, nullptr);
        POINT origin = {0, 0};
        ClientToScreen(hwnd_, &origin);
        logLine(L"Fenster offen: Innenbereich ab %ld,%ld, Maßstab %u%%", origin.x, origin.y, unsigned(dpi_ * 100 / 96));
        return true;
    }

    int loop() {
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        return 0;
    }

    // Für Tests/README: ohne Fenster in eine PNG-Datei zeichnen
    bool renderPng(const std::wstring& path, int w, int h, int scalePct) {
        setScale(UINT(96 * scalePct / 100));
        if (!opt_.catalogUrl.empty()) {
            std::string text;
            std::wstring err;
            if (download(catalogUrl_, text, err)) applyCatalogText(text, false, err);
            if (!err.empty()) catalogError_ = err;
        } else {
            loadCachedCatalog();
        }
        catalogState_ = apps_.empty() ? CatalogState::Failed : CatalogState::Ok;
        checkedAt_ = L"12:00";
        refreshStates();
        Bitmap bmp(px(float(w)), px(float(h)), PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            paint(g, float(bmp.GetWidth()), float(bmp.GetHeight()));
        }
        CLSID png;
        return pngEncoder(png) && bmp.Save(path.c_str(), &png, nullptr) == Ok;
    }

private:
    enum { kTimerPoll = 1, kTimerAnim = 2, kTimerBanner = 3 };
    enum class CatalogState { Loading, Ok, Failed };

    HINSTANCE inst_;
    Options opt_;
    HWND hwnd_ = nullptr;
    std::wstring catalogUrl_;
    bool dark_ = true;
    Theme theme_;
    float scale_ = 1;
    UINT dpi_ = 96;

    std::unique_ptr<Bitmap> logo_;
    std::map<std::string, std::unique_ptr<Bitmap>> tiles_;
    std::map<std::string, std::unique_ptr<Bitmap>> scaled_;   // Bilder in Zielgröße (je DPI einmal berechnet)
    std::unique_ptr<FontFamily> famText_, famSemi_;
    bool semiIsBold_ = false;
    std::unique_ptr<Font> fWord_, fTitle_, fBody_, fSmall_, fSmallBold_, fButton_, fEmpty_;

    cat::Catalog catalog_;
    std::vector<AppView> apps_;
    CatalogState catalogState_ = CatalogState::Loading;
    std::wstring catalogError_;
    bool catalogOffline_ = false;   // Fehler beim Laden (true) oder beim Lesen (false)
    bool catalogTooNew_ = false;    // Programmliste in einem Format, das dieser Launcher nicht kennt
    std::wstring checkedAt_;
    std::wstring cacheTime_;
    bool fetching_ = false;
    bool minimized_ = false;

    BannerKind banner_ = BannerKind::None;
    std::wstring bannerText_;
    bool selfUpdating_ = false;

    Jobs* jobs_ = new Jobs();
    std::vector<Hit> hits_;
    std::vector<RectF> cardRects_;   // Kartenflächen der letzten Zeichnung (Index = Programm)
    float viewTop_ = 0, viewBottom_ = 0;
    int hover_ = -1, pressed_ = -1, hoverCard_ = -1;
    HitKind focusKind_ = HitKind::None;
    int focusIndex_ = -1;
    bool keyboardFocus_ = false;
    bool tracking_ = false;
    float scrollY_ = 0, contentH_ = 0, viewH_ = 0;
    float animT_ = 0;
    HBITMAP backBmp_ = nullptr;
    HDC backDc_ = nullptr;
    void* backBits_ = nullptr;
    int backW_ = 0, backH_ = 0;

    // ---------------------------------------------------------- Maßstab und Schriften

    int px(float dip) const { return int(std::lround(dip * scale_)); }
    float s(float dip) const { return dip * scale_; }

    // Bild einmal hochwertig auf die Zielgröße bringen und dann nur noch 1:1 kopieren
    Bitmap* scaledImage(const std::string& key, Bitmap* src, int size) {
        std::string k = key + "@" + std::to_string(size);
        auto it = scaled_.find(k);
        if (it != scaled_.end()) return it->second.get();
        auto* b = new Bitmap(size, size, PixelFormat32bppPARGB);
        {
            Graphics g(b);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            ImageAttributes ia;
            ia.SetWrapMode(WrapModeTileFlipXY);   // keine hellen Ränder an den Kanten
            g.DrawImage(src, Rect(0, 0, size, size), 0, 0, int(src->GetWidth()), int(src->GetHeight()), UnitPixel, &ia);
        }
        scaled_[k].reset(b);
        return b;
    }

    void blit(Graphics& g, Bitmap* b, float x, float y) {
        InterpolationMode old = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(b, Rect(int(std::lround(x)), int(std::lround(y)), int(b->GetWidth()), int(b->GetHeight())), 0, 0, int(b->GetWidth()),
                    int(b->GetHeight()), UnitPixel);
        g.SetInterpolationMode(old);
    }

    void setScale(UINT dpi) {
        scaled_.clear();
        dpi_ = dpi ? dpi : 96;
        scale_ = float(dpi_) / 96.0f;
        if (!famText_) {
            famText_ = pickFamily({L"Segoe UI", L"Inter", L"Tahoma", L"Arial"});
            famSemi_ = pickFamily({L"Segoe UI Semibold", L"Inter SemiBold", L"Inter Semi Bold"});
            semiIsBold_ = false;
            if (famSemi_->GetLastStatus() != Ok || !famSemi_->IsAvailable() || famSemi_->IsStyleAvailable(FontStyleRegular) == FALSE) {
                famSemi_ = pickFamily({L"Segoe UI", L"Inter", L"Tahoma", L"Arial"});
                semiIsBold_ = true;
            }
            // pickFamily liefert bei Misserfolg GenericSansSerif – dann ebenfalls fett simulieren
            WCHAR name[LF_FACESIZE] = {};
            famSemi_->GetFamilyName(name);
            if (wcsstr(name, L"Semi") == nullptr && wcsstr(name, L"semi") == nullptr) semiIsBold_ = true;
        }
        int semiStyle = semiIsBold_ ? FontStyleBold : FontStyleRegular;
        fWord_ = std::make_unique<Font>(famSemi_.get(), s(22), semiStyle, UnitPixel);
        fTitle_ = std::make_unique<Font>(famSemi_.get(), s(16), semiStyle, UnitPixel);
        fBody_ = std::make_unique<Font>(famText_.get(), s(13), FontStyleRegular, UnitPixel);
        fSmall_ = std::make_unique<Font>(famText_.get(), s(12), FontStyleRegular, UnitPixel);
        fSmallBold_ = std::make_unique<Font>(famSemi_.get(), s(12), semiStyle, UnitPixel);
        fButton_ = std::make_unique<Font>(famSemi_.get(), s(13), semiStyle, UnitPixel);
        fEmpty_ = std::make_unique<Font>(famSemi_.get(), s(17), semiStyle, UnitPixel);
    }

    void applyWindowTheme() {
        theme_ = makeTheme(dark_);
        if (hwnd_) {
            BOOL dark = dark_ ? TRUE : FALSE;
            if (FAILED(DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark)))
                DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof dark);   // ältere Windows-10-Versionen
            COLORREF caption = RGB(theme_.bg.GetR(), theme_.bg.GetG(), theme_.bg.GetB());
            DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);   // Windows 11
            COLORREF captionText = RGB(theme_.text.GetR(), theme_.text.GetG(), theme_.text.GetB());
            DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &captionText, sizeof captionText);
        }
        setMenuTheme(dark_);
    }

    // ---------------------------------------------------------- Katalog

    std::wstring cachePath() const { return joinPath(dataDir(), L"catalog.json"); }

    void loadCachedCatalog() {
        std::string text;
        std::wstring err;
        if (opt_.catalogUrl.empty() || opt_.demo) {
            if (readFile(cachePath(), text, 4u << 20) && applyCatalogText(text, false, err)) cacheTime_ = fileTimeText(cachePath());
        }
    }

    bool applyCatalogText(const std::string& text, bool save, std::wstring& err) {
        cat::Catalog c;
        std::string perr;
        if (!cat::parse(text, c, perr)) {
            err = widen(perr);
            logLine(L"Katalog ungültig: %ls", err.c_str());
            return false;
        }
        for (const std::string& why : c.skipped) logLine(L"Katalog: Eintrag übersprungen – %ls", widen(why).c_str());
        if (!c.supported) {
            // Neueres Format: Programme nicht anfassen (der bisherige Stand bleibt), aber Launcher-Update anbieten
            catalogTooNew_ = true;
            catalog_.launcher = c.launcher;
            logLine(L"Katalog-Format %d ist neuer als dieser Launcher", c.schema);
            updateLauncherBanner();
            return true;
        }
        catalogTooNew_ = false;
        std::vector<AppView> next;
        for (const cat::App& a : c.apps) {
            AppView v;
            for (const AppView& old : apps_)
                if (old.app.id == a.id) v = old;   // laufende Aufträge und Meldungen behalten
            v.app = a;
            next.push_back(std::move(v));
        }
        catalog_ = std::move(c);
        apps_ = std::move(next);
        if (save) {
            createDirs(dataDir());
            writeFileAtomic(cachePath(), text);
            cacheTime_ = fileTimeText(cachePath());
        }
        if (opt_.demo) applyDemoStates();
        updateLauncherBanner();
        return true;
    }

    void fetchCatalog() {
        if (fetching_ || !hwnd_) return;
        fetching_ = true;
        catalogState_ = CatalogState::Loading;
        SetTimer(hwnd_, kTimerAnim, 33, nullptr);
        struct Ctx {
            HWND hwnd;
            std::wstring url;
        };
        auto* ctx = new Ctx{hwnd_, catalogUrl_};
        HANDLE t = CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD {
                std::unique_ptr<Ctx> c(static_cast<Ctx*>(p));
                auto* r = new CatalogResult();
                r->ok = download(c->url, r->text, r->error, Progress(), 4u << 20);
                if (!PostMessageW(c->hwnd, WM_APP_CATALOG, 0, reinterpret_cast<LPARAM>(r))) delete r;
                return 0;
            },
            ctx, 0, nullptr);
        if (t) CloseHandle(t);
        else {
            delete ctx;
            fetching_ = false;
        }
        invalidate();
    }

    void onCatalog(CatalogResult* r) {
        std::unique_ptr<CatalogResult> res(r);
        fetching_ = false;
        std::wstring err;
        if (res->ok && applyCatalogText(res->text, !opt_.demo && opt_.catalogUrl.empty(), err)) {
            catalogState_ = CatalogState::Ok;
            catalogOffline_ = false;
            catalogError_.clear();
            checkedAt_ = timeNow();
            logLine(L"Katalog geladen: %ls, %u Programme", widen(catalog_.release).c_str(), unsigned(catalog_.apps.size()));
        } else {
            catalogState_ = CatalogState::Failed;
            catalogOffline_ = !res->ok;
            catalogError_ = res->ok ? L"Programmliste unlesbar: " + err : res->error;
            logLine(L"Katalog nicht geladen: %ls", catalogError_.c_str());
        }
        refreshStates();
        invalidate();
    }

    void updateLauncherBanner() {
        if (selfUpdating_) return;
        const cat::Launcher& l = catalog_.launcher;
        bool newer = !l.version.empty() && !l.url.empty() && l.sha256.size() == 64 && cat::compareVersions(l.version, kVersionA) > 0;
        if (newer && banner_ != BannerKind::Error)
            setBanner(BannerKind::LauncherUpdate, catalogTooNew_ ? L"Für die neue Programmliste braucht es Launcher " + widen(l.version) + L"."
                                                                 : L"Version " + widen(l.version) + L" des Launchers ist da.");
        else if (catalogTooNew_ && banner_ != BannerKind::Error)
            setBanner(BannerKind::Error, L"Die Programmliste ist neuer als dieser Launcher – bitte EfficientLamp.exe neu von GitHub laden.");
        else if (!newer && banner_ == BannerKind::LauncherUpdate)
            setBanner(BannerKind::None, L"");
    }

    void setBanner(BannerKind k, const std::wstring& text) {
        banner_ = k;
        bannerText_ = text;
        if (hwnd_ && (k == BannerKind::Welcome || k == BannerKind::Updated)) SetTimer(hwnd_, kTimerBanner, 12000, nullptr);
        invalidate();
    }

    // ---------------------------------------------------------- Zustand der Programme

    void applyDemoStates() {
        for (AppView& v : apps_) {
            v.inst = InstallInfo();
            v.running = false;
            v.busy = false;
            if (v.app.id == "deskcats") {
                v.inst.installed = true;
                v.inst.version = widen(v.app.version);
                v.running = true;
            } else if (v.app.id == "snowdesk") {
                v.inst.installed = true;
                v.inst.version = L"1.0.0";
                v.running = true;
            } else if (v.app.id == "triggerswitch") {
                v.busy = true;
                v.kind = JobKind::Install;
                v.phase = JobPhase::Download;
                v.progress = 0.64;
            }
        }
    }

    bool refreshStates() {
        if (opt_.demo) return false;
        std::vector<std::wstring> procs = runningProcesses();
        bool changed = false;
        for (AppView& v : apps_) {
            InstallInfo i = detectInstall(v.app);
            bool run = isRunning(procs, v.app.process);
            if (i.installed != v.inst.installed || i.version != v.inst.version || run != v.running) changed = true;
            v.inst = i;
            v.running = run;
        }
        if (changed) invalidate();
        return changed;
    }

    int updateCount() const {
        int n = 0;
        for (const AppView& v : apps_)
            if (v.updateAvailable() && !v.busy) ++n;
        return n;
    }

    // Laufende Animation nur für kurze Phasen (Suche, Entpacken, Launcher-Update). Während ein Setup-Fenster offen ist
    // oder ein Auftrag wartet, steht die Anzeige still – kein Neuzeichnen im Hintergrund.
    bool anyAnimation() const {
        if (fetching_ || banner_ == BannerKind::Updating) return true;
        for (const AppView& v : apps_)
            if (v.busy && v.phase == JobPhase::Extract) return true;
        return false;
    }

    void onJob(JobEvent* e) {
        std::unique_ptr<JobEvent> ev(e);
        for (AppView& v : apps_) {
            if (v.app.id != ev->appId) continue;
            if (ev->phase == JobPhase::Finished) {
                v.busy = false;
                v.progress = -1;
                refreshStates();
                if (!ev->ok) {
                    v.error = ev->message == L"abgebrochen" ? std::wstring() : ev->message;
                } else if (ev->kind == JobKind::Install) {
                    if (!v.inst.installed) v.error = L"Setup beendet, aber das Programm ist noch nicht installiert – Hinweise standen im Setup-Fenster.";
                    else showNote(v, v.updateAvailable() ? L"Setup beendet" : L"Installiert");
                } else if (ev->kind == JobKind::Uninstall) {
                    if (v.inst.installed) v.error = L"Noch installiert – Deinstallation abgebrochen?";
                    else showNote(v, L"Entfernt");
                }
            } else {
                v.busy = true;
                v.kind = ev->kind;
                v.phase = ev->phase;
                v.progress = ev->progress;
                v.error.clear();
            }
        }
        if (anyAnimation()) SetTimer(hwnd_, kTimerAnim, 33, nullptr);
        invalidate();
    }

    void showNote(AppView& v, const std::wstring& text) {
        v.note = text;
        v.noteUntil = GetTickCount64() + 6000;
        if (hwnd_) SetTimer(hwnd_, kTimerAnim, 250, nullptr);
    }

    // ---------------------------------------------------------- Aktionen

    void startJob(AppView& v, JobKind kind) {
        if (jobs_->pending(v.app.id)) return;
        v.error.clear();
        v.note.clear();
        v.busy = true;
        v.kind = kind;
        v.phase = JobPhase::Queued;
        v.progress = -1;
        jobs_->add(kind, v.app);
        invalidate();
    }

    void openApp(AppView& v) {
        const cat::Command& c = v.app.open;
        if (!c.exe.empty()) {
            std::wstring err;
            if (!startProgram(expandEnv(widen(c.exe)), widen(c.args), err)) v.error = err;
            else showNote(v, L"Gestartet");
            invalidate();
        } else if (!c.script.empty()) {
            startJob(v, JobKind::OpenScript);
        }
    }

    void primaryAction(int i) {
        if (i < 0 || i >= int(apps_.size())) return;
        AppView& v = apps_[size_t(i)];
        if (v.busy) {
            if (jobs_->cancel(v.app.id)) {
                v.note.clear();
                invalidate();
            }
            return;
        }
        if (!v.inst.installed || v.updateAvailable()) startJob(v, JobKind::Install);
        else openApp(v);
    }

    // Während eines Menüs oder einer Rückfrage kann eine neue Programmliste eintreffen und apps_ ersetzen –
    // deshalb danach immer über die ID neu suchen statt eine Referenz festzuhalten.
    AppView* findApp(const std::string& id) {
        for (AppView& v : apps_)
            if (v.app.id == id) return &v;
        return nullptr;
    }

    void uninstallApp(const std::string& id) {
        AppView* v = findApp(id);
        if (!v) return;
        std::wstring q = widen(v->app.name) + L" deinstallieren?\n\nEs öffnet sich das Setup-Fenster des Programms und führt durch die Schritte.";
        if (MessageBoxW(hwnd_, q.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        if ((v = findApp(id)) != nullptr) startJob(*v, JobKind::Uninstall);
    }

    void updateAll() {
        for (AppView& v : apps_)
            if (v.updateAvailable() && !v.busy) startJob(v, JobKind::Install);
    }

    void startSelfUpdate() {
        if (selfUpdating_ || !hwnd_) return;
        selfUpdating_ = true;
        setBanner(BannerKind::Updating, L"Neue Launcher-Version wird geladen …");
        SetTimer(hwnd_, kTimerAnim, 33, nullptr);
        struct Ctx {
            HWND hwnd;
            cat::Launcher l;
        };
        auto* ctx = new Ctx{hwnd_, catalog_.launcher};
        HANDLE t = CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD {
                std::unique_ptr<Ctx> c(static_cast<Ctx*>(p));
                auto* r = new SelfUpdateResult();
                r->ok = download(widen(c->l.url), r->data, r->error);
                if (r->ok && sha256Hex(r->data.data(), r->data.size()) != c->l.sha256) {
                    r->ok = false;
                    r->error = L"Prüfsumme der neuen Version stimmt nicht";
                }
                if (!PostMessageW(c->hwnd, WM_APP_SELFUPDATE, 0, reinterpret_cast<LPARAM>(r))) delete r;
                return 0;
            },
            ctx, 0, nullptr);
        if (t) CloseHandle(t);
    }

    void onSelfUpdate(SelfUpdateResult* r) {
        std::unique_ptr<SelfUpdateResult> res(r);
        std::wstring err = res->error;
        if (res->ok) {
            if (jobs_->busy()) {
                err = L"bitte erst die laufenden Aufträge abwarten";
            } else {
                jobs_->stop(1500);   // vor dem Neustart: Arbeits-Thread beenden
                if (applyUpdate(res->data, err, opt_.relaunchArgs)) {
                    DestroyWindow(hwnd_);
                    return;
                }
                jobs_->start(hwnd_);   // Update fehlgeschlagen: weiterarbeiten
            }
        }
        selfUpdating_ = false;
        logLine(L"Launcher-Update fehlgeschlagen: %ls", err.c_str());
        setBanner(BannerKind::Error, L"Launcher-Update fehlgeschlagen: " + err);
    }

    void showAppMenu(int i, const RectF& anchor) {
        if (i < 0 || i >= int(apps_.size())) return;
        const std::string id = apps_[size_t(i)].app.id;
        AppView& v = apps_[size_t(i)];
        HMENU m = CreatePopupMenu();
        bool busy = v.busy;
        UINT dis = busy ? MF_GRAYED : 0;
        if (v.inst.installed) {
            std::wstring label = v.app.open.label.empty() ? L"Öffnen" : widen(v.app.open.label);
            if (v.app.open.valid()) AppendMenuW(m, MF_STRING | dis, kMenuOpen, label.c_str());
            if (dirExists(v.inst.dir)) AppendMenuW(m, MF_STRING, kMenuFolder, L"Installationsordner öffnen");
        } else {
            AppendMenuW(m, MF_STRING | dis, kMenuInstall, L"Installieren");
        }
        if (!v.app.page.empty()) AppendMenuW(m, MF_STRING, kMenuReadme, L"Anleitung auf GitHub");
        if (v.inst.installed) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING | dis, kMenuReinstall, v.updateAvailable() ? L"Aktualisieren" : L"Neu installieren (reparieren)");
            if (v.app.uninstall.valid()) AppendMenuW(m, MF_STRING | dis, kMenuUninstall, L"Deinstallieren …");
        }
        POINT pt = {LONG(anchor.X), LONG(anchor.Y + anchor.Height + s(4))};
        ClientToScreen(hwnd_, &pt);
        int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hwnd_, nullptr);
        DestroyMenu(m);
        AppView* cur = findApp(id);   // die Liste kann sich während des Menüs geändert haben
        if (!cur || cmd == 0) return;
        switch (cmd) {
            case kMenuOpen: openApp(*cur); break;
            case kMenuFolder: openFolder(cur->inst.dir); break;
            case kMenuReadme: openUrl(widen(cur->app.page)); break;
            case kMenuInstall:
            case kMenuReinstall: startJob(*cur, JobKind::Install); break;
            case kMenuUninstall: uninstallApp(id); break;
            default: break;
        }
    }

    void showMainMenu(const RectF& anchor) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (fetching_ ? MF_GRAYED : 0), kMenuCheck, L"Nach Updates suchen\tF5");
        AppendMenuW(m, MF_STRING, kMenuGithub, L"efficient lamp auf GitHub");
        AppendMenuW(m, MF_STRING, kMenuLog, L"Protokoll anzeigen");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, kMenuAbout, L"Über efficient lamp");
        if (opt_.installedCopy) AppendMenuW(m, MF_STRING, kMenuRemoveLauncher, L"Launcher deinstallieren …");
        POINT pt = {LONG(anchor.X + anchor.Width), LONG(anchor.Y + anchor.Height + s(4))};
        ClientToScreen(hwnd_, &pt);
        int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, hwnd_, nullptr);
        DestroyMenu(m);
        switch (cmd) {
            case kMenuCheck: fetchCatalog(); break;
            case kMenuGithub: openUrl(kRepoUrl); break;
            case kMenuLog: {
                std::wstring log = joinPath(dataDir(), L"launcher.log");
                if (fileExists(log)) ShellExecuteW(hwnd_, L"open", L"notepad.exe", (L"\"" + log + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                break;
            }
            case kMenuAbout: showAbout(); break;
            case kMenuRemoveLauncher:
                if (jobs_->busy()) {
                    MessageBoxW(hwnd_, L"Bitte erst warten, bis die laufenden Aufträge fertig sind.", kAppName, MB_OK | MB_ICONINFORMATION);
                } else if (uninstallSelf(hwnd_)) {
                    DestroyWindow(hwnd_);
                }
                break;
            default: break;
        }
    }

    void showAbout() {
        std::wstring text = std::wstring(L"efficient lamp ") + kVersion +
                            L"\nKleine, sparsame Windows-Tools – und ein Launcher für alles.\n\n"
                            L"Der Launcher lädt die Programme direkt aus den GitHub-Releases, prüft jede Datei per SHA-256 "
                            L"und startet dann das Setup des jeweiligen Programms. Er läuft nur, solange dieses Fenster offen ist, "
                            L"braucht keine Administratorrechte und greift nie in andere Programme oder Spiele ein.\n\n";
        if (!catalog_.release.empty()) text += L"Programmliste: Release " + widen(catalog_.release) + L"\n";
        text += std::wstring(kRepoUrl) + L"\nMIT-Lizenz";
        MessageBoxW(hwnd_, text.c_str(), L"Über efficient lamp", MB_OK | MB_ICONINFORMATION);
    }

    void activate(int hit) {
        if (hit < 0 || hit >= int(hits_.size()) || !hits_[size_t(hit)].enabled) return;
        Hit h = hits_[size_t(hit)];
        switch (h.kind) {
            case HitKind::Primary: primaryAction(h.index); break;
            case HitKind::More: showAppMenu(h.index, h.rc); break;
            case HitKind::UpdateAll: updateAll(); break;
            case HitKind::Refresh: fetchCatalog(); break;
            case HitKind::Menu: showMainMenu(h.rc); break;
            case HitKind::BannerAction: startSelfUpdate(); break;
            case HitKind::BannerClose: setBanner(BannerKind::None, L""); break;
            case HitKind::Footer: openUrl(kRepoUrl); break;
            case HitKind::Retry: fetchCatalog(); break;
            default: break;
        }
    }

    // ---------------------------------------------------------- Zeichnen

    void invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    float textWidth(Graphics& g, const std::wstring& t, const Font& f) {
        RectF b;
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
        g.MeasureString(t.c_str(), int(t.size()), &f, PointF(0, 0), &fmt, &b);
        return b.Width;
    }

    void text(Graphics& g, const std::wstring& t, const Font& f, const Color& c, const RectF& r, StringAlignment h = StringAlignmentNear,
              bool wrap = false, StringAlignment v = StringAlignmentCenter) {
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetAlignment(h);
        fmt.SetLineAlignment(v);
        fmt.SetTrimming(StringTrimmingEllipsisCharacter);
        fmt.SetFormatFlags((wrap ? 0 : StringFormatFlagsNoWrap) | StringFormatFlagsLineLimit | StringFormatFlagsMeasureTrailingSpaces);
        SolidBrush b(c);
        g.DrawString(t.c_str(), int(t.size()), &f, r, &fmt, &b);
    }

    int addHit(const RectF& rc, HitKind k, int index, bool enabled = true) {
        Hit h;
        h.rc = rc;
        h.kind = k;
        h.index = index;
        h.enabled = enabled;
        hits_.push_back(h);
        return int(hits_.size()) - 1;
    }

    bool isHover(int hit) const { return hit == hover_; }
    bool isPressed(int hit) const { return hit == pressed_ && hit == hover_; }
    bool isFocus(const Hit& h) const { return keyboardFocus_ && h.kind == focusKind_ && h.index == focusIndex_; }

    void focusRing(Graphics& g, const RectF& r, float rad) {
        RectF f(r.X - s(3), r.Y - s(3), r.Width + s(6), r.Height + s(6));
        strokeRound(g, f, rad + s(3), theme_.accent, s(2));
    }

    // Schaltfläche: accent = gelb gefüllt, sonst dezent
    void button(Graphics& g, const RectF& r, const std::wstring& label, HitKind kind, int index, bool accent, bool enabled = true) {
        int h = addHit(r, kind, index, enabled);
        Color bg = accent ? (isPressed(h) ? theme_.accentPressed : isHover(h) ? theme_.accentHover : theme_.accent)
                          : (isPressed(h) ? theme_.btnPressed : isHover(h) ? theme_.btnHover : theme_.btn);
        Color fg = accent ? theme_.onAccent : theme_.text;
        if (!enabled) {
            bg = withAlpha(bg, 110);
            fg = withAlpha(fg, 120);
        }
        fillRound(g, r, s(8), bg);
        text(g, label, *fButton_, fg, r, StringAlignmentCenter);
        if (isFocus(hits_.back())) focusRing(g, r, s(8));
    }

    float buttonWidth(Graphics& g, const std::wstring& label, float minW) { return std::max(s(minW), textWidth(g, label, *fButton_) + s(36)); }

    void iconButton(Graphics& g, const RectF& r, HitKind kind, int index, int icon, bool enabled = true) {
        int h = addHit(r, kind, index, enabled);
        if (isHover(h) || isPressed(h)) fillRound(g, r, s(8), isPressed(h) ? theme_.btnPressed : theme_.btnHover);
        Color fg = enabled ? theme_.text2 : withAlpha(theme_.text3, 120);
        float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
        if (icon == 0) {   // drei Punkte
            SolidBrush b(fg);
            for (int k = -1; k <= 1; ++k) g.FillEllipse(&b, cx + k * s(6) - s(1.9f), cy - s(1.9f), s(3.8f), s(3.8f));
        } else if (icon == 1) {   // Kreispfeil (dreht sich, solange nach Updates gesucht wird)
            float rad = s(6.5f);
            float rot = fetching_ ? animT_ * 360.0f * 1.2f : 0;
            float start = -70 + rot, sweep = 280;
            Pen pen(fg, s(1.7f));
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapFlat);
            g.DrawArc(&pen, cx - rad, cy - rad, rad * 2, rad * 2, start, sweep);
            float e = float((start + sweep) * 3.14159265 / 180.0);
            PointF p(cx + rad * std::cos(e), cy + rad * std::sin(e));
            PointF t(-std::sin(e), std::cos(e)), n(std::cos(e), std::sin(e));   // Laufrichtung (im Uhrzeigersinn) und Normale
            float h = s(4.2f), w = s(3.6f);
            PointF tri[3] = {PointF(p.X + t.X * h, p.Y + t.Y * h), PointF(p.X + n.X * w, p.Y + n.Y * w), PointF(p.X - n.X * w, p.Y - n.Y * w)};
            SolidBrush b(fg);
            g.FillPolygon(&b, tri, 3);
        } else if (icon == 2) {   // Schließen ×
            Pen pen(fg, s(1.6f));
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            float d = s(4.5f);
            g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
            g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
        }
        if (isFocus(hits_.back())) focusRing(g, r, s(8));
    }

    void drawTile(Graphics& g, const AppView& v, const RectF& r) {
        auto it = tiles_.find(v.app.id);
        if (it != tiles_.end() && it->second) {
            blit(g, scaledImage("tile:" + v.app.id, it->second.get(), int(std::lround(r.Width))), r.X, r.Y);
            return;
        }
        // Unbekanntes Programm: farbige Kachel mit Anfangsbuchstaben
        uint32_t col = 0xF59E0B;
        if (v.app.color.size() == 7 && v.app.color[0] == '#') col = uint32_t(strtoul(v.app.color.c_str() + 1, nullptr, 16));
        fillRound(g, r, r.Width * 0.22f, rgb(col));
        std::wstring wname = widen(v.app.name), letter;
        if (!wname.empty()) letter = wname.substr(0, (wname.size() > 1 && IS_HIGH_SURROGATE(wname[0])) ? 2 : 1);
        Font f(famSemi_.get(), r.Height * 0.5f, semiIsBold_ ? FontStyleBold : FontStyleRegular, UnitPixel);
        text(g, letter, f, rgb(0xFFFFFF), r, StringAlignmentCenter);
    }

    void progressBar(Graphics& g, const RectF& r, double p) {
        fillRound(g, r, r.Height / 2, theme_.track);
        if (p >= 0) {
            RectF f(r.X, r.Y, std::max(r.Height, float(r.Width * std::min(1.0, p))), r.Height);
            fillRound(g, f, r.Height / 2, theme_.accent);
        } else {   // unbestimmt: wandernder Abschnitt
            float w = r.Width * 0.3f;
            float t = animT_ - std::floor(animT_);
            float x = r.X - w + (r.Width + w) * t;
            RectF f(std::max(r.X, x), r.Y, std::min(x + w, r.X + r.Width) - std::max(r.X, x), r.Height);
            if (f.Width > 0) fillRound(g, f, r.Height / 2, theme_.accent);
        }
    }

    std::wstring phaseText(const AppView& v) const {
        switch (v.phase) {
            case JobPhase::Queued: return L"Wartet …";
            case JobPhase::Download: {
                std::wstring t = L"Wird geladen";
                if (v.progress >= 0) t += L" … " + std::to_wstring(int(v.progress * 100 + 0.5)) + L" %";
                return t;
            }
            case JobPhase::Extract: return L"Wird geprüft und entpackt …";
            case JobPhase::Setup:
                return v.kind == JobKind::Uninstall ? L"Deinstallation läuft – bitte im Setup-Fenster weitermachen"
                                                    : L"Setup läuft – bitte im Setup-Fenster weitermachen";
            default: return L"";
        }
    }

    void drawCard(Graphics& g, int i, const RectF& c, const RectF& clip) {
        AppView& v = apps_[size_t(i)];
        (void)clip;
        bool hot = hoverCard_ == i;
        fillRound(g, c, s(12), hot ? theme_.cardHover : theme_.card);
        strokeRound(g, RectF(c.X + 0.5f, c.Y + 0.5f, c.Width - 1, c.Height - 1), s(12), theme_.border, 1);

        RectF tile(c.X + s(18), c.Y + (c.Height - s(52)) / 2, s(52), s(52));
        drawTile(g, v, tile);

        // Schaltflächen rechts
        RectF more(c.X + c.Width - s(16) - s(34), c.Y + (c.Height - s(34)) / 2, s(34), s(34));
        std::wstring label;
        bool accent = false, enabled = true;
        if (v.busy) {
            bool cancellable = v.phase == JobPhase::Queued || v.phase == JobPhase::Download;
            label = cancellable ? L"Abbrechen" : L"Bitte warten";
            enabled = cancellable;
        } else if (!v.inst.installed) {
            label = L"Installieren";
            accent = true;
        } else if (v.updateAvailable()) {
            label = L"Aktualisieren";
            accent = true;
        } else {
            label = v.app.open.label.empty() ? L"Öffnen" : widen(v.app.open.label);
            enabled = v.app.open.valid();
        }
        float bw = buttonWidth(g, label, 118);
        RectF prim(more.X - s(8) - bw, more.Y, bw, s(34));
        if (accent && catalogState_ == CatalogState::Loading) enabled = false;   // erst die aktuelle Liste abwarten
        button(g, prim, label, HitKind::Primary, i, accent, enabled);
        iconButton(g, more, HitKind::More, i, 0);

        // Texte
        float x0 = tile.X + tile.Width + s(16);
        float tw = prim.X - s(20) - x0;
        std::wstring name = widen(v.app.name);
        float nameW = std::min(tw, textWidth(g, name, *fTitle_));
        float top = c.Y + s(13);
        text(g, name, *fTitle_, theme_.text, RectF(x0, top, nameW + s(2), s(22)));
        float chipX = x0 + nameW + s(10);
        if (v.running && !v.busy) {
            SolidBrush dot(theme_.success);
            g.FillEllipse(&dot, chipX, top + s(11) - s(3.5f), s(7), s(7));
            text(g, L"Läuft", *fSmallBold_, theme_.success, RectF(chipX + s(11), top, s(60), s(22)));
            chipX += s(11) + textWidth(g, L"Läuft", *fSmallBold_) + s(10);
        }
        if (v.updateAvailable() && !v.busy && chipX + s(70) < x0 + tw) {
            std::wstring t = L"Update";
            float w = textWidth(g, t, *fSmallBold_) + s(16);
            RectF pill(chipX, top + s(2), w, s(19));
            fillRound(g, pill, s(9.5f), withAlpha(theme_.accent, dark_ ? 46 : 70));
            text(g, t, *fSmallBold_, accentText(theme_), pill, StringAlignmentCenter);
        }
        std::wstring desc = widen(v.app.description.empty() ? v.app.tagline : v.app.description);
        text(g, desc, *fBody_, theme_.text2, RectF(x0, c.Y + s(38), tw, s(36)), StringAlignmentNear, true, StringAlignmentNear);

        // Fußzeile der Karte: Fortschritt, Fehler, Meldung oder Versionsinfo
        RectF meta(x0, c.Y + c.Height - s(28), tw, s(18));
        if (v.busy && (v.phase == JobPhase::Download || v.phase == JobPhase::Extract)) {
            float barW = std::min(s(170), tw * 0.42f);
            progressBar(g, RectF(x0, meta.Y + (meta.Height - s(6)) / 2, barW, s(6)), v.phase == JobPhase::Download ? v.progress : -1);
            text(g, phaseText(v), *fSmall_, theme_.text2, RectF(x0 + barW + s(12), meta.Y, tw - barW - s(12), meta.Height));
        } else if (v.busy) {   // wartet oder Setup-Fenster offen: ruhige Anzeige ohne Animation
            SolidBrush dot(v.phase == JobPhase::Setup ? theme_.accent : theme_.text3);
            g.FillEllipse(&dot, x0, meta.Y + meta.Height / 2 - s(3.5f), s(7), s(7));
            text(g, phaseText(v), *fSmall_, theme_.text2, RectF(x0 + s(14), meta.Y, tw - s(14), meta.Height));
        } else if (!v.error.empty()) {
            text(g, v.error, *fSmall_, theme_.danger, meta);
        } else if (!v.note.empty() && GetTickCount64() < v.noteUntil) {
            text(g, v.note, *fSmallBold_, theme_.success, meta);
        } else {
            std::wstring m;
            Color col = theme_.text3;
            if (!v.inst.installed) {
                m = L"Version " + widen(v.app.version);
                if (v.app.size) m += L" · " + formatBytes(v.app.size);
            } else if (v.updateAvailable()) {
                m = L"Installiert: " + v.inst.version + L" · neu: " + widen(v.app.version);
                col = accentText(theme_);
            } else {
                m = L"Version " + (v.inst.version.empty() ? widen(v.app.version) : v.inst.version) + L" installiert";
            }
            if (!v.app.requirements.empty()) m += L" · " + widen(v.app.requirements);
            text(g, m, *fSmall_, col, meta);
        }
    }

    void paint(Graphics& g, float W, float H) {
        hits_.clear();
        cardRects_.clear();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(theme_.bg);
        const float M = s(24);

        // Kopfzeile: Logo, Name, Status, Knöpfe
        float headerH = s(92);
        if (logo_) blit(g, scaledImage("logo", logo_.get(), px(44)), M, s(24));
        float wx = M + s(44) + s(14);
        std::wstring w1 = L"efficient", w2 = L"lamp";
        float w1W = textWidth(g, w1, *fWord_);
        text(g, w1, *fWord_, theme_.text, RectF(wx, s(20), w1W + s(4), s(30)));
        text(g, w2, *fWord_, accentText(theme_), RectF(wx + w1W + s(6), s(20), s(80), s(30)));

        std::wstring status;
        Color statusCol = theme_.text2;
        int updates = updateCount();
        if (catalogState_ == CatalogState::Loading) status = apps_.empty() ? L"Programmliste wird geladen …" : L"Suche nach Updates …";
        else if (catalogState_ == CatalogState::Failed && apps_.empty()) status = L"Programmliste nicht geladen";
        else if (catalogState_ == CatalogState::Failed)
            status = catalogOffline_ ? L"Offline – zeige den Stand vom " + cacheTime_ : catalogError_;
        else if (updates > 0) {
            status = updates == 1 ? L"1 Update verfügbar" : std::to_wstring(updates) + L" Updates verfügbar";
            statusCol = accentText(theme_);
        } else {
            int installed = 0;
            for (const AppView& v : apps_) installed += v.inst.installed ? 1 : 0;
            status = installed == 0 ? L"Wähle aus, was du installieren möchtest" : L"Alles aktuell · geprüft um " + checkedAt_;
        }
        float rightEdge = W - M;
        RectF menuR(rightEdge - s(34), s(29), s(34), s(34));
        RectF refreshR(menuR.X - s(4) - s(34), s(29), s(34), s(34));
        float statusRight = refreshR.X - s(12);
        if (updates > 0) {
            std::wstring t = updates == 1 ? L"Aktualisieren" : L"Alle aktualisieren";
            float bw = buttonWidth(g, t, 100);
            RectF ua(refreshR.X - s(10) - bw, s(29), bw, s(34));
            button(g, ua, t, HitKind::UpdateAll, -1, true);
            statusRight = ua.X - s(12);
        }
        text(g, status, *fBody_, statusCol, RectF(wx, s(50), std::max(s(40), statusRight - wx), s(20)));
        iconButton(g, refreshR, HitKind::Refresh, -1, 1, !fetching_);
        iconButton(g, menuR, HitKind::Menu, -1, 0);

        // Hinweisleiste (Willkommen, Launcher-Update, Fehler)
        float y = headerH;
        if (banner_ != BannerKind::None) {
            RectF b(M, y, W - 2 * M, s(46));
            Color tint = banner_ == BannerKind::Error ? theme_.danger : theme_.accent;
            fillRound(g, b, s(10), withAlpha(tint, dark_ ? 30 : 38));
            strokeRound(g, RectF(b.X + 0.5f, b.Y + 0.5f, b.Width - 1, b.Height - 1), s(10), withAlpha(tint, dark_ ? 90 : 120), 1);
            float right = b.X + b.Width - s(8);
            if (banner_ == BannerKind::LauncherUpdate) {
                std::wstring t = L"Jetzt aktualisieren";
                float bw = buttonWidth(g, t, 100);
                RectF br(right - bw, b.Y + s(7), bw, s(32));
                button(g, br, t, HitKind::BannerAction, -1, true);
                right = br.X - s(10);
            } else if (banner_ != BannerKind::Updating) {
                RectF cr(right - s(32), b.Y + s(7), s(32), s(32));
                iconButton(g, cr, HitKind::BannerClose, -1, 2);
                right = cr.X - s(6);
            }
            float tx = b.X + s(16);
            if (banner_ == BannerKind::Updating) {
                progressBar(g, RectF(tx, b.Y + (b.Height - s(6)) / 2, s(90), s(6)), -1);
                tx += s(104);
            }
            text(g, bannerText_, *fBody_, theme_.text, RectF(tx, b.Y, right - tx, b.Height));
            y += s(46) + s(14);
        }

        // Inhalt (scrollbar)
        float footerH = s(44);
        RectF view(0, y, W, std::max(0.0f, H - footerH - y));
        viewH_ = view.Height;
        viewTop_ = view.Y;
        viewBottom_ = view.Y + view.Height;
        const float cardH = s(100), gap = s(12);
        contentH_ = apps_.empty() ? 0 : s(2) + apps_.size() * cardH + (apps_.size() - 1) * gap + s(10);   // oben 2, unten 10 Luft
        float maxScroll = contentH_ - viewH_ > s(2) ? contentH_ - viewH_ : 0.0f;   // Rundungsreste ignorieren
        scrollY_ = std::min(std::max(0.0f, scrollY_), maxScroll);
        g.SetClip(view);
        if (apps_.empty()) {
            RectF e(M, view.Y, W - 2 * M, std::max(s(160), view.Height));
            if (catalogState_ == CatalogState::Loading) {
                text(g, L"Programmliste wird von GitHub geladen …", *fBody_, theme_.text2, RectF(e.X, e.Y + e.Height / 2 - s(40), e.Width, s(30)), StringAlignmentCenter);
                progressBar(g, RectF(e.X + e.Width / 2 - s(60), e.Y + e.Height / 2, s(120), s(5)), -1);
            } else {
                text(g, L"Keine Verbindung zu GitHub", *fEmpty_, theme_.text, RectF(e.X, e.Y + e.Height / 2 - s(66), e.Width, s(28)), StringAlignmentCenter);
                std::wstring why = catalogError_.empty() ? L"Bitte die Internetverbindung prüfen." : catalogError_;
                text(g, why, *fBody_, theme_.text2, RectF(e.X + s(30), e.Y + e.Height / 2 - s(34), e.Width - s(60), s(40)), StringAlignmentCenter, true,
                     StringAlignmentNear);
                float bw = buttonWidth(g, L"Erneut versuchen", 120);
                button(g, RectF(e.X + (e.Width - bw) / 2, e.Y + e.Height / 2 + s(12), bw, s(34)), L"Erneut versuchen", HitKind::Retry, -1, true);
            }
        } else {
            float cy = view.Y + s(2) - scrollY_;
            for (size_t i = 0; i < apps_.size(); ++i) {
                RectF c(M, cy, W - 2 * M, cardH);
                cardRects_.push_back(c);   // für jedes Programm, auch wenn es gerade nicht sichtbar ist
                drawCard(g, int(i), c, view);   // unsichtbare Teile schneidet der Clip ab; Knöpfe bleiben per Tab erreichbar
                cy += cardH + gap;
            }
            if (maxScroll > 0) {   // Scrollanzeige
                float trackH = view.Height - s(8);
                float thumbH = std::max(s(30), trackH * viewH_ / contentH_);
                float thumbY = view.Y + s(4) + (trackH - thumbH) * (scrollY_ / maxScroll);
                fillRound(g, RectF(W - s(9), thumbY, s(4), thumbH), s(2), withAlpha(theme_.text3, 120));
            }
        }
        g.ResetClip();

        // Fußzeile
        float fy = H - footerH;
        Pen line(theme_.border, 1);
        g.DrawLine(&line, M, fy, W - M, fy);
        std::wstring left = std::wstring(L"Launcher ") + kVersion;
        if (!opt_.installedCopy && opt_.renderPng.empty()) left += L" · portabel";
        if (!catalog_.release.empty()) left += L" · Programmliste " + widen(catalog_.release);
        text(g, left, *fSmall_, theme_.text3, RectF(M, fy, W / 2, footerH));
        std::wstring link = L"Quellcode & Anleitungen auf GitHub";
        float lw = textWidth(g, link, *fSmall_) + s(13);
        RectF lr(W - M - lw, fy + s(10), lw, footerH - s(20));
        int lh = addHit(lr, HitKind::Footer, -1);
        Color lc = isHover(lh) ? theme_.text : accentText(theme_);
        text(g, link, *fSmall_, lc, RectF(lr.X, lr.Y, lw - s(13), lr.Height));
        {   // kleiner Pfeil nach rechts oben (selbst gezeichnet – nicht jede Schrift hat das Zeichen)
            Pen pen(lc, s(1.3f));
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            float ax = lr.X + lw - s(5), ay = lr.Y + lr.Height / 2 + s(0.5f), d = s(3.2f);
            g.DrawLine(&pen, ax - d, ay + d, ax + d, ay - d);
            g.DrawLine(&pen, ax - s(0.6f), ay - d, ax + d, ay - d);
            g.DrawLine(&pen, ax + d, ay - d, ax + d, ay + s(0.6f));
        }
        if (isFocus(hits_.back())) focusRing(g, lr, s(4));
    }

    void onPaint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = std::max<int>(1, rc.right), h = std::max<int>(1, rc.bottom);
        if (!backDc_ || w != backW_ || h != backH_) {
            if (backDc_) DeleteDC(backDc_);
            if (backBmp_) DeleteObject(backBmp_);
            // 32-Bit-DIB als Zeichenfläche: GDI+ zeichnet direkt in den Speicher (volle Kantenglättung)
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            backDc_ = CreateCompatibleDC(dc);
            backBmp_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &backBits_, nullptr, 0);
            SelectObject(backDc_, backBmp_);
            backW_ = w;
            backH_ = h;
        }
        if (backBits_) {
            Bitmap canvas(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(backBits_));
            Graphics g(&canvas);
            paint(g, float(w), float(h));
        }
        GdiFlush();
        BitBlt(dc, 0, 0, w, h, backDc_, 0, 0, SRCCOPY);
        EndPaint(hwnd_, &ps);
    }

    // ---------------------------------------------------------- Eingabe

    int hitAt(int x, int y) const {
        for (size_t i = hits_.size(); i-- > 0;) {
            const Hit& h = hits_[i];
            bool inCard = h.kind == HitKind::Primary || h.kind == HitKind::More;
            if (inCard && (y < viewTop_ || y >= viewBottom_)) continue;   // weggescrollte Teile sind nicht klickbar
            if (h.rc.Contains(float(x), float(y))) return int(i);
        }
        return -1;
    }

    void moveFocus(int dir) {
        std::vector<int> order;
        for (size_t i = 0; i < hits_.size(); ++i)
            if (hits_[i].enabled) order.push_back(int(i));
        if (order.empty()) return;
        // Reihenfolge: von oben nach unten, links nach rechts
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const RectF &ra = hits_[size_t(a)].rc, &rb = hits_[size_t(b)].rc;
            if (std::fabs(ra.Y - rb.Y) > s(20)) return ra.Y < rb.Y;
            return ra.X < rb.X;
        });
        int cur = -1;
        for (size_t k = 0; k < order.size(); ++k)
            if (hits_[size_t(order[k])].kind == focusKind_ && hits_[size_t(order[k])].index == focusIndex_) cur = int(k);
        int next = cur < 0 ? (dir > 0 ? 0 : int(order.size()) - 1) : (cur + dir + int(order.size())) % int(order.size());
        const Hit& h = hits_[size_t(order[size_t(next)])];
        focusKind_ = h.kind;
        focusIndex_ = h.index;
        keyboardFocus_ = true;
        // sichtbar machen
        if (h.kind == HitKind::Primary || h.kind == HitKind::More) {
            float top = h.rc.Y, bottom = h.rc.Y + h.rc.Height;   // Knopf mit etwas Luft ins Bild holen
            if (top < viewTop_ + s(40)) scrollY_ -= (viewTop_ + s(40) - top);
            if (bottom > viewBottom_ - s(40)) scrollY_ += bottom - (viewBottom_ - s(40));
        }
        invalidate();
    }

    void activateFocus() {
        for (size_t i = 0; i < hits_.size(); ++i)
            if (hits_[i].kind == focusKind_ && hits_[i].index == focusIndex_) {
                activate(int(i));
                return;
            }
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Ui* self = reinterpret_cast<Ui*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Ui*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = h;
        }
        if (self) return self->handle(msg, wp, lp);
        return DefWindowProcW(h, msg, wp, lp);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: onPaint(); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_SIZE:
                if (wp == SIZE_MINIMIZED) KillTimer(hwnd_, kTimerPoll);   // minimiert: gar nichts tun
                else {
                    if (minimized_) {
                        SetTimer(hwnd_, kTimerPoll, 3000, nullptr);
                        refreshStates();
                    }
                    invalidate();
                }
                minimized_ = wp == SIZE_MINIMIZED;
                return 0;
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                mmi->ptMinTrackSize.x = px(600);
                mmi->ptMinTrackSize.y = px(420);
                return 0;
            }
            case WM_DPICHANGED: {
                float before = scale_;
                setScale(HIWORD(wp));
                scrollY_ *= scale_ / before;
                const RECT* r = reinterpret_cast<const RECT*>(lp);
                SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                invalidate();
                return 0;
            }
            case WM_SETTINGCHANGE:
                if (opt_.theme == -1 && lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                    bool d = systemUsesDarkMode();
                    if (d != dark_) {
                        dark_ = d;
                        applyWindowTheme();
                        invalidate();
                    }
                }
                break;
            case WM_ACTIVATE:
                if (LOWORD(wp) != WA_INACTIVE) refreshStates();
                break;
            case WM_MOUSEMOVE: {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                if (!tracking_) {
                    TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd_, 0};
                    TrackMouseEvent(&t);
                    tracking_ = true;
                }
                int hit = hitAt(x, y);
                int card = -1;
                for (size_t k = 0; k < cardRects_.size(); ++k)
                    if (cardRects_[k].Contains(float(x), float(y)) && y >= viewTop_ && y < viewBottom_) card = int(k);
                if (hit != hover_ || card != hoverCard_) {   // nur neu zeichnen, wenn sich etwas ändert
                    hover_ = hit;
                    hoverCard_ = card;
                    invalidate();
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                tracking_ = false;
                hover_ = -1;
                hoverCard_ = -1;
                invalidate();
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    POINT p;
                    GetCursorPos(&p);
                    ScreenToClient(hwnd_, &p);
                    int hit = hitAt(p.x, p.y);
                    SetCursor(LoadCursorW(nullptr, hit >= 0 && hits_[size_t(hit)].enabled ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN:
                pressed_ = hitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                keyboardFocus_ = false;
                SetCapture(hwnd_);
                invalidate();
                return 0;
            case WM_LBUTTONUP: {
                ReleaseCapture();
                int hit = hitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                int was = pressed_;
                pressed_ = -1;
                invalidate();
                if (hit >= 0 && hit == was) {
                    focusKind_ = hits_[size_t(hit)].kind;
                    focusIndex_ = hits_[size_t(hit)].index;
                    activate(hit);
                }
                return 0;
            }
            case WM_MOUSEWHEEL:
                scrollY_ -= float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * s(60);
                hover_ = hoverCard_ = -1;   // stimmt nach dem Verschieben nicht mehr – nächste Mausbewegung setzt es neu
                invalidate();
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_TAB) moveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                else if (wp == VK_RETURN || wp == VK_SPACE) {
                    if (keyboardFocus_) activateFocus();
                } else if (wp == VK_F5) fetchCatalog();
                else if (wp == VK_DOWN) scrollY_ += s(40), invalidate();
                else if (wp == VK_UP) scrollY_ -= s(40), invalidate();
                else if (wp == VK_ESCAPE && keyboardFocus_) keyboardFocus_ = false, invalidate();
                return 0;
            case WM_TIMER:
                if (wp == kTimerPoll) {
                    if (!IsIconic(hwnd_) && IsWindowVisible(hwnd_)) refreshStates();   // nur wenn sichtbar
                } else if (wp == kTimerAnim) {
                    animT_ += 0.018f;
                    bool notes = false;
                    for (const AppView& v : apps_)
                        if (!v.note.empty() && GetTickCount64() < v.noteUntil + 300) notes = true;
                    if (!anyAnimation() && !notes) KillTimer(hwnd_, kTimerAnim);
                    else if (!anyAnimation()) SetTimer(hwnd_, kTimerAnim, 250, nullptr);
                    invalidate();
                } else if (wp == kTimerBanner) {
                    KillTimer(hwnd_, kTimerBanner);
                    if (banner_ == BannerKind::Welcome || banner_ == BannerKind::Updated) setBanner(BannerKind::None, L"");
                }
                return 0;
            case WM_APP_CATALOG: onCatalog(reinterpret_cast<CatalogResult*>(lp)); return 0;
            case WM_APP_JOB: onJob(reinterpret_cast<JobEvent*>(lp)); return 0;
            case WM_APP_REFRESH: refreshStates(); return 0;
            case WM_APP_SELFUPDATE: onSelfUpdate(reinterpret_cast<SelfUpdateResult*>(lp)); return 0;
            case WM_CLOSE:
                if (jobs_->setupRunning()) {
                    if (MessageBoxW(hwnd_, L"Ein Setup-Fenster ist noch offen. Es läuft auch ohne den Launcher weiter.\n\nLauncher trotzdem schließen?", kAppName,
                                    MB_YESNO | MB_ICONQUESTION) != IDYES)
                        return 0;
                } else if (jobs_->busy()) {
                    if (MessageBoxW(hwnd_, L"Ein Download läuft noch. Abbrechen und schließen?", kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                }
                DestroyWindow(hwnd_);   // Aufträge beendet ~Ui (Downloads werden dabei abgebrochen)
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd_, kTimerPoll);
                KillTimer(hwnd_, kTimerAnim);
                if (backDc_) DeleteDC(backDc_);
                if (backBmp_) DeleteObject(backBmp_);
                backDc_ = nullptr;
                backBmp_ = nullptr;
                backBits_ = nullptr;
                PostQuitMessage(0);
                return 0;
            default: break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
};

}  // namespace

int runLauncher(HINSTANCE inst, const Options& opt) {
    GdiplusStartupInput in;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &in, nullptr) != Ok) {
        MessageBoxW(nullptr, L"GDI+ konnte nicht gestartet werden.", kAppName, MB_OK | MB_ICONERROR);
        return 1;
    }
    int rc = 0;
    {
        Ui ui(inst, opt);
        if (!opt.renderPng.empty()) {
            rc = ui.renderPng(opt.renderPng, opt.renderWidth, opt.renderHeight, opt.renderScale) ? 0 : 1;
        } else if (ui.create()) {
            rc = ui.loop();
        } else {
            rc = 1;
        }
    }
    GdiplusShutdown(token);
    return rc;
}

}  // namespace lamp
