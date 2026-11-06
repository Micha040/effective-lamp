// DeskCats – Hauptprogramm (siehe app.h)
//
// Sparsamkeit: Es gibt keinen festen Takt. Jede Katze meldet, wann sie das nächste Mal dran ist (beim Laufen
// 12-mal pro Sekunde, beim Dösen alle paar Sekunden). Einmal berechnete Bilder werden wiederverwendet.
// Bei Spielen, Vollbild, gesperrtem Bildschirm oder ausgeblendeten Katzen schläft das Programm komplett.
#include "app.h"

#include "monitors.h"
#include "pause.h"
#include "scene.h"
#include "settings.h"
#include "settings_ui.h"
#include "sprite.h"

#include "../core/brain.h"
#include "../core/pixcat.h"
#include "../core/props.h"

#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace app {

namespace {

const wchar_t* kMutexName = L"Local\\DeskCats.Instance";
constexpr UINT WM_APP_COMMAND = WM_APP + 1;
constexpr UINT WM_APP_SCENE = WM_APP + 2;   // Fenster haben sich geändert
enum : UINT_PTR { kTickTimer = 1, kFollowTimer = 2, kHousekeepingTimer = 3, kSelftestTimer = 4 };
constexpr int kItemIdBowl = 100, kItemIdToy = 101;
constexpr float kPpc = 1.25f;   // Sprite-Pixel pro Zentimeter (PixelStyle)

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

double processCpuSeconds() {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    auto sec = [](FILETIME f) { return (double)(((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime) / 1e7; };
    return sec(k) + sec(u);
}

}  // namespace

bool sendToRunning(const std::wstring& cmd) {
    HWND h = FindWindowW(kHostClass, nullptr);
    if (!h) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cds;
    cds.dwData = kCopyDataId;
    cds.cbData = (DWORD)((cmd.size() + 1) * sizeof(wchar_t));
    cds.lpData = (PVOID)cmd.c_str();
    DWORD_PTR res = 0;
    return SendMessageTimeoutW(h, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 3000, &res) != 0 && res != 0;
}

// ------------------------------------------------------------------ eine Katze auf dem Bildschirm

struct CatSlot {
    int look = 0;   // 0 Fatos, 1 Kosta
    cat::Look lk;
    std::unique_ptr<cat::CatBrain> brain;
    SpriteWindow win;
    double nextT = 0;
    uint64_t lastKey = 0;
    int lastScale = 0;
    int lastLeft = 0, lastTop = 0;
    bool lastValid = false;
    uint64_t viewPlat = 0;     // Fläche beim letzten Bild (für das flüssige Mitfahren beim Fensterziehen)
    int viewPlatX0 = 0, viewPlatY = 0;
    struct Cached {
        cat::PixelImage img;
        uint64_t used = 0;
    };
    std::unordered_map<uint64_t, Cached> cache;   // fertige Bilder je Pose (zuletzt benutzte bleiben)
    uint64_t useClock = 0;
    bool hidden = false;
};

// Bildspeicher klein halten: bei 160 Bildern die ältere Hälfte verwerfen (ein Bild hat 10–15 KB)
static void trimCache(CatSlot& c) {
    std::vector<std::pair<uint64_t, uint64_t>> ages;   // (zuletzt benutzt, Schlüssel)
    ages.reserve(c.cache.size());
    for (const auto& e : c.cache) ages.emplace_back(e.second.used, e.first);
    std::sort(ages.begin(), ages.end());
    for (size_t i = 0; i < ages.size() / 2; ++i) c.cache.erase(ages[i].second);
}

class App {
public:
    int run(const std::wstring& startCmd, bool autostart, int selftest);

private:
    static LRESULT CALLBACK hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp);
    void command(const std::wstring& cmd);
    void applySettings(const Settings& s, bool save);
    void syncCats();
    void placeNewCat(CatSlot& c, int order);
    void rebuild();
    cat::Senses senses(int i, double now, POINT cur, bool curValid);
    void heartbeat();
    void follow();
    void present(CatSlot& c, bool force);
    void presentItem(cat::Item& it, SpriteWindow& w, bool force);
    void updateZ(SpriteWindow& w, uint64_t plat, bool airborne, bool force);
    void scheduleNext(double delay);
    bool pausedNow(std::wstring* why, bool* needsPoll = nullptr);
    void updateHousekeeping();
    void hideAll();
    void onMouse(int id, UINT msg, POINT pt);
    void catMenu(int id, POINT pt);
    void itemMenu(int id, POINT pt);
    void openSettings();
    void spawnBowl(POINT at);
    void spawnToy(POINT at);
    void setVisible(bool v);
    void publish();
    bool isOwn(HWND h) const;
    std::vector<HWND> ownWindows() const;
    const cat::Platform* platformBelow(POINT p) const;
    void selftestReport();

    HINSTANCE inst_ = nullptr;
    HWND host_ = nullptr;
    Settings set_;
    PauseWatch pause_;
    Scene scene_;
    std::vector<MonitorInfo> mons_;
    cat::World world_;
    std::vector<std::unique_ptr<CatSlot>> cats_;
    cat::Item bowl_, toy_;
    SpriteWindow bowlWin_, toyWin_;
    bool bowlWinOk_ = false, toyWinOk_ = false;
    uint64_t bowlKey_ = ~0ull, toyKey_ = ~0ull;
    double lastItemStep_ = 0, toyTouched_ = 0, bowlEmptySince_ = 0;
    bool pausedState_ = false, gameRunning_ = false, monitorsDirty_ = true;
    std::wstring pauseWhy_;
    double lastBuild_ = -100, nextGameCheck_ = 0;
    bool timerArmed_ = false;
    bool housekeepingOn_ = false;
    double timerDue_ = 0;
    bool itemsWake_ = false;
    bool followOn_ = false;
    // Maus
    int pressId_ = -1;
    POINT pressPt_{0, 0};
    bool dragging_ = false;
    double lastDragT_ = 0;
    POINT lastDragPt_{0, 0};
    double itemVx_ = 0, itemVy_ = 0;
    bool publishedSleeping_ = false, publishedVisible_ = true, published_ = false;
    // Selbsttest
    int selftest_ = 0;
    double startT_ = 0, startCpu_ = 0;
    long ticks_ = 0, renders_ = 0, presents_ = 0, builds_ = 0, uploads_ = 0, moves_ = 0;
    bool selftestWarm_ = false;
    double warmT_ = 0, warmCpu_ = 0;
    long warmTicks_ = 0;
    long zPlaced_ = 0, zFailed_ = 0;
};

App* g_app = nullptr;

// ------------------------------------------------------------------ Start

int App::run(const std::wstring& startCmd, bool autostart, int selftest) {
    inst_ = GetModuleHandleW(nullptr);
    selftest_ = selftest;
    logInit();
    logLine(L"DeskCats %ls startet%ls", kVersion, autostart ? L" (Autostart)" : L"");
    set_ = loadSettings();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostProc;
    wc.hInstance = inst_;
    wc.lpszClassName = kHostClass;
    wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"DeskCats", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst_, nullptr);
    if (!host_) {
        logLine(L"Hauptfenster konnte nicht erstellt werden (%lu)", GetLastError());
        return 1;
    }
    ChangeWindowMessageFilterEx(host_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    pause_.init(host_);
    scene_.setNotify(host_, WM_APP_SCENE);
    if (!scene_.start()) logLine(L"Fenster-Beobachtung eingeschränkt (keine WinEvent-Hooks)");
    mons_ = enumMonitors(false);
    for (const MonitorInfo& m : mons_) logLine(L"Monitor: %ls", describe(m).c_str());
    syncCats();
    startT_ = nowSec();
    startCpu_ = processCpuSeconds();
    if (selftest_ > 0) SetTimer(host_, kSelftestTimer, (UINT)std::min(selftest_, 10) * 1000, nullptr);
    if (set_.visible) {   // läuft schon ein Spiel? Nicht erst nach 5 s merken
        std::wstring which;
        gameRunning_ = pause_.gameRunning(set_.games, &which);
        if (gameRunning_) logLine(L"Spiel erkannt (%ls): Katzen schlafen", which.c_str());
    }
    if (!startCmd.empty()) command(startCmd);
    heartbeat();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsDialogMessage(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    scene_.stop();
    pause_.shutdown();
    for (auto& c : cats_) c->win.destroy();
    bowlWin_.destroy();
    toyWin_.destroy();
    publishMenuState(set_.visible, false);   // "Visible" ist zugleich die Einstellung; das Menü prüft selbst, ob DeskCats läuft
    logLine(L"DeskCats beendet");
    return 0;
}

LRESULT CALLBACK App::hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app && g_app->host_ == h) return g_app->onMessage(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::onMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kTickTimer) {
                KillTimer(host_, kTickTimer);
                timerArmed_ = false;
                heartbeat();
            } else if (wp == kFollowTimer) {
                follow();
            } else if (wp == kHousekeepingTimer) {
                if (selftest_ > 0) {   // Testlauf: Lage der Katzen protokollieren (für automatisches Klicken/Ziehen)
                    for (const auto& c : cats_) {
                        RECT r = c->win.rect();
                        const cat::View& v = c->brain->view();
                        logLine(L"Lage %ls: Fenster %ld,%ld-%ld,%ld Fläche=%llx Handlung=%d sichtbar=%d", c->look == 0 ? L"Fatos" : L"Kosta", (long)r.left,
                                (long)r.top, (long)r.right, (long)r.bottom, (unsigned long long)v.platform, (int)v.act, c->win.shown() ? 1 : 0);
                    }
                }
                // Spiele-Liste prüfen (Prozessliste, keine fremden Prozesse öffnen) und ggf. aufwachen
                bool before = gameRunning_;
                std::wstring which;
                if (set_.visible && !pause_.locked() && !pause_.displayOff())   // ausgeblendet/gesperrt: nichts zu prüfen
                    gameRunning_ = pause_.gameRunning(set_.games, &which);
                if (gameRunning_ != before) {
                    logLine(gameRunning_ ? L"Spiel erkannt (%ls): Katzen schlafen" : L"Spiel beendet: Katzen sind wieder da", which.c_str());
                    heartbeat();
                } else if (!timerArmed_ && !pausedState_) {
                    heartbeat();
                }
            } else if (wp == kSelftestTimer) {
                if (!selftestWarm_) {   // erst nach 10 s Anlauf messen (Start, erste Bilder)
                    selftestWarm_ = true;
                    warmT_ = nowSec();
                    warmCpu_ = processCpuSeconds();
                    warmTicks_ = ticks_;
                    KillTimer(host_, kSelftestTimer);
                    SetTimer(host_, kSelftestTimer, (UINT)std::max(1, selftest_ - 10) * 1000, nullptr);
                } else {
                    selftestReport();
                    DestroyWindow(host_);
                }
            }
            return 0;
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kCopyDataId || !cds->lpData || cds->cbData < sizeof(wchar_t)) return FALSE;
            std::wstring cmd(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            auto* copy = new std::wstring(trimLower(cmd));
            PostMessageW(host_, WM_APP_COMMAND, 0, reinterpret_cast<LPARAM>(copy));   // nicht im SendMessage des Absenders arbeiten
            return TRUE;
        }
        case WM_APP_SCENE:
            scene_.clearNotified();
            if (!pausedState_) {
                // kurz sammeln (viele Ereignisse hintereinander), dann ein Takt
                scheduleNext(0.02);
            }
            return 0;
        case WM_APP_COMMAND: {
            std::unique_ptr<std::wstring> cmd(reinterpret_cast<std::wstring*>(lp));
            if (cmd) command(*cmd);
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
        case WM_SETTINGCHANGE:
            monitorsDirty_ = true;
            scene_.markDirty();
            heartbeat();
            return 0;
        case WM_WTSSESSION_CHANGE:
            pause_.onSessionChange(wp);
            heartbeat();
            return 0;
        case WM_POWERBROADCAST:
            pause_.onPowerBroadcast(wp, lp);
            heartbeat();
            return TRUE;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wp) saveSettings(set_);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(host_, msg, wp, lp);
}

// ------------------------------------------------------------------ Katzen anlegen / Einstellungen

void App::syncCats() {
    const bool want[2] = {set_.fatos, set_.kosta};
    for (int look = 0; look < 2; ++look) {
        auto it = std::find_if(cats_.begin(), cats_.end(), [&](const std::unique_ptr<CatSlot>& c) { return c->look == look; });
        if (want[look] && it == cats_.end()) {
            auto c = std::make_unique<CatSlot>();
            c->look = look;
            c->lk = look == 0 ? cat::lookFatos() : cat::lookKosta();
            cat::Personality p = look == 0 ? cat::personalityFatos() : cat::personalityKosta();
            float f = set_.activity == 0 ? 1.6f : (set_.activity == 2 ? 0.6f : 1.f);
            p.idleMin *= f;
            p.idleMax *= f;
            if (set_.activity == 0) p.wanderOther *= 0.6f;
            if (set_.activity == 2) p.wanderOther = std::min(0.9f, p.wanderOther * 1.4f);
            c->brain = std::make_unique<cat::CatBrain>(p, (uint32_t)GetTickCount() ^ (look * 7919u));
            int id = look;
            if (!c->win.create(inst_, id, [this](int i, UINT m, POINT pt) { onMouse(i, m, pt); })) {
                logLine(L"Fenster für Katze %d konnte nicht erstellt werden (%lu)", look, GetLastError());
                continue;
            }
            cats_.push_back(std::move(c));
            if (!mons_.empty()) {
                rebuild();
                placeNewCat(*cats_.back(), (int)cats_.size() - 1);
            }
        } else if (!want[look] && it != cats_.end()) {
            (*it)->win.destroy();
            cats_.erase(it);
        }
    }
}

void App::placeNewCat(CatSlot& c, int order) {
    // auf die Taskleiste (bzw. den Boden) des Hauptmonitors
    const cat::Platform* best = nullptr;
    for (const cat::Platform& p : world_.plats) {
        if (p.kind == cat::PlatKind::Window) continue;
        bool primary = p.monitor >= 0 && p.monitor < (int)world_.monitors.size();
        if (!best || (primary && p.x0 <= 0 && p.x1 > 0)) best = &p;
    }
    if (!best && !world_.plats.empty()) best = &world_.plats.front();
    if (!best) return;
    double x = best->x0 + (best->x1 - best->x0) * (order == 0 ? 0.36 : 0.62);
    c.brain->place(world_, *best, x, c.look == 0 ? cat::Posture::Loaf : cat::Posture::Sit);
    c.nextT = 0;
}

void App::applySettings(const Settings& s, bool save) {
    bool lookChanged = s.activity != set_.activity;
    set_ = s;
    if (save) saveSettings(set_);
    if (lookChanged) {   // Lebhaftigkeit gilt beim Erzeugen: Katzen neu anlegen (Position bleibt grob erhalten)
        for (auto& c : cats_) c->win.destroy();
        cats_.clear();
    }
    syncCats();
    scene_.markDirty();
    for (auto& c : cats_) {
        c->nextT = 0;
        c->lastValid = false;
    }
    publish();
    heartbeat();
}

// ------------------------------------------------------------------ Welt

std::vector<HWND> App::ownWindows() const {
    std::vector<HWND> v;
    for (const auto& c : cats_) v.push_back(c->win.hwnd());
    if (bowlWinOk_) v.push_back(bowlWin_.hwnd());
    if (toyWinOk_) v.push_back(toyWin_.hwnd());
    v.push_back(host_);
    return v;
}

bool App::isOwn(HWND h) const {
    for (const auto& c : cats_)
        if (c->win.hwnd() == h) return true;
    return h == host_ || (bowlWinOk_ && h == bowlWin_.hwnd()) || (toyWinOk_ && h == toyWin_.hwnd());
}

void App::rebuild() {
    if (monitorsDirty_) {
        mons_ = enumMonitors(false);
        monitorsDirty_ = false;
    }
    scene_.build(mons_, ownWindows(), set_, world_);
    lastBuild_ = nowSec();
    ++builds_;
    // Fenster, auf denen Katzen oder Gegenstände sitzen, genauer beobachten
    std::vector<HWND> watch;
    for (const auto& c : cats_) {
        HWND h = scene_.hwndOf(c->brain->platform());
        if (h && !Scene::isTaskbar(c->brain->platform())) watch.push_back(h);
    }
    for (cat::Item* it : {&bowl_, &toy_}) {
        HWND h = it->active ? scene_.hwndOf(it->platform) : nullptr;
        if (h && !Scene::isTaskbar(it->platform)) watch.push_back(h);
    }
    scene_.watch(watch);
}

const cat::Platform* App::platformBelow(POINT p) const {
    const int sc = world_.scaleAt(p.x, p.y);
    const cat::Platform* best = world_.nearestBelow(p.x, p.y, 4000);
    if (!best) {   // nichts darunter: nächste Taskleiste/Boden
        for (const cat::Platform& q : world_.plats)
            if (q.kind != cat::PlatKind::Window && (!best || std::abs((q.x0 + q.x1) / 2 - p.x) < std::abs((best->x0 + best->x1) / 2 - p.x))) best = &q;
    }
    (void)sc;
    return best;
}

cat::Senses App::senses(int i, double now, POINT cur, bool curValid) {
    cat::Senses s;
    s.now = now;
    s.world = &world_;
    s.cursorValid = curValid;
    s.cursorX = cur.x;
    s.cursorY = cur.y;
    s.bowl = &bowl_;
    s.toy = &toy_;
    for (size_t j = 0; j < cats_.size(); ++j) {
        if ((int)j == i) continue;
        const cat::CatBrain& o = *cats_[j]->brain;
        s.otherPresent = true;
        s.otherX = o.x();
        s.otherY = o.y();
        s.otherPlat = o.platform();
        s.otherSleeping = o.sleeping();
        s.otherBowlSide = o.bowlSide();
    }
    return s;
}

// ------------------------------------------------------------------ Takt

// needsPoll: nur "System beschäftigt" meldet sein Ende nicht selbst – Sperre, Bildschirm aus und Ausblenden kommen als
// Nachricht, das Ende eines Spiels erkennt der Wartungstakt an der Prozessliste.
bool App::pausedNow(std::wstring* why, bool* needsPoll) {
    if (needsPoll) *needsPoll = false;
    if (!set_.visible) {
        if (why) *why = L"ausgeblendet";
        return true;
    }
    if (pause_.locked()) {
        if (why) *why = L"Bildschirm gesperrt";
        return true;
    }
    if (pause_.displayOff()) {
        if (why) *why = L"Bildschirm aus";
        return true;
    }
    if (gameRunning_) {
        if (why) *why = L"Spiel läuft";
        return true;
    }
    bool busy = pause_.systemBusy(why);
    if (needsPoll) *needsPoll = busy;
    return busy;
}

// Wartungstakt (Spiele-Liste, Sicherheitsnetz) nur, wenn er etwas zu tun hat: nicht ausgeblendet, nicht gesperrt,
// Bildschirm an
void App::updateHousekeeping() {
    const bool want = selftest_ > 0 || (set_.visible && !pause_.locked() && !pause_.displayOff());
    if (want == housekeepingOn_) return;
    housekeepingOn_ = want;
    if (want) SetTimer(host_, kHousekeepingTimer, 5000, nullptr);
    else KillTimer(host_, kHousekeepingTimer);
}

void App::hideAll() {
    for (auto& c : cats_) {
        c->win.hide();
        c->lastValid = false;
    }
    if (bowlWinOk_) bowlWin_.hide();
    if (toyWinOk_) toyWin_.hide();
    bowlKey_ = toyKey_ = ~0ull;
}

void App::scheduleNext(double delay) {
    delay = std::max(0.005, std::min(delay, 30.0));
    double due = nowSec() + delay;
    if (timerArmed_ && due >= timerDue_ - 0.002) return;
    timerArmed_ = true;
    timerDue_ = due;
    SetTimer(host_, kTickTimer, (UINT)std::max(1.0, delay * 1000.0), nullptr);
}

void App::heartbeat() {
    const double now = nowSec();
    std::wstring why;
    bool needsPoll = false;
    bool paused = pausedNow(&why, &needsPoll);
    updateHousekeeping();
    if (paused) {
        if (!pausedState_) {
            logLine(L"Pause: %ls", why.c_str());
            hideAll();
            pausedState_ = true;
            KillTimer(host_, kFollowTimer);
            followOn_ = false;
            scene_.stop();   // keine Fensterereignisse mehr empfangen, solange nichts zu sehen ist
        }
        // Ruhe: kein Takt. Nur "System beschäftigt" muss gelegentlich nachgesehen werden.
        if (needsPoll) scheduleNext(5.0);
        else {
            KillTimer(host_, kTickTimer);
            timerArmed_ = false;
        }
        return;
    }
    if (pausedState_) {
        pausedState_ = false;
        logLine(L"Weiter");
        if (!scene_.start()) logLine(L"Fenster-Beobachtung eingeschränkt (keine WinEvent-Hooks)");
        scene_.markDirty();
        for (auto& c : cats_) c->nextT = now;
    }
    if (scene_.dirty() || monitorsDirty_ || now - lastBuild_ > 6.0) {
        rebuild();
        for (auto& cp : cats_) {   // Fläche bewegt oder weg: Katze sofort nachführen (fährt mit oder fällt)
            CatSlot& c = *cp;
            uint64_t plat = c.brain->platform();
            if (!plat) continue;
            const cat::Platform* p = world_.find(plat);
            if (!p || p->x0 != c.viewPlatX0 || p->y != c.viewPlatY) c.nextT = std::min(c.nextT, now);
        }
        for (cat::Item* it : {&bowl_, &toy_})
            if (it->active && it->platform && !world_.find(it->platform)) itemsWake_ = true;
    }

    POINT cur{0, 0};
    bool curValid = GetCursorPos(&cur) != FALSE;
    const std::vector<char> fsMons = scene_.fullscreenMonitors(mons_);
    const bool zCheck = scene_.zChanged();
    scene_.clearZChanged();
    double next = 30.0;
    for (size_t i = 0; i < cats_.size(); ++i) {
        CatSlot& c = *cats_[i];
        if (pressId_ == (int)i && dragging_) {   // hängt an der Maus
            next = std::min(next, 0.2);
            continue;
        }
        // Katze auf einem Monitor mit Vollbild-Programm: ausblenden und ruhen lassen
        int mi = -1;
        for (size_t m = 0; m < mons_.size(); ++m) {
            const RECT& r = mons_[m].rect;
            if (c.brain->x() >= r.left && c.brain->x() < r.right && c.brain->y() - 10 >= r.top && c.brain->y() - 10 < r.bottom) mi = (int)m;
        }
        if (mi >= 0 && mi < (int)fsMons.size() && fsMons[mi]) {
            if (!c.hidden) {
                c.win.hide();
                c.hidden = true;
                c.lastValid = false;
            }
            next = std::min(next, 2.0);
            continue;
        }
        if (c.hidden) {
            c.hidden = false;
            c.nextT = now;
        }
        if (now + 0.004 >= c.nextT) {
            cat::Senses s = senses((int)i, now, cur, curValid);
            double d = c.brain->tick(s);
            ++ticks_;
            c.nextT = now + d;
            double vx, vy;
            if (c.brain->takeBat(vx, vy) && toy_.active) {
                toy_.vx = vx;
                toy_.vy = vy;
                toyTouched_ = now;
                if (vy < 0) {
                    toy_.airborne = true;
                    toy_.platform = 0;
                }
            }
            present(c, false);
        } else if (c.win.shown()) {
            // Stapelreihenfolge prüfen (billig): Fenster können auch ohne Aktivierung nach vorne kommen
            const cat::View& v = c.brain->view();
            updateZ(c.win, v.platform, v.topmost, zCheck);
        }
        next = std::min(next, c.nextT - now);
        // sitzt sie auf einem Fenster, sekündlich nachsehen, ob sie noch richtig liegt
        if (c.win.shown() && c.brain->platform() && !Scene::isTaskbar(c.brain->platform()) && !Scene::isFloor(c.brain->platform()))
            next = std::min(next, 1.0);
    }
    // Napf und Knäuel
    bool itemsMoving = false;
    double dt = lastItemStep_ > 0 ? std::min(0.2, now - lastItemStep_) : 1.0 / cat::kFps;
    lastItemStep_ = now;
    for (cat::Item* it : {&bowl_, &toy_}) {
        if (!it->active) continue;
        bool isToy = it == &toy_;
        if (isToy && pressId_ == kItemIdToy && dragging_) continue;
        if (cat::stepItem(*it, world_, dt)) itemsMoving = true;
        if (it->airborne || std::fabs(it->vx) > 1) itemsMoving = true;
        presentItem(*it, isToy ? toyWin_ : bowlWin_, false);
    }
    // aufräumen: leerer Napf nach einer Minute, Knäuel nach drei Minuten ohne Spiel
    if (bowl_.active) {
        if (bowl_.food <= 0.05) {
            if (bowlEmptySince_ == 0) bowlEmptySince_ = now;
            if (now - bowlEmptySince_ > 60) {
                bowl_.active = false;
                bowlWin_.hide();
            }
        } else {
            bowlEmptySince_ = 0;
        }
    }
    if (toy_.active && now - toyTouched_ > 180) {
        toy_.active = false;
        toyWin_.hide();
    }
    if (itemsMoving || itemsWake_) next = std::min(next, 1.0 / cat::kFps);
    itemsWake_ = false;
    // Fenster wird gezogen und eine Katze sitzt darauf: flüssig mitfahren (nur Fenster verschieben, kein Neuzeichnen)
    bool wantFollow = false;
    if (scene_.dragging()) {
        uint64_t dragId = (uint64_t)(uintptr_t)scene_.dragWindow();
        for (const auto& c : cats_)
            if (c->brain->platform() == dragId) wantFollow = true;
        if (wantFollow) next = std::min(next, 1.0 / cat::kFps);
    }
    if (wantFollow != followOn_) {
        followOn_ = wantFollow;
        if (followOn_) SetTimer(host_, kFollowTimer, 15, nullptr);
        else KillTimer(host_, kFollowTimer);
    }
    publish();
    scheduleNext(next);
}

void App::follow() {
    if (!scene_.dragging()) {
        KillTimer(host_, kFollowTimer);
        followOn_ = false;
        return;
    }
    HWND drag = scene_.dragWindow();
    RECT r;
    if (!drag || FAILED(DwmGetWindowAttribute(drag, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) return;
    uint64_t id = (uint64_t)(uintptr_t)drag;
    // Welt mitziehen (nur dieses Fenster), damit auch die Logik beim nächsten Takt passt
    for (cat::Platform& p : world_.plats) {
        if (p.id != id) continue;
        int dx = r.left - p.x0, w = p.x1 - p.x0;
        if (r.right - r.left >= w - 2 && r.right - r.left <= w + 2) {
            for (cat::Span& s : p.visible) {
                s.x0 += dx;
                s.x1 += dx;
            }
            p.x0 = r.left;
            p.x1 = r.left + w;
        }
        p.y = r.top;
    }
    for (auto& cp : cats_) {
        CatSlot& c = *cp;
        if (c.viewPlat != id || !c.lastValid || !c.win.shown()) continue;
        const cat::Platform* p = world_.find(id);
        if (!p) continue;
        int left = c.lastLeft + (p->x0 - c.viewPlatX0), top = c.lastTop + (p->y - c.viewPlatY);
        RECT cr = c.win.rect();
        if (cr.left != left || cr.top != top) SetWindowPos(c.win.hwnd(), nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// ------------------------------------------------------------------ Zeichnen

namespace {
bool isTaskbarWindow(HWND h) {
    wchar_t cls[32] = {0};
    GetClassNameW(h, cls, 31);
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}
}  // namespace

// Stapelreihenfolge pflegen, ohne sich mit anderen Programmen zu "streiten": Muss eine Katze kurz nach dem letzten
// Nachbessern schon wieder nachgebessert werden, drängt sich offenbar etwas ständig nach vorne – dann mit wachsender
// Pause (1, 2, 4, 8, 16 s) und auch dann, wenn gerade viele Fensterereignisse kommen (force).
void App::updateZ(SpriteWindow& w, uint64_t plat, bool airborne, bool force) {
    SpriteWindow::ZState& z = w.z;
    const double now = nowSec();
    auto noteFix = [&](bool ok) {
        const bool fight = now - z.lastFix < 2.0;
        z.lastFix = now;
        if (ok && !fight) {
            z.fails = 0;
            z.retryAt = 0;
            return;
        }
        z.fails = std::min(z.fails + 1, 5);
        z.retryAt = now + 0.5 * (1 << z.fails);
    };
    auto fresh = [&](int mode) {
        z.mode = mode;
        z.plat = plat;
        z.fails = 0;
        z.retryAt = 0;
        z.lastFix = now;
    };
    if (airborne || plat == 0 || Scene::isTaskbar(plat)) {
        if (z.mode != 1) {
            w.zTopmost();
            fresh(1);
            return;
        }
        // Ganz oben bleiben – nachgebessert wird nur, wenn die Taskleiste die Katze verdeckt (z. B. nach einem Klick
        // auf die Taskleiste), nicht wegen Tooltips, Vorschaubildern oder anderer "immer oben"-Fenster.
        if (!force || now < z.retryAt || !w.coveredBy(isTaskbarWindow)) return;
        w.zTopmost();
        noteFix(true);
        return;
    }
    if (Scene::isFloor(plat)) {
        if (z.mode != 2) {
            w.zAboveDesktop(scene_.desktopTop());
            fresh(2);
        } else if (force && now >= z.retryAt) {
            w.zAboveDesktop(scene_.desktopTop());
            noteFix(true);
        }
        return;
    }
    HWND ph = scene_.hwndOf(plat);
    if (!ph) return;
    if (!IsWindow(ph) || !IsWindowVisible(ph)) {
        // eben geschlossen oder versteckt (das meldet nicht jedes Programm): Welt neu bauen, die Katze fällt dann herunter
        scene_.invalidate();
        return;
    }
    auto own = [this](HWND h) { return isOwn(h); };
    const bool same = z.mode == 0 && z.plat == plat;
    HWND blocker = nullptr;
    if (same) {
        if (w.directlyAbove(ph, own, &blocker)) return;
        if (now < z.retryAt) return;   // nach Gerangel: abwarten, auch wenn gerade viele Ereignisse kommen
    }
    w.zAbove(ph);
    ++zPlaced_;
    HWND still = nullptr;
    const bool ok = w.directlyAbove(ph, own, &still);
    if (!ok) ++zFailed_;
    if (same) noteFix(ok);
    else fresh(0);   // neue Fläche (Katze ist umgezogen): kein Gerangel
    if (selftest_ > 0 && (same || !ok)) {   // Testlauf: unerwartetes Verrutschen protokollieren
        wchar_t bc[48] = {0}, sc[48] = {0};
        if (blocker) GetClassNameW(blocker, bc, 47);
        if (still) GetClassNameW(still, sc, 47);
        logLine(L"Z: %p über %p neu eingeordnet%ls – dazwischen war %p %ls%ls%ls", (void*)w.hwnd(), (void*)ph, force ? L" (erzwungen)" : L"", (void*)blocker, bc,
                ok ? L"" : L"; klappt nicht, jetzt dazwischen: ", sc);
    }
}

void App::present(CatSlot& c, bool force) {
    const cat::View& v = c.brain->view();
    cat::Pose p = v.transition ? cat::transitionPose(c.lk, v.from, v.to, v.frame, v.ov) : cat::actPose(c.lk, v.act, v.frame, v.param, v.ov);
    uint64_t key = cat::poseKey(p) ^ (v.facingLeft ? 0x9E3779B97F4A7C15ull : 0ull);
    auto it = c.cache.find(key);
    if (it == c.cache.end()) {
        if (c.cache.size() >= 160) trimCache(c);
        CatSlot::Cached entry;
        cat::PixelStyle st;
        cat::renderPixelCat(c.lk, p, v.facingLeft, st, entry.img);
        ++renders_;
        it = c.cache.emplace(key, std::move(entry)).first;
    }
    it->second.used = ++c.useClock;
    const cat::PixelImage& img = it->second.img;
    const int sc = world_.scaleAt((int)v.x, (int)v.y - 8);
    float ax = p.anchor.x * kPpc, ay = p.anchor.y * kPpc;
    float apx = img.originX + (v.facingLeft ? -ax : ax), apy = img.originY - ay;
    int left = (int)std::lround(v.x - apx * sc), top = (int)std::lround(v.y - apy * sc);
    bool changed = force || !c.lastValid || key != c.lastKey || sc != c.lastScale;
    if (changed) ++uploads_;
    else if (left != c.lastLeft || top != c.lastTop) ++moves_;
    c.win.show(img, sc, left, top, changed);
    ++presents_;
    c.lastKey = key;
    c.lastScale = sc;
    c.lastLeft = left;
    c.lastTop = top;
    c.lastValid = true;
    c.viewPlat = v.platform;
    if (const cat::Platform* pl = v.platform ? world_.find(v.platform) : nullptr) {
        c.viewPlatX0 = pl->x0;
        c.viewPlatY = pl->y;
    }
    updateZ(c.win, v.platform, v.topmost, false);
}

void App::presentItem(cat::Item& it, SpriteWindow& w, bool force) {
    bool isToy = &it == &toy_;
    bool& ok = isToy ? toyWinOk_ : bowlWinOk_;
    if (!ok) {
        ok = w.create(inst_, isToy ? kItemIdToy : kItemIdBowl, [this](int i, UINT m, POINT pt) { onMouse(i, m, pt); });
        if (!ok) return;
    }
    cat::PixelImage img;
    uint64_t key;
    if (isToy) {
        float spin = std::round(it.spin * 8.f) / 8.f;
        cat::renderYarn(spin, img);
        key = (uint64_t)(((int)std::lround(spin * 8.f) & 7) + 1);
    } else {
        cat::renderBowl(it.food, img);
        key = it.food > 0.55 ? 1 : (it.food > 0.05 ? 2 : 3);
    }
    uint64_t& lastKey = isToy ? toyKey_ : bowlKey_;
    const int sc = world_.scaleAt((int)it.x, (int)it.y - 4);
    int left = (int)std::lround(it.x - img.originX * sc), top = (int)std::lround(it.y - img.originY * sc);
    w.show(img, sc, left, top, force || key != lastKey);
    lastKey = key;
    bool held = pressId_ == (isToy ? kItemIdToy : kItemIdBowl) && dragging_;
    updateZ(w, it.platform, it.airborne || held, false);
}

// ------------------------------------------------------------------ Befehle

void App::spawnBowl(POINT at) {
    if (world_.plats.empty()) rebuild();
    const cat::Platform* p = platformBelow(at);
    if (!p) return;
    const int sc = world_.scaleAt(at.x, p->y - 4);
    bowl_ = cat::Item();
    bowl_.active = true;
    bowl_.kind = 0;
    bowl_.platform = p->id;
    bowl_.x = std::max<double>(p->x0 + 20 * sc, std::min<double>(p->x1 - 20 * sc, at.x));
    bowl_.y = p->y;
    bowl_.food = 1.0;
    bowl_.since = nowSec();
    bowlEmptySince_ = 0;
    bowlKey_ = ~0ull;
    bowlWin_.z.mode = -1;
}

void App::spawnToy(POINT at) {
    toy_ = cat::Item();
    toy_.active = true;
    toy_.kind = 1;
    toy_.airborne = true;
    toy_.x = at.x;
    toy_.y = at.y;
    toy_.vx = 0;
    toy_.vy = 0;
    toy_.since = toyTouched_ = nowSec();
    toyKey_ = ~0ull;
    toyWin_.z.mode = -1;
}

void App::setVisible(bool v) {
    if (set_.visible == v) return;
    set_.visible = v;
    saveSettings(set_);
    if (!v) hideAll();
    else
        for (auto& c : cats_) c->nextT = 0;
    publish();
    heartbeat();
}

void App::publish() {
    bool sleeping = !cats_.empty();
    for (const auto& c : cats_) sleeping = sleeping && c->brain->sleeping();
    if (!published_ || sleeping != publishedSleeping_ || set_.visible != publishedVisible_) {
        publishMenuState(set_.visible, sleeping);
        publishedSleeping_ = sleeping;
        publishedVisible_ = set_.visible;
        published_ = true;
    }
}

void App::command(const std::wstring& cmd) {
    logLine(L"Befehl: %ls", cmd.c_str());
    const double now = nowSec();
    POINT cur{0, 0};
    bool curValid = GetCursorPos(&cur) != FALSE;
    if (cmd == L"exit") {
        DestroyWindow(host_);
        return;
    }
    if (cmd == L"settings" || cmd == L"open") {
        openSettings();
        return;
    }
    if (cmd == L"hide") {
        setVisible(false);
        return;
    }
    if (cmd == L"show" || cmd == L"toggle") {
        setVisible(cmd == L"show" ? true : !set_.visible);
        return;
    }
    if (cmd == L"reload") {
        applySettings(loadSettings(), false);
        return;
    }
    // alle weiteren Befehle brauchen sichtbare Katzen
    if (!set_.visible) setVisible(true);
    if (pausedNow(nullptr)) return;
    rebuild();
    cat::Command c;
    if (cmd == L"feed") {
        spawnBowl(cur);
        c = cat::Command::Feed;
    } else if (cmd == L"play") {
        spawnToy(cur);
        c = cat::Command::Play;
    } else if (cmd == L"call") {
        c = cat::Command::Call;
    } else if (cmd == L"sleep") {
        c = cat::Command::Sleep;
    } else if (cmd == L"wake") {
        c = cat::Command::Wake;
    } else {
        logLine(L"Unbekannter Befehl: %ls", cmd.c_str());
        return;
    }
    for (size_t i = 0; i < cats_.size(); ++i) {
        cat::Senses s = senses((int)i, now, cur, curValid);
        cats_[i]->brain->command(c, s);
        cats_[i]->nextT = now;
    }
    heartbeat();
}

void App::openSettings() {
    Settings cur = set_;
    showSettingsWindow(inst_, cur, [this](const Settings& s, bool autostart) {
        setAutostart(autostart);
        applySettings(s, true);
    });
}

// ------------------------------------------------------------------ Maus

void App::onMouse(int id, UINT msg, POINT pt) {
    const double now = nowSec();
    SpriteWindow* w = nullptr;
    CatSlot* c = nullptr;
    if (id == kItemIdBowl) w = &bowlWin_;
    else if (id == kItemIdToy) w = &toyWin_;
    else
        for (auto& cp : cats_)
            if (cp->look == id) {
                c = cp.get();
                w = &cp->win;
            }
    if (!w) return;
    auto catIndex = [&]() {
        for (size_t i = 0; i < cats_.size(); ++i)
            if (cats_[i].get() == c) return (int)i;
        return -1;
    };
    switch (msg) {
        case WM_LBUTTONDOWN:
            pressId_ = c ? catIndex() : id;
            pressPt_ = pt;
            dragging_ = false;
            lastDragPt_ = pt;
            lastDragT_ = now;
            itemVx_ = itemVy_ = 0;
            SetCapture(w->hwnd());
            break;
        case WM_MOUSEMOVE: {
            if (pressId_ < 0 || GetCapture() != w->hwnd()) break;
            const int th = GetSystemMetrics(SM_CXDRAG) + 2;
            if (!dragging_ && (std::abs(pt.x - pressPt_.x) > th || std::abs(pt.y - pressPt_.y) > th)) {
                dragging_ = true;
                if (c) {
                    cat::Senses s = senses(pressId_, now, pt, true);
                    c->brain->grab(pt.x, pt.y, s);
                }
            }
            if (!dragging_) break;
            double dt = std::max(0.008, now - lastDragT_);
            itemVx_ = itemVx_ * 0.5 + (pt.x - lastDragPt_.x) / dt * 0.5;
            itemVy_ = itemVy_ * 0.5 + (pt.y - lastDragPt_.y) / dt * 0.5;
            lastDragPt_ = pt;
            lastDragT_ = now;
            if (c) {
                cat::Senses s = senses(pressId_, now, pt, true);
                c->brain->drag(pt.x, pt.y, s);
                present(*c, false);
            } else {
                cat::Item& it = id == kItemIdToy ? toy_ : bowl_;
                it.x = pt.x;
                it.y = pt.y + 6;
                it.airborne = true;
                it.platform = 0;
                it.vx = it.vy = 0;
                presentItem(it, *w, false);
            }
            break;
        }
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED: {
            if (pressId_ < 0) break;
            bool wasDrag = dragging_;
            int pid = pressId_;
            pressId_ = -1;
            dragging_ = false;
            if (msg == WM_LBUTTONUP && GetCapture() == w->hwnd()) ReleaseCapture();
            if (c) {
                cat::Senses s = senses(pid, now, pt, true);
                if (wasDrag) c->brain->release(s);
                else if (msg == WM_LBUTTONUP) c->brain->pet(s);
                c->nextT = now;
            } else if (wasDrag) {
                cat::Item& it = id == kItemIdToy ? toy_ : bowl_;
                it.airborne = true;
                it.platform = 0;
                if (now - lastDragT_ < 0.12) {   // geworfen
                    it.vx = std::max(-2500.0, std::min(2500.0, itemVx_));
                    it.vy = std::max(-2500.0, std::min(1500.0, itemVy_));
                }
                if (id == kItemIdToy) toyTouched_ = now;
            }
            heartbeat();
            break;
        }
        case WM_RBUTTONUP:
            if (c) catMenu(catIndex(), pt);
            else itemMenu(id, pt);
            break;
        default: break;
    }
}

void App::catMenu(int index, POINT pt) {
    if (index < 0 || index >= (int)cats_.size()) return;
    const CatSlot& c = *cats_[index];
    const int lookOfMenu = c.look;   // nach dem Menü nicht mehr auf c verlassen
    enum { kPet = 1, kFeed, kPlay, kSleep, kWake, kSettings, kHide, kExit };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, kPet, c.look == 0 ? L"Fatos streicheln" : L"Kosta streicheln");
    AppendMenuW(m, MF_STRING, kFeed, L"Füttern");
    AppendMenuW(m, MF_STRING, kPlay, L"Spielen");
    bool anyAwake = false;
    for (const auto& o : cats_) anyAwake = anyAwake || !o->brain->sleeping();
    AppendMenuW(m, MF_STRING, anyAwake ? kSleep : kWake, anyAwake ? L"Schlafen schicken" : L"Aufwecken");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kSettings, L"Einstellungen …");
    AppendMenuW(m, MF_STRING, kHide, L"Katzen ausblenden");
    AppendMenuW(m, MF_STRING, kExit, L"Beenden");
    SetForegroundWindow(host_);
    int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, host_, nullptr);
    PostMessageW(host_, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
        case kPet: {
            // während des Menüs kann sich die Katzenliste ändern (Einstellungen per Befehl): neu nachschlagen
            const int look = lookOfMenu;
            for (size_t i = 0; i < cats_.size(); ++i) {
                if (cats_[i]->look != look) continue;
                cat::Senses s = senses((int)i, nowSec(), pt, true);
                cats_[i]->brain->pet(s);
                cats_[i]->nextT = 0;
                heartbeat();
                break;
            }
            break;
        }
        case kFeed: command(L"feed"); break;
        case kPlay: command(L"play"); break;
        case kSleep: command(L"sleep"); break;
        case kWake: command(L"wake"); break;
        case kSettings: openSettings(); break;
        case kHide: setVisible(false); break;
        case kExit: DestroyWindow(host_); break;
        default: break;
    }
}

void App::itemMenu(int id, POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, id == kItemIdToy ? L"Wollknäuel wegräumen" : L"Napf wegräumen");
    if (id == kItemIdBowl) AppendMenuW(m, MF_STRING, 2, L"Nachfüllen");
    SetForegroundWindow(host_);
    int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, host_, nullptr);
    PostMessageW(host_, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1) {
        if (id == kItemIdToy) {
            toy_.active = false;
            toyWin_.hide();
        } else {
            bowl_.active = false;
            bowlWin_.hide();
        }
    } else if (cmd == 2) {
        bowl_.food = 1.0;
        bowlEmptySince_ = 0;
        const double now = nowSec();
        POINT cur{0, 0};
        GetCursorPos(&cur);
        for (size_t i = 0; i < cats_.size(); ++i) {
            cat::Senses s = senses((int)i, now, cur, true);
            cats_[i]->brain->command(cat::Command::Feed, s);
            cats_[i]->nextT = now;
        }
    }
    heartbeat();
}

// ------------------------------------------------------------------ Selbsttest (für Tests ohne Bildschirm-Prüfung)

void App::selftestReport() {
    double t = nowSec() - startT_, cpu = processCpuSeconds() - startCpu_;
    PROCESS_MEMORY_COUNTERS_EX pm;
    std::memset(&pm, 0, sizeof(pm));
    pm.cb = sizeof(pm);
    typedef BOOL(WINAPI * GetPmFn)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    GetPmFn getPm = (GetPmFn)(void*)GetProcAddress(LoadLibraryW(L"psapi.dll"), "GetProcessMemoryInfo");
    if (getPm) getPm(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pm, sizeof(pm));
    wchar_t line[512];
    _snwprintf(line, 511, L"Selbsttest: %.1f s, CPU %.3f s (%.2f %%), Schritte %ld, neu gezeichnet %ld, angezeigt %ld, Welt gebaut %ld, Flächen %zu, Speicher %.1f MB",
               t, cpu, t > 0 ? cpu / t * 100.0 : 0.0, ticks_, renders_, presents_, builds_, world_.plats.size(), pm.WorkingSetSize / 1048576.0);
    line[511] = 0;
    logLine(L"%ls", line);
    if (selftestWarm_) {
        double t2 = nowSec() - warmT_, cpu2 = processCpuSeconds() - warmCpu_;
        logLine(L"  ohne Anlauf: %.1f s, CPU %.3f s (%.2f %%), Schritte %ld; gesamt: neue Bilder an Fenster %ld, nur verschoben %ld", t2, cpu2,
                t2 > 0 ? cpu2 / t2 * 100.0 : 0.0, ticks_ - warmTicks_, uploads_, moves_);
    }
    logLine(L"  Stapelreihenfolge: %ld-mal über ein Fenster gelegt, %ld-mal ohne Erfolg", zPlaced_, zFailed_);
    for (const auto& c : cats_) {
        const cat::View& v = c->brain->view();
        logLine(L"  %ls: x=%.0f y=%.0f Fläche=%llx Handlung=%d sichtbar=%d", c->look == 0 ? L"Fatos" : L"Kosta", v.x, v.y, (unsigned long long)v.platform,
                (int)v.act, c->win.shown() ? 1 : 0);
    }
}

// ------------------------------------------------------------------ Einstieg

int runApp(const std::wstring& startCmd, bool autostart, int selftestSeconds) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS || err == ERROR_ACCESS_DENIED || !mutex) {
        // läuft schon (auch als Admin gestartete Instanz zählt): Befehl hinschicken
        if (mutex) CloseHandle(mutex);
        sendToRunning(startCmd.empty() ? (autostart ? L"show" : L"settings") : startCmd);
        return 0;
    }
    App a;
    g_app = &a;
    int rc = a.run(startCmd, autostart, selftestSeconds);
    g_app = nullptr;
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return rc;
}

}  // namespace app
