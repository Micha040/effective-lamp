// SnowDesk – Hauptprogramm: Schnee auf dem Desktop-Hintergrund, der auf Fenstern liegen bleibt.
//
//   SnowDesk.exe                 starten bzw. Einstellungen öffnen, wenn es schon läuft
//   SnowDesk.exe --autostart     still starten (Anmeldung)
//   SnowDesk.exe --cmd <Befehl>  on | off | toggle | light | medium | storm | weather | shake | settings | test |
//                                reload | diag | exit | render:0..2
//   SnowDesk.exe --pack-menu <Ordner>   Paket für das Windows-11-Kontextmenü bauen (Setup)
//   SnowDesk.exe --diag          Diagnose nach %LOCALAPPDATA%\SnowDesk\diagnose.txt
#include "common.h"
#include "desktop.h"
#include "layerthread.h"
#include "monitors.h"
#include "pack.h"
#include "pause.h"
#include "renderer.h"
#include "settings.h"
#include "settings_ui.h"
#include "tracker.h"
#include "weather.h"

#include "../snowcore.h"

#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <memory>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace app {

namespace {

const wchar_t* kVersion = L"1.1.1";
int g_headlessSeconds = 0;   // --headless-test: zeichnet offscreen statt auf den Desktop (Tests unter Wine)
const wchar_t* kMutexName = L"Local\\SnowDesk.Instance";
constexpr UINT WM_APP_COMMAND = WM_APP + 1;
enum : UINT_PTR { kHousekeepingTimer = 10, kSaveTimer = 11, kReattachTimer = 12, kModalTimer = 13 };

double nowSec() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

std::wstring trimLower(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\0' || s[a] == L'"')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\0' || s[b - 1] == L'"')) --b;
    std::wstring t = s.substr(a, b - a);
    if (!t.empty()) CharLowerBuffW(&t[0], (DWORD)t.size());
    return t;
}

std::wstring windowsVersion() {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    OSVERSIONINFOW v;
    ZeroMemory(&v, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    auto fn = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (fn) fn(&v);
    DWORD ubr = 0, size = sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size);
    wchar_t buf[64];
    _snwprintf(buf, 63, L"%lu.%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, ubr);
    buf[63] = 0;
    return buf;
}

bool sendToRunning(const std::wstring& cmd) {
    HWND h = FindWindowW(kHostClass, nullptr);
    if (!h) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    AllowSetForegroundWindow(pid);   // damit das Einstellungsfenster nach vorne darf
    COPYDATASTRUCT cds;
    cds.dwData = kCopyDataId;
    cds.cbData = (DWORD)((cmd.size() + 1) * sizeof(wchar_t));
    cds.lpData = (PVOID)cmd.c_str();
    DWORD_PTR res = 0;
    return SendMessageTimeoutW(h, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &res) != 0 && res != 0;
}

}  // namespace

class App {
public:
    int run(const std::wstring& startCmd);
    std::wstring diagnostics();

private:
    struct Screen {
        MonitorInfo mon;
        std::unique_ptr<snow::World> world;
        bool hidden = false;          // komplett verdeckt: eingefroren, nichts wird gezeichnet
        bool presentedEmpty = true;   // zuletzt ein leeres Bild gezeigt
        bool needsClear = false;      // einmal leer zeichnen (z. B. ausgeschaltet, während verdeckt)
        bool fresh = false;
        uint64_t drawnVersion = ~0ull;   // zuletzt gezeigter Stand der Welt (unverändert: nicht neu zeichnen)
    };

    static LRESULT CALLBACK hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT hostMessage(UINT msg, WPARAM wp, LPARAM lp);
    void command(const std::wstring& cmd);
    void setEnabled(bool on);
    void setPreset(int preset);
    void shake();
    void openSettings();
    void onSettingsFromUi(const Settings& s);
    void applyParams();
    snow::Params currentParams() const;
    void setRealWeather(bool on);
    void maybeFetchWeather(double now);
    void onWeather(WeatherReply* r);
    std::wstring weatherStatus() const;
    double wantedFps() const;
    void scheduleSave();
    void saveNow();
    void publishState(bool fromUi);

    bool setupGraphics();
    void scheduleReattach(UINT ms);
    bool layerCall(std::function<void()> fn, DWORD timeoutMs);
    bool layerAttach(int wanted);
    void layerDetach();
    bool layerCheck();
    void updateHooks();
    void syncScreens(const std::vector<MonitorInfo>& mons);
    void scanWindows(double now);
    void frame();
    void modalFrame();
    void housekeeping();
    bool globallyPaused() const;
    bool shouldRun() const;
    void updateTimer();
    void armTimer(double waitSec);
    void scheduleNext();
    int currentFps() const;
    std::wstring statusText() const;
    void writeDiagnostics();

    HWND host_ = nullptr;
    UINT taskbarCreated_ = 0;
    HANDLE timer_ = nullptr;
    bool timerHighRes_ = false, timerArmed_ = false, timePeriodSet_ = false, housekeepingOn_ = false;
    double nextFrame_ = 0, lastFrame_ = 0, lastScan_ = -1;
    double nextGameCheck_ = 0, nextBusyCheck_ = 0, nextLayerCheck_ = 0, nextMonitorCheck_ = 0, nextRefresh_ = 0;
    int reattachTries_ = 0;
    bool reattachPending_ = false;   // während einer Pause angefallen: erst danach neu aufbauen
    bool wasPaused_ = false, hooksOn_ = false;
    UINT housekeepingMs_ = 0;
    bool ownDrag_ = false;
    bool graphicsOk_ = false;
    bool gamePaused_ = false, busyPaused_ = false;
    std::wstring gameName_, busyWhy_;
    double frameCpuMs_ = 0;
    uint64_t frames_ = 0;

    Settings settings_;
    std::vector<MonitorInfo> mons_;
    std::vector<Screen> screens_;
    DesktopLayer layer_;          // nur auf layerThread_ benutzen (Explorer-Kindfenster)
    LayerThread layerThread_;
    Renderer renderer_;
    WindowTracker tracker_;
    PauseWatch pause_;
    SettingsWindow ui_;
    std::vector<snow::PlatformIn> platforms_;
    std::vector<snow::Sprite> sprites_;
    std::vector<snow::PileVertex> mesh_;
    uint64_t seed_ = 0;
    // echtes Wetter
    WeatherClient weather_;
    wx::WeatherNow wxNow_;
    double wxFetched_ = 0, wxNextTry_ = 0;
    int wxFails_ = 0;
    unsigned wxPending_ = 0;
    std::wstring wxError_, wxTimeText_;
};

// ---------------------------------------------------------------- Start & Hauptschleife

int App::run(const std::wstring& startCmd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gin;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gin, nullptr);
    seed_ = GetTickCount64() * 2654435761ull + GetCurrentProcessId();

    logLine(L"SnowDesk %ls startet (Windows %ls)", kVersion, windowsVersion().c_str());
    settings_ = loadSettings();
    publishState(false);
    if (settings_.autostart != autostartRegistered()) setAutostart(settings_.autostart);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostProc;
    wc.hInstance = inst;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);
    // unsichtbares Hauptfenster (kein Nachrichten-only-Fenster: es soll Rundrufe wie WM_DISPLAYCHANGE bekommen)
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"SnowDesk", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, this);
    if (!host_) {
        logLine(L"Hauptfenster fehlgeschlagen (%lu)", GetLastError());
        return 1;
    }
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    weather_.setTarget(host_);
    ChangeWindowMessageFilterEx(host_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);

    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    timerHighRes_ = timer_ != nullptr;
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    pause_.init(host_);
    if (!layerThread_.start()) logLine(L"Fenster-Thread nicht gestartet – Ebenen-Fenster laufen im Haupt-Thread");
    if (!setupGraphics()) scheduleReattach(3000);   // z. B. direkt nach der Anmeldung, Explorer noch nicht bereit
    if (!startCmd.empty()) command(startCmd);
    updateTimer();

    MSG msg;
    int exitCode = 0;
    for (;;) {
        DWORD n = timerArmed_ ? 1 : 0;
        DWORD r = MsgWaitForMultipleObjectsEx(n, &timer_, INFINITE, QS_ALLINPUT, 0);
        if (n == 1 && r == WAIT_OBJECT_0) {
            timerArmed_ = false;
            if (shouldRun()) {
                frame();
                scheduleNext();
            }
        }
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                exitCode = (int)msg.wParam;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        updateTimer();
    }

    logLine(L"SnowDesk beendet");
    if (timePeriodSet_) timeEndPeriod(1);
    ui_.close();
    if (hooksOn_) tracker_.stop();
    pause_.shutdown();
    renderer_.shutdown();
    layerDetach();
    layerThread_.stop();
    if (timer_) CloseHandle(timer_);
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    return exitCode;
}

LRESULT CALLBACK App::hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* self;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->host_ = h;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->hostMessage(msg, wp, lp);
}

LRESULT App::hostMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated_ && msg == taskbarCreated_) {
        logLine(L"Explorer/Taskleiste neu gestartet: Desktop-Ebene neu anlegen");
        scheduleReattach(2000);
        return 0;
    }
    switch (msg) {
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kCopyDataId || !cds->lpData || cds->cbData > 4096) return FALSE;
            std::wstring cmd(reinterpret_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            // erst nach der Rückkehr ausführen: der Absender (Explorer) wartet nicht
            PostMessageW(host_, WM_APP_COMMAND, 0, reinterpret_cast<LPARAM>(new std::wstring(cmd)));
            return TRUE;
        }
        case WM_APP_COMMAND: {
            std::unique_ptr<std::wstring> cmd(reinterpret_cast<std::wstring*>(lp));
            if (cmd) command(*cmd);
            return 0;
        }
        case WM_APP_WEATHER: {
            std::unique_ptr<WeatherReply> r(reinterpret_cast<WeatherReply*>(lp));
            if (r) onWeather(r.get());
            return 0;
        }
        case WM_TIMER:
            switch (wp) {
                case kHousekeepingTimer: housekeeping(); break;
                case kSaveTimer:
                    KillTimer(host_, kSaveTimer);
                    saveNow();
                    break;
                case kReattachTimer:
                    KillTimer(host_, kReattachTimer);
                    if (globallyPaused() && g_headlessSeconds == 0) {
                        reattachPending_ = true;   // nicht während eines Spiels umbauen
                        break;
                    }
                    if (!setupGraphics()) {
                        ++reattachTries_;
                        scheduleReattach(reattachTries_ < 10 ? 3000 : 15000);
                    } else {
                        reattachTries_ = 0;
                    }
                    break;
                case kModalTimer: modalFrame(); break;
                default: break;
            }
            return 0;
        case WM_DISPLAYCHANGE:
            scheduleReattach(800);
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA) {
                std::vector<MonitorInfo> mons = enumMonitors();
                bool layoutSame = mons.size() == mons_.size();
                for (size_t i = 0; layoutSame && i < mons.size(); ++i) layoutSame = mons[i].sameLayout(mons_[i]);
                if (!layoutSame) {
                    scheduleReattach(500);
                } else {
                    mons_ = mons;   // nur die Taskleiste hat sich geändert: Böden anpassen, Schnee bleibt
                    for (size_t i = 0; i < screens_.size() && i < mons.size(); ++i) screens_[i].mon = mons[i];
                    tracker_.markDirty();
                }
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            pause_.onSessionChange(wp);
            if (wp == WTS_SESSION_UNLOCK) {
                nextLayerCheck_ = 0;
                tracker_.markDirty();
            }
            updateTimer();
            return 0;
        case WM_POWERBROADCAST:
            pause_.onPowerBroadcast(wp, lp);
            if (wp == PBT_APMRESUMEAUTOMATIC) scheduleReattach(3000);
            updateTimer();
            return TRUE;
        case WM_APP_LAYERLOST:
            logLine(L"Desktop-Ebene wurde von Windows entfernt: neu anlegen");
            graphicsOk_ = false;
            scheduleReattach(1500);
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) saveNow();
            return 0;
        case WM_CLOSE:
            DestroyWindow(host_);
            return 0;
        case WM_DESTROY:
            saveNow();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(host_, msg, wp, lp);
}

// ---------------------------------------------------------------- Befehle

void App::command(const std::wstring& raw) {
    std::wstring cmd = trimLower(raw);
    logLine(L"Befehl: %ls", cmd.empty() ? L"(Einstellungen)" : cmd.c_str());
    if (cmd == L"on") setEnabled(true);
    else if (cmd == L"off") setEnabled(false);
    else if (cmd == L"toggle") setEnabled(!settings_.enabled);
    else if (cmd == L"light") setPreset(PresetLight);
    else if (cmd == L"medium") setPreset(PresetMedium);
    else if (cmd == L"storm") setPreset(PresetStorm);
    else if (cmd == L"weather") setRealWeather(true);
    else if (cmd == L"weather-off") setRealWeather(false);
    else if (cmd == L"shake") shake();
    else if (cmd == L"settings" || cmd.empty()) openSettings();
    else if (cmd == L"exit") DestroyWindow(host_);
    else if (cmd == L"reload") {
        Settings s = loadSettings();
        bool on = s.enabled;
        s.enabled = settings_.enabled;
        settings_ = s;
        applyParams();
        setEnabled(on);
        setupGraphics();
    } else if (cmd == L"test") {
        setEnabled(true);
        for (Screen& s : screens_) s.world->prefill();
        updateTimer();
    } else if (cmd.compare(0, 7, L"render:") == 0) {
        int m = _wtoi(cmd.c_str() + 7);
        if (m >= RenderBehind && m <= RenderFront) {
            settings_.render = m;
            saveNow();
            setupGraphics();
            publishState(false);
        }
    } else if (cmd == L"diag") {
        writeDiagnostics();
    } else if (cmd.compare(0, 7, L"wxtest:") == 0) {
        // Test ohne Internet: wxtest:<Wettercode>:<Temperatur>:<Wind km/h>:<Windrichtung>
        wx::WeatherNow w;
        w.valid = true;
        float vals[4] = {63, 6, 15, 250};
        int n = swscanf(cmd.c_str() + 7, L"%f:%f:%f:%f", &vals[0], &vals[1], &vals[2], &vals[3]);
        (void)n;
        w.code = (int)vals[0];
        w.tempC = vals[1];
        w.windKmh = vals[2];
        w.windDirDeg = vals[3];
        w.time = "Test";
        wxNow_ = w;
        wxFetched_ = nowSec();
        wxNextTry_ = nowSec() + 24 * 3600.0;
        wxTimeText_ = L"Test";
        settings_.realWeather = true;
        logLine(L"Wetter (Test): %ls", wx::describeWeather(w).c_str());
        applyParams();
        if (!settings_.enabled) setEnabled(true);
        publishState(false);
        updateTimer();
    } else {
        logLine(L"Unbekannter Befehl: %ls", cmd.c_str());
    }
}

void App::setEnabled(bool on) {
    settings_.enabled = on;
    for (Screen& s : screens_) {
        snow::World& w = *s.world;
        if (on) {
            bool idle = !w.snowing() || w.thawing();
            bool empty = !w.active();
            w.resume();
            if (idle && empty) w.prefill();
        } else if (!w.thawing() && w.snowing()) {
            if (s.hidden || globallyPaused()) {
                w.clear();               // kann gerade nicht sichtbar abtauen: sofort weg
                w.setSnowing(false);
                s.needsClear = true;
            } else {
                w.startThaw();           // taut in ~2 s weg
            }
        }
    }
    publishState(false);
    scheduleSave();
    updateTimer();
}

void App::setPreset(int preset) {
    settings_.applyPreset(preset);
    settings_.realWeather = false;   // feste Stärke statt Wetter
    applyParams();
    if (!settings_.enabled) setEnabled(true);
    publishState(false);
    scheduleSave();
}

void App::shake() {
    for (Screen& s : screens_) s.world->shakeAll();
    updateTimer();
}

snow::Params App::currentParams() const {
    snow::Params p = settings_.params();
    if (settings_.realWeather) {
        // Echtes Wetter: was fällt, Wind, Nebel und Tauen kommen vom Wetter (ohne Daten fällt nichts)
        wx::WeatherLook L = wx::lookFor(wxNow_);
        p.amount = L.snow;
        p.rain = L.rain;
        p.drizzle = L.drizzle;
        p.wetSnow = L.wetSnow;
        p.wind = L.wind;
        p.fog = L.fog;
        p.thunder = L.thunder;
        p.meltDipPerMin = L.meltDipPerMin;
    }
    return p;
}

void App::applyParams() {
    snow::Params p = currentParams();
    for (Screen& s : screens_) s.world->setParams(p);
    updateTimer();
}

void App::setRealWeather(bool on) {
    if (settings_.realWeather != on) {
        settings_.realWeather = on;
        if (on) wxNextTry_ = 0;   // gleich abrufen
        applyParams();
    }
    if (on && !settings_.enabled) setEnabled(true);
    maybeFetchWeather(nowSec());
    publishState(false);
    scheduleSave();
    updateTimer();
}

// Alle 15 Minuten das Wetter holen – nur wenn es gebraucht wird (an, nicht pausiert)
void App::maybeFetchWeather(double now) {
    if (!settings_.realWeather || !settings_.enabled || globallyPaused() || wxPending_ != 0 || weather_.busy()) return;
    if (now < wxNextTry_) return;
    wxPending_ = weather_.fetchWeather(settings_.lat, settings_.lon);
    wxNextTry_ = now + 90.0;   // Sicherheitsnetz, falls keine Antwort kommt
}

void App::onWeather(WeatherReply* r) {
    if (r->kind == 1) {   // Ortssuche aus dem Einstellungsfenster
        if (ui_.isOpen()) ui_.placesFound(r->ok, r->places, r->error);
        return;
    }
    if (r->id != wxPending_) return;   // veraltet (Ort inzwischen geändert)
    wxPending_ = 0;
    const double now = nowSec();
    if (r->ok) {
        wxNow_ = r->weather;
        wxFetched_ = now;
        wxFails_ = 0;
        wxError_.clear();
        wxNextTry_ = now + 15 * 60.0;
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[16];
        _snwprintf(buf, 15, L"%02d:%02d", st.wHour, st.wMinute);
        buf[15] = 0;
        wxTimeText_ = buf;
        logLine(L"Wetter %ls: %ls", settings_.place.c_str(), wx::describeWeather(wxNow_).c_str());
    } else {
        ++wxFails_;
        wxError_ = r->error;
        static const int kBackoff[] = {60, 120, 300, 600, 900};
        int wait = kBackoff[std::min(wxFails_, 5) - 1];
        wxNextTry_ = now + wait;
        logLine(L"Wetter nicht abrufbar: %ls (nächster Versuch in %d s)", r->error.c_str(), wait);
        if (wxNow_.valid && now - wxFetched_ > 3 * 3600.0) wxNow_ = wx::WeatherNow();   // zu alt: lieber nichts zeigen
    }
    applyParams();
    publishState(false);
}

std::wstring App::weatherStatus() const {
    std::wstring s = settings_.placeDetail.empty() ? settings_.place : settings_.placeDetail;
    if (!settings_.realWeather) return s + L" – aus (es gilt die feste Stärke)";
    if (wxNow_.valid) return s + L": " + wx::describeWeather(wxNow_) + (wxTimeText_.empty() ? L"" : L" · Stand " + wxTimeText_);
    if (wxPending_ != 0) return s + L": Wetter wird abgerufen …";
    if (!wxError_.empty()) return s + L": " + wxError_;
    if (!settings_.enabled) return s + L": Schnee ist aus";
    return s + L": noch keine Wetterdaten";
}

void App::openSettings() {
    SettingsWindow::Callbacks cb;
    cb.changed = [this](const Settings& s) { onSettingsFromUi(s); };
    cb.shake = [this]() { shake(); };
    cb.status = [this]() { return statusText(); };
    cb.modalLoop = [this](bool on) {
        ownDrag_ = on;
        if (on) SetTimer(host_, kModalTimer, 15, nullptr);
        else KillTimer(host_, kModalTimer);
        tracker_.markDirty();
    };
    cb.monitorHz = [this]() {
        int hz = 60;
        for (const Screen& s : screens_) hz = std::max(hz, s.mon.hz);
        return hz;
    };
    cb.searchPlace = [this](const std::wstring& name) { return weather_.searchPlace(name) != 0; };
    cb.weatherText = [this]() { return weatherStatus(); };
    ui_.open(settings_, cb);
}

void App::onSettingsFromUi(const Settings& n) {
    Settings old = settings_;
    settings_ = n;
    settings_.enabled = old.enabled;
    const bool placeChanged = old.lat != n.lat || old.lon != n.lon;
    if (placeChanged) {   // anderer Ort: altes Wetter gilt nicht mehr, gleich neu abrufen
        wxNow_ = wx::WeatherNow();
        wxPending_ = 0;
        wxError_.clear();
        wxTimeText_.clear();
        wxNextTry_ = 0;
    }
    if (!old.realWeather && n.realWeather && !wxNow_.valid) wxNextTry_ = 0;   // eingeschaltet: gleich abrufen
    if (old.amount != n.amount || old.wind != n.wind || old.flakeSize != n.flakeSize || old.fallSpeed != n.fallSpeed ||
        old.maxPile != n.maxPile || old.fillMinutes != n.fillMinutes || old.crystals != n.crystals || old.release != n.release ||
        old.mouseBlow != n.mouseBlow || old.realWeather != n.realWeather || placeChanged)
        applyParams();
    if (old.enabled != n.enabled) setEnabled(n.enabled);
    if (n.realWeather) maybeFetchWeather(nowSec());
    if (old.autostart != n.autostart) setAutostart(n.autostart);
    if (old.games != n.games || old.pauseGames != n.pauseGames) {
        nextGameCheck_ = 0;
        if (!n.pauseGames) gamePaused_ = false;
    }
    if (old.render != n.render) setupGraphics();
    publishState(true);
    scheduleSave();
    updateTimer();
}

void App::scheduleSave() { SetTimer(host_, kSaveTimer, 600, nullptr); }

void App::saveNow() {
    if (!saveSettings(settings_)) logLine(L"Einstellungen konnten nicht gespeichert werden");
    mirrorToRegistry(settings_, true);
}

// Zustand für das Kontextmenü (Registry) und das Einstellungsfenster
void App::publishState(bool fromUi) {
    mirrorToRegistry(settings_, true);
    if (!fromUi && ui_.isOpen()) ui_.refresh(settings_);
}

// ---------------------------------------------------------------- Grafik & Desktop-Ebene

void App::scheduleReattach(UINT ms) {
    if (globallyPaused() && g_headlessSeconds == 0) {
        reattachPending_ = true;   // z. B. Auflösungswechsel beim Spielstart: erst nach dem Spiel neu aufbauen
        return;
    }
    if (host_) SetTimer(host_, kReattachTimer, ms, nullptr);
}

// Aufgabe auf dem Fenster-Thread (der die Explorer-Kindfenster besitzt); ohne Thread direkt
bool App::layerCall(std::function<void()> fn, DWORD timeoutMs) {
    if (!layerThread_.busy() && layerThread_.call(fn, timeoutMs)) return true;
    if (layerThread_.busy()) logLine(L"Explorer reagiert gerade nicht – Desktop-Ebene später erneut");
    return false;
}

bool App::layerAttach(int wanted) {
    auto ok = std::make_shared<bool>(false);
    std::vector<MonitorInfo> mons = mons_;
    HWND host = host_;
    DesktopLayer* layer = &layer_;
    bool done = layerCall([layer, ok, wanted, mons, host]() { *ok = layer->attach(wanted, mons, host); }, 8000);
    return done && *ok;
}

void App::layerDetach() {
    DesktopLayer* layer = &layer_;
    layerCall([layer]() { layer->detach(); }, 5000);
}

bool App::layerCheck() {
    auto ok = std::make_shared<bool>(true);
    DesktopLayer* layer = &layer_;
    if (!layerCall([layer, ok]() { *ok = layer->check(); }, 1500)) return true;   // Explorer beschäftigt: nicht umbauen
    return *ok;
}

// System-Ereignisse nur abonnieren, solange es etwas zu beobachten gibt (nicht im Spiel, nicht wenn alles aus ist)
void App::updateHooks() {
    bool anyActive = false;
    for (const Screen& s : screens_) anyActive = anyActive || s.world->active();
    bool want = (settings_.enabled || anyActive) && graphicsOk_ && !globallyPaused();
    if (want && !hooksOn_) {
        tracker_.start();
        hooksOn_ = true;
    } else if (!want && hooksOn_) {
        tracker_.stop();
        hooksOn_ = false;
    }
}

void App::syncScreens(const std::vector<MonitorInfo>& mons) {
    std::vector<Screen> next;
    for (const MonitorInfo& m : mons) {
        Screen s;
        s.mon = m;
        for (Screen& old : screens_) {
            if (old.world && old.mon.device == m.device) {
                s.world = std::move(old.world);
                s.hidden = old.hidden;
                s.fresh = !old.mon.sameLayout(m);   // andere Größe/Skalierung: configure() leert die Welt
                break;
            }
        }
        if (!s.world) {
            s.world.reset(new snow::World(++seed_ * 0x9E3779B97F4A7C15ull));
            s.fresh = true;
        }
        s.world->configure(m.width(), m.height(), m.scale);
        s.world->setParams(currentParams());
        if (!settings_.enabled && !s.world->thawing()) s.world->setSnowing(false);
        next.push_back(std::move(s));
    }
    screens_ = std::move(next);
    mons_ = mons;
}

bool App::setupGraphics() {
    if (layerThread_.busy()) {
        logLine(L"Explorer reagiert nicht – Desktop-Ebene später erneut");
        return false;
    }
    renderer_.clearSurfaces();
    layerDetach();
    graphicsOk_ = false;
    std::vector<MonitorInfo> mons = enumMonitors();
    for (const MonitorInfo& m : mons) logLine(L"Monitor: %ls", describe(m).c_str());
    syncScreens(mons);
    if (screens_.empty()) return false;
    if (g_headlessSeconds > 0) {
        if (!renderer_.ready() && !renderer_.init(false)) return false;
        for (const Screen& s : screens_) renderer_.addOffscreenSurface(s.mon.width(), s.mon.height());
        graphicsOk_ = renderer_.surfaceCount() == screens_.size();
        scanWindows(nowSec());
        for (Screen& s : screens_) {
            if (s.fresh && settings_.enabled) s.world->prefill();
            s.fresh = false;
        }
        updateTimer();
        return graphicsOk_;
    }
    // Grafikgerät während einer Pause verloren (Treiber-Update, Ruhezustand)? Dann neu anlegen
    if (renderer_.ready() && renderer_.deviceLost()) {
        logLine(L"Grafikgerät ungültig: wird neu angelegt");
        renderer_.shutdown();
    }
    if (!renderer_.ready() && !renderer_.init()) return false;

    int wanted = settings_.render == RenderFront ? LayerFront : (settings_.render == RenderAlt ? LayerWallpaper : LayerBehind);
    int tried = 0;
    bool reinitTried = false;
    while (tried++ < 4) {
        if (!layerAttach(wanted)) break;
        bool ok = layer_.count() == screens_.size();
        for (size_t i = 0; ok && i < screens_.size(); ++i) ok = renderer_.addSurface(layer_.window(i), screens_[i].mon.width(), screens_[i].mon.height());
        if (ok) {
            graphicsOk_ = true;
            break;
        }
        int failed = layer_.mode();
        renderer_.clearSurfaces();
        layerDetach();
        if (renderer_.deviceLost() && !reinitTried) {
            // nicht die Ebene ist schuld, sondern das Grafikgerät: einmal neu anlegen und denselben Weg erneut
            reinitTried = true;
            renderer_.shutdown();
            if (!renderer_.init()) return false;
            continue;
        }
        // Ebene steht, aber DirectComposition lässt sich dort nicht anbinden: nächsten Weg probieren
        logLine(L"Zeichnen %ls nicht möglich", layerName(failed));
        if (failed == LayerFront) break;
        wanted = failed == LayerBehind ? LayerWallpaper : LayerFront;
    }
    if (!graphicsOk_) {
        logLine(L"Keine Desktop-Ebene verfügbar");
        return false;
    }
    for (Screen& s : screens_) {
        s.presentedEmpty = true;
        s.needsClear = false;
    }
    scanWindows(nowSec());
    for (Screen& s : screens_) {
        if (s.fresh && settings_.enabled) s.world->prefill();
        s.fresh = false;
    }
    nextLayerCheck_ = nowSec() + 3.0;
    updateTimer();
    return true;
}

void App::scanWindows(double now) {
    std::vector<HWND> ignore;
    for (size_t i = 0; i < layer_.count(); ++i) ignore.push_back(layer_.window(i));
    tracker_.scan(ignore);
    for (Screen& s : screens_) {
        tracker_.platformsFor(s.mon, platforms_);
        s.world->setPlatforms(platforms_);
        bool hidden = tracker_.covered(s.mon);
        if (hidden != s.hidden) {
            s.hidden = hidden;
            logLine(L"%ls: %ls", s.mon.device.c_str(), hidden ? L"komplett verdeckt – pausiert" : L"wieder sichtbar");
        }
    }
    lastScan_ = now;
}

// ---------------------------------------------------------------- Bilder

bool App::globallyPaused() const { return pause_.locked() || pause_.displayOff() || gamePaused_ || busyPaused_; }

bool App::shouldRun() const {
    if (!graphicsOk_) return false;
    for (const Screen& s : screens_) {
        if (s.needsClear) return true;
    }
    if (globallyPaused()) return false;
    return wantedFps() > 0.0;
}

// Wie viele Bilder pro Sekunde gerade nötig sind: volle Rate, solange etwas fällt; wenige für Nebel, rollende Tropfen
// und Tauwetter; keins, wenn sich nichts bewegt und das letzte Bild schon zu sehen ist
double App::wantedFps() const {
    const double full = currentFps();
    double want = 0.0;
    for (const Screen& s : screens_) {
        if (s.needsClear) return full;
        if (s.hidden) continue;
        const snow::World& w = *s.world;
        float hint = w.frameRateHint();
        if (hint < 0.0f) return full;
        if (hint > 0.0f) want = std::max(want, std::min(full, (double)hint));
        if (w.version() != s.drawnVersion || (!w.active() && !s.presentedEmpty)) want = std::max(want, std::min(full, 30.0));
    }
    return want;
}

int App::currentFps() const {
    if (settings_.fps > 0) return settings_.fps;
    int hz = 60;
    for (const Screen& s : screens_) hz = std::max(hz, s.mon.hz);
    return std::max(30, std::min(240, hz));
}

void App::armTimer(double waitSec) {
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(std::max(0.0, waitSec) * 1e7);
    if (due.QuadPart == 0) due.QuadPart = -1;
    SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
    timerArmed_ = true;
}

void App::updateTimer() {
    bool paused = globallyPaused();
    if (paused != wasPaused_) {
        wasPaused_ = paused;
        if (!paused) {
            // Pause vorbei: aufgeschobenen Neuaufbau nachholen, Fenster neu einlesen
            if (reattachPending_) {
                reattachPending_ = false;
                scheduleReattach(300);
            }
            tracker_.markDirty();
            tracker_.requestRefresh();
            nextMonitorCheck_ = 0;
            nextLayerCheck_ = 0;
        }
    }
    updateHooks();
    bool run = shouldRun();
    if (run && !timerArmed_) {
        if (!timerHighRes_ && !timePeriodSet_) timePeriodSet_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        double now = nowSec();
        if (now - lastFrame_ > 0.25) lastFrame_ = now - 1.0 / currentFps();
        nextFrame_ = now;
        armTimer(0.0);
    } else if (!run && timerArmed_) {
        CancelWaitableTimer(timer_);
        timerArmed_ = false;
    }
    if (!run && timePeriodSet_) {
        timeEndPeriod(1);
        timePeriodSet_ = false;
    }
    // Aufräum-Takt nur, solange Schnee an ist oder noch etwas zu sehen ist – sonst schläft das Programm ganz
    bool anyActive = false;
    for (const Screen& s : screens_) anyActive = anyActive || s.world->active() || !s.presentedEmpty;
    bool need = settings_.enabled || run || anyActive || !graphicsOk_;
    UINT interval = paused ? 3000 : 500;   // in der Pause nur noch selten nachsehen (Spiel beendet?)
    if (need && (!housekeepingOn_ || housekeepingMs_ != interval)) {
        SetTimer(host_, kHousekeepingTimer, interval, nullptr);
        housekeepingOn_ = true;
        housekeepingMs_ = interval;
    } else if (!need && housekeepingOn_) {
        KillTimer(host_, kHousekeepingTimer);
        housekeepingOn_ = false;
    }
}

void App::scheduleNext() {
    if (!shouldRun()) {
        updateTimer();
        return;
    }
    double fps = wantedFps();
    if (fps <= 0.0) {
        updateTimer();
        return;
    }
    double period = 1.0 / fps;
    double now = nowSec();
    nextFrame_ += period;
    if (nextFrame_ < now - 2.0 * period) nextFrame_ = now;   // weit zurückgefallen: neu ausrichten
    armTimer(nextFrame_ - now);
}

void App::frame() {
    double now = nowSec();
    float dt = (float)std::max(0.0, std::min(0.5, now - lastFrame_));   // die Welt begrenzt selbst (0,1 s, solange etwas fällt)
    lastFrame_ = now;
    bool paused = globallyPaused();
    if (!paused && (tracker_.dirty() || tracker_.dragging() || ownDrag_ || now - lastScan_ >= 0.1)) scanWindows(now);

    POINT cur{};
    bool haveCur = !paused && settings_.mouseBlow && GetCursorPos(&cur);
    for (size_t i = 0; i < screens_.size(); ++i) {
        Screen& s = screens_[i];
        bool simulate = !paused && !s.hidden;
        if (!simulate && !s.needsClear) continue;
        snow::World& w = *s.world;
        if (simulate) {
            if (haveCur) w.setCursor(PtInRect(&s.mon.rect, cur) != FALSE, (float)(cur.x - s.mon.rect.left), (float)(cur.y - s.mon.rect.top));
            else w.setCursor(false, 0.0f, 0.0f);
            w.step(dt);
        }
        bool active = w.active();
        if (!active && s.presentedEmpty && !s.needsClear) {
            s.drawnVersion = w.version();
            continue;
        }
        if (!s.needsClear && active && w.version() == s.drawnVersion && !s.presentedEmpty) continue;   // nichts verändert
        if (s.needsClear || !active) {
            sprites_.clear();
            mesh_.clear();
        } else {
            w.collectSprites(sprites_);
            w.buildPileMesh(mesh_);
        }
        if (i >= renderer_.surfaceCount()) continue;
        Renderer::Result r = renderer_.draw(i, sprites_, mesh_, s.mon.scale);
        if (r == Renderer::DeviceLost || r == Renderer::Failed) {
            // Grafikgerät weg (Treiber-Update, Grafikkarte gewechselt) oder Fenster ungültig: alles neu aufbauen
            graphicsOk_ = false;
            if (r == Renderer::DeviceLost) renderer_.shutdown();
            renderer_.clearSurfaces();
            layerDetach();
            scheduleReattach(r == Renderer::DeviceLost ? 500 : 1500);
            return;
        }
        if (r == Renderer::Ok) {
            s.presentedEmpty = sprites_.empty() && mesh_.empty();
            s.needsClear = false;
            s.drawnVersion = w.version();
        }
    }
    double ms = (nowSec() - now) * 1000.0;
    frameCpuMs_ = frames_ == 0 ? ms : frameCpuMs_ * 0.97 + ms * 0.03;
    ++frames_;
}

// Während eigene Fenster gezogen werden (modale Schleife), kommt die Hauptschleife nicht dran: per WM_TIMER weiterzeichnen
void App::modalFrame() {
    double now = nowSec();
    if (!shouldRun() || now < nextFrame_ - 0.002) return;
    frame();
    nextFrame_ = now + 1.0 / currentFps();
}

void App::housekeeping() {
    double now = nowSec();
    if (g_headlessSeconds > 0) {
        static double start = now, nextShot = now + 5.0;
        static int shot = 0;
        if (now >= nextShot && renderer_.surfaceCount() > 0) {
            std::vector<unsigned char> bgra;
            if (renderer_.snapshot(0, bgra)) {
                int w = screens_[0].mon.width(), h = screens_[0].mon.height();
                wchar_t name[64];
                _snwprintf(name, 63, L"headless-%02d.tga", ++shot);
                name[63] = 0;
                HANDLE f = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (unsigned char)(w & 255), (unsigned char)(w >> 8),
                                             (unsigned char)(h & 255), (unsigned char)(h >> 8), 32, 0x28};
                    DWORD wr = 0;
                    WriteFile(f, hdr, 18, &wr, nullptr);
                    WriteFile(f, bgra.data(), (DWORD)bgra.size(), &wr, nullptr);
                    CloseHandle(f);
                }
            }
            logLine(L"Test: %llu Bilder, %.3f ms pro Bild, Scan %.0f µs, %zu Fenster", (unsigned long long)frames_, frameCpuMs_,
                    tracker_.lastScanMicros(), tracker_.windows().size());
            writeDiagnostics();
            nextShot = now + 5.0;
        }
        if (now - start > g_headlessSeconds) DestroyWindow(host_);
    }
    if (settings_.enabled && settings_.pauseGames && now >= nextGameCheck_) {
        std::wstring which;
        bool g = pause_.gameRunning(settings_.gameList(), &which);
        if (g != gamePaused_) {
            gamePaused_ = g;
            gameName_ = which;
            logLine(g ? L"Spiel erkannt (%ls): pausiert" : L"Spiel beendet: Schnee läuft weiter%ls", which.c_str());
        }
        nextGameCheck_ = now + 3.0;
    }
    if ((!settings_.pauseGames || !settings_.enabled) && gamePaused_) gamePaused_ = false;
    if (settings_.enabled && now >= nextBusyCheck_) {
        std::wstring why;
        bool b = pause_.systemBusy(&why);
        if (b != busyPaused_) {
            busyPaused_ = b;
            busyWhy_ = why;
            logLine(b ? L"Pausiert: %ls" : L"Weiter (%ls vorbei)", why.empty() ? busyWhy_.c_str() : why.c_str());
        }
        nextBusyCheck_ = now + 2.0;
    }
    bool paused = globallyPaused();
    if (graphicsOk_ && !paused && g_headlessSeconds == 0 && now >= nextLayerCheck_) {
        if (!layerCheck()) {
            logLine(L"Desktop-Ebene ungültig: neu anlegen");
            graphicsOk_ = false;
            scheduleReattach(1000);
        }
        nextLayerCheck_ = now + 3.0;
    }
    if (graphicsOk_ && !paused && g_headlessSeconds == 0 && now >= nextMonitorCheck_) {
        if (!sameMonitors(enumMonitors(false), mons_)) {
            logLine(L"Monitore geändert: neu einrichten");
            scheduleReattach(300);
        }
        nextMonitorCheck_ = now + 5.0;
    }
    if (!paused && now >= nextRefresh_) {
        tracker_.requestRefresh();
        nextRefresh_ = now + 2.0;
    }
    maybeFetchWeather(now);
    // Läuft gerade kein Bild (alles verdeckt), trotzdem nachsehen, ob wieder etwas frei ist
    if (graphicsOk_ && settings_.enabled && !globallyPaused() && !timerArmed_) scanWindows(now);
    updateTimer();
}

// ---------------------------------------------------------------- Status & Diagnose

std::wstring App::statusText() const {
    if (!graphicsOk_) return L"Desktop-Ebene wird eingerichtet …";
    if (!settings_.enabled) return L"Schnee ist aus";
    if (pause_.locked()) return L"Pausiert – Bildschirm gesperrt";
    if (pause_.displayOff()) return L"Pausiert – Bildschirm aus";
    if (gamePaused_) return L"Pausiert – " + gameName_ + L" läuft";
    if (busyPaused_) return L"Pausiert – " + busyWhy_;
    int visible = 0;
    for (const Screen& s : screens_) visible += s.hidden ? 0 : 1;
    if (visible == 0) return L"Pausiert – alle Bildschirme verdeckt";
    std::wstring what = L"Schneit";
    if (settings_.realWeather) {
        wx::WeatherLook L = wx::lookFor(wxNow_);
        if (!wxNow_.valid) what = L"Echtes Wetter (wartet auf Daten)";
        else if (L.rain > 0.0f && L.snow > 0.0f) what = L"Schneeregen";
        else if (L.rain > 0.0f) what = L.thunder ? L"Gewitter" : (L.drizzle ? L"Niesel" : L"Regen");
        else if (L.snow > 0.0f) what = L"Schneit";
        else if (L.fog > 0.0f) what = L"Nebel";
        else what = L"Trocken";
    }
    wchar_t buf[256];
    _snwprintf(buf, 255, L"%ls auf %d von %d Bildschirm%ls · bis %d Bilder/s · %.2f ms Rechenzeit pro Bild · %ls", what.c_str(), visible,
               (int)screens_.size(), screens_.size() == 1 ? L"" : L"en", currentFps(), frameCpuMs_, layerName(layer_.mode()));
    buf[255] = 0;
    std::wstring s = buf;
    for (wchar_t& c : s) {
        if (c == L'.') c = L',';
    }
    return s;
}

std::wstring App::diagnostics() {
    std::wstring d = L"SnowDesk ";
    d += kVersion;
    d += L"\r\nWindows " + windowsVersion();
    d += L"\r\nGrafik: " + (renderer_.ready() ? renderer_.adapterName() : std::wstring(L"nicht bereit"));
    d += L"\r\nStatus: " + statusText();
    d += L"\r\n\r\nMonitore:";
    for (const Screen& s : screens_) d += L"\r\n  " + describe(s.mon) + (s.hidden ? L" (verdeckt)" : L"");
    auto layerText = std::make_shared<std::wstring>(L"Ebene: (Explorer antwortet nicht)");
    DesktopLayer* layer = &layer_;
    layerCall([layer, layerText]() { *layerText = layer->diagnostics(); }, 2000);
    d += L"\r\n\r\n" + *layerText;
    d += L"\r\n\r\nFenster (Scan " + std::to_wstring((int)tracker_.lastScanMicros()) + L" µs):\r\n";
    d += tracker_.describeWindows();
    d += L"\r\nEinstellungen: " + settingsPath();
    return d;
}

void App::writeDiagnostics() {
    std::wstring text = diagnostics();
    std::wstring path = dataDir() + L"diagnose.txt";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8((size_t)bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], bytes, nullptr, nullptr);
    DWORD written = 0;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    WriteFile(f, bom, 3, &written, nullptr);
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
    logLine(L"Diagnose geschrieben: %ls", path.c_str());
}

}  // namespace app

// ---------------------------------------------------------------- Einstieg

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    app::logInit();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cmd;
    bool autostart = false, haveCmd = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--pack-menu" && i + 1 < argc) {
            int rc = app::packMenu(argv[i + 1]);
            LocalFree(argv);
            return rc;
        }
        if (a == L"--headless-test" && i + 1 < argc) {
            app::g_headlessSeconds = std::max(5, _wtoi(argv[++i]));
            autostart = true;
            continue;
        }
        if (a == L"--autostart") autostart = true;
        else if (a == L"--cmd" && i + 1 < argc) {
            cmd = argv[++i];
            haveCmd = true;
        } else if (a == L"--exit") {
            cmd = L"exit";
            haveCmd = true;
        } else if (a == L"--diag") {
            cmd = L"diag";
            haveCmd = true;
        } else if (a == L"--settings") {
            cmd = L"settings";
            haveCmd = true;
        }
    }
    if (argv) LocalFree(argv);
    std::wstring c = app::trimLower(cmd);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, app::kMutexName);
    DWORD mutexError = GetLastError();
    // ERROR_ACCESS_DENIED: eine Instanz mit Adminrechten läuft schon (z. B. Setup als Administrator gestartet)
    bool running = (mutex && mutexError == ERROR_ALREADY_EXISTS) || (!mutex && mutexError == ERROR_ACCESS_DENIED);
    if (running) {
        // läuft schon: Befehl weiterreichen (ohne Befehl: Einstellungen öffnen; beim Autostart nichts tun)
        if (autostart && !haveCmd) return 0;
        std::wstring send = haveCmd ? c : L"settings";
        for (int tries = 0; tries < 30; ++tries) {
            if (app::sendToRunning(send)) return 0;
            Sleep(100);   // startet vielleicht gerade erst
        }
        return 2;
    }
    if (haveCmd && (c == L"exit" || c == L"shake")) return 0;   // nichts zu tun
    if (haveCmd && c == L"toggle") c = L"on";                    // läuft nicht = Schnee ist sichtbar aus
    if (haveCmd && c == L"off") {
        app::Settings s = app::loadSettings();
        s.enabled = false;
        app::saveSettings(s);
        app::mirrorToRegistry(s, false);
        return 0;
    }
    std::wstring start = haveCmd ? c : (autostart ? std::wstring() : std::wstring(L"settings"));
    app::App a;
    int rc = a.run(start);
    if (mutex) CloseHandle(mutex);
    return rc;
}
