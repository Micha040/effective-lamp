// FragDesk – CS2 fernsteuern und Momente aufnehmen (siehe recorder.h)
// winsock2.h muss vor windows.h kommen (recorder.h bindet windows.h ein)
#include <winsock2.h>
#include <ws2tcpip.h>

#include "recorder.h"

#include <mmsystem.h>
#include <objbase.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include "../core/vdf.h"
#include "capture.h"
#include "common.h"

namespace fdw {

bool isProcessRunning(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof pe;
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exeName) == 0) found = true;
    CloseHandle(snap);
    return found;
}

std::wstring cs2SandboxDir() { return joinPath(dataDir(), L"cs2"); }

// Umgebung dieses Prozesses plus/ersetzt name=value (für CreateProcess mit CREATE_UNICODE_ENVIRONMENT)
std::vector<wchar_t> environmentWith(const std::wstring& name, const std::wstring& value) {
    std::vector<std::wstring> vars;
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; *p; p += wcslen(p) + 1) {
            std::wstring v = p;
            size_t eq = v.find(L'=', 1);   // Einträge wie "=C:=C:\" beginnen mit '='
            if (eq != std::wstring::npos && _wcsicmp(v.substr(0, eq).c_str(), name.c_str()) == 0) continue;
            vars.push_back(v);
        }
        FreeEnvironmentStringsW(env);
    }
    vars.push_back(name + L"=" + value);
    std::stable_sort(vars.begin(), vars.end(), [](const std::wstring& a, const std::wstring& b) {
        size_t ea = a.find(L'=', 1), eb = b.find(L'=', 1);
        return _wcsicmp(a.substr(0, ea).c_str(), b.substr(0, eb).c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (const auto& v : vars) {
        block.insert(block.end(), v.begin(), v.end());
        block.push_back(0);
    }
    block.push_back(0);
    return block;
}

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// ---- Konsole von CS2 (-netconport) und console.log (-condebug) ------------------------------------------------
class Console {
public:
    ~Console() { close(); }

    bool connect(int port, std::atomic<bool>& cancel, HANDLE process, int timeoutMs) {
        auto start = Clock::now();
        while (!cancel && secondsSince(start) * 1000 < timeoutMs) {
            if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return false;
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s != INVALID_SOCKET) {
                sockaddr_in a = {};
                a.sin_family = AF_INET;
                a.sin_port = htons(u_short(port));
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
                    sock_ = s;
                    run_ = true;
                    reader_ = std::thread([this] { readLoop(); });
                    return true;
                }
                closesocket(s);
            }
            Sleep(1000);
        }
        return false;
    }

    // console.log mitlesen (nur was ab jetzt dazukommt)
    void addLog(const std::wstring& path) { logs_.push_back({path, std::max<int64_t>(0, fileSize(path))}); }

    bool send(const std::string& cmd) {
        if (sock_ == INVALID_SOCKET) return false;
        std::string line = cmd + "\n";
        logLine(L"CS2 <- %ls", widen(cmd).c_str());
        return ::send(sock_, line.data(), int(line.size()), 0) == int(line.size());
    }

    // Neue Ausgabe seit dem letzten Aufruf (Konsole und console.log)
    void drain() {
        std::string out;
        {
            std::lock_guard<std::mutex> lock(mu_);
            out.swap(buf_);
        }
        for (auto& lg : logs_) {
            int64_t size = fileSize(lg.path);
            if (size < lg.pos) lg.pos = 0;   // Datei wurde neu angelegt
            if (size <= lg.pos) continue;
            HANDLE h = CreateFileW(lg.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                                   nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            LARGE_INTEGER p;
            p.QuadPart = lg.pos;
            SetFilePointerEx(h, p, nullptr, FILE_BEGIN);
            std::string chunk(size_t(std::min<int64_t>(size - lg.pos, 4 << 20)), '\0');
            DWORD got = 0;
            if (ReadFile(h, &chunk[0], DWORD(chunk.size()), &got, nullptr)) {
                chunk.resize(got);
                lg.pos += got;
                out += chunk;
            }
            CloseHandle(h);
        }
        seen_ += out;
        if (seen_.size() > (1 << 20)) seen_.erase(0, seen_.size() - (1 << 19));
    }

    // Wartet, bis einer der Texte in der Ausgabe auftaucht
    bool waitFor(const std::vector<std::string>& needles, int timeoutMs, std::atomic<bool>& cancel, HANDLE process) {
        auto start = Clock::now();
        while (!cancel && secondsSince(start) * 1000 < timeoutMs) {
            drain();
            for (const auto& n : needles)
                if (seen_.find(n) != std::string::npos) return true;
            if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return false;
            Sleep(250);
        }
        return false;
    }

    void close() {
        run_ = false;
        if (sock_ != INVALID_SOCKET) {
            shutdown(sock_, SD_BOTH);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (reader_.joinable()) reader_.join();
    }

private:
    void readLoop() {
        char b[4096];
        while (run_) {
            int n = recv(sock_, b, sizeof b, 0);
            if (n <= 0) break;
            std::lock_guard<std::mutex> lock(mu_);
            buf_.append(b, size_t(n));
            if (buf_.size() > (4 << 20)) buf_.erase(0, buf_.size() - (1 << 20));
        }
    }

    struct Log {
        std::wstring path;
        int64_t pos;
    };
    SOCKET sock_ = INVALID_SOCKET;
    std::thread reader_;
    std::atomic<bool> run_{false};
    std::mutex mu_;
    std::string buf_, seen_;
    std::vector<Log> logs_;
};

std::vector<HWND> processWindows(DWORD pid) {
    struct Ctx {
        DWORD pid;
        std::vector<HWND> list;
    } ctx{pid, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD p = 0;
            GetWindowThreadProcessId(h, &p);
            if (p == c->pid && IsWindowVisible(h) && !IsIconic(h)) c->list.push_back(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.list;
}

// größtes sichtbares Fenster des Prozesses, Innenbereich in Bildschirmkoordinaten
bool gameArea(DWORD pid, HWND& win, RECT& area) {
    long best = 0;
    for (HWND h : processWindows(pid)) {
        RECT rc;
        if (!GetClientRect(h, &rc)) continue;
        long a = (rc.right - rc.left) * (rc.bottom - rc.top);
        if (a > best && rc.right - rc.left >= 320 && rc.bottom - rc.top >= 200) {
            POINT tl = {0, 0};
            ClientToScreen(h, &tl);
            area = {tl.x, tl.y, tl.x + (rc.right - rc.left), tl.y + (rc.bottom - rc.top)};
            win = h;
            best = a;
        }
    }
    return best > 0;
}

bool runUnpack(const std::wstring& demo, const std::wstring& out, std::wstring& err) {
    std::wstring exe = joinPath(selfDir(), kAnalyzerExe);
    std::wstring cmd = L"\"" + exe + L"\" -demo \"" + demo + L"\" -unpack \"" + out + L"\" -temp \"" + tempDir() + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
        err = L"Entpacken der Demo startet nicht";
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0 || !fileExists(out)) {
        err = L"Demo konnte nicht entpackt werden";
        return false;
    }
    return true;
}

bool isPacked(const std::wstring& path) { return endsWithI(path, L".zst") || endsWithI(path, L".gz") || endsWithI(path, L".bz2"); }

// Zuletzt geänderte cs2_video.txt im Steam-Ordner (userdata\<Konto>\730\local\cfg)
std::wstring newestVideoConfig(const std::wstring& steamDir) {
    std::wstring best;
    FILETIME bestTime = {};
    if (steamDir.empty()) return best;
    std::wstring users = joinPath(steamDir, L"userdata");
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(joinPath(users, L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return best;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::wstring cfg = joinPath(joinPath(users, fd.cFileName), L"730\\local\\cfg\\cs2_video.txt");
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExW(cfg.c_str(), GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &bestTime) > 0) {
            bestTime = a.ftLastWriteTime;
            best = cfg;
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return best;
}

struct WindowPlan {
    bool borderless = false;   // randloses Vollbild-Fenster in Monitorgröße (Einstellung in cs2_video.txt)
    int w = 1280, h = 720;
};

// Fenstergröße für die Aufnahme: gewünschte Höhe im Fenster, wenn sie auf den Hauptmonitor passt; sonst ein
// randloses Fenster in Monitorgröße (braucht eine cs2_video.txt als Vorlage), zur Not ein kleineres Fenster
WindowPlan planWindow(int wantH, bool canBorderless) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    POINT origin = {0, 0};
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    int mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    auto fits = [&](int h) { return h * 16 / 9 + 40 <= mw && h + 90 <= mh; };
    WindowPlan p;
    if (fits(wantH)) {
        p.h = wantH;
    } else if (canBorderless && mw >= 640 && mh >= 360) {
        p.borderless = true;
        p.w = mw;
        p.h = mh;
        return p;
    } else {
        p.h = 540;
        for (int h : {1080, 720})
            if (h < wantH && fits(h)) {
                p.h = h;
                break;
            }
    }
    p.w = p.h * 16 / 9;
    return p;
}

// Eigener Einstellungsordner für Aufnahmen: Fenstermodus in dessen cs2_video.txt eintragen
void prepareSandboxVideo(const std::wstring& steamDir, const WindowPlan& plan) {
    std::wstring cfgDir = joinPath(cs2SandboxDir(), L"cfg");
    createDirs(cfgDir);
    std::wstring own = joinPath(cfgDir, L"cs2_video.txt");
    std::string text;
    if (!readFile(own, text, 1u << 20)) {
        std::wstring base = newestVideoConfig(steamDir);
        if (base.empty() || !readFile(base, text, 1u << 20)) return;   // CS2 legt selbst Standardwerte an
    }
    text = fd::setVdfValue(text, "setting.fullscreen", "0");
    text = fd::setVdfValue(text, "setting.nowindowborder", plan.borderless ? "1" : "0");
    text = fd::setVdfValue(text, "setting.coop_fullscreen", plan.borderless ? "1" : "0");
    text = fd::setVdfValue(text, "setting.defaultres", std::to_string(plan.w));
    text = fd::setVdfValue(text, "setting.defaultresheight", std::to_string(plan.h));
    writeFileAtomic(own, text);
}

// Alte entpackte Demos aufräumen (eine noch von CS2 geöffnete bleibt einfach liegen)
void cleanPlayCopies() {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(joinPath(tempDir(), L"play-*.dem").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do DeleteFileW(joinPath(tempDir(), fd.cFileName).c_str());
    while (FindNextFileW(f, &fd));
    FindClose(f);
}

int firstTick(const RecordJob& j) { return j.segments.empty() ? 0 : j.segments.front().first; }

}  // namespace

Recorder::Recorder(HWND notify) : hwnd_(notify) {}

Recorder::~Recorder() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::wstring Recorder::status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

void Recorder::setStatus(const std::wstring& s) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = s;
    }
    logLine(L"Aufnahme: %ls", s.c_str());
    auto* e = new RecordEvent();
    e->kind = RecordEvent::Status;
    e->text = s;
    post(e);
}

void Recorder::post(RecordEvent* e) {
    if (!hwnd_ || !PostMessageW(hwnd_, WM_APP_RECORD, 0, reinterpret_cast<LPARAM>(e))) delete e;
}

void Recorder::cancel() { cancel_ = true; }

bool Recorder::watch(const SteamInfo& steam, const RecordJob& job, std::wstring& err) { return start(steam, RecordSettings(), {job}, false, err); }

bool Recorder::record(const SteamInfo& steam, const RecordSettings& rs, std::vector<RecordJob> jobs, std::wstring& err) {
    return start(steam, rs, std::move(jobs), true, err);
}

bool Recorder::start(const SteamInfo& steam, const RecordSettings& rs, std::vector<RecordJob> jobs, bool recordVideo, std::wstring& err) {
    if (busy_) {
        err = L"Es läuft bereits eine Aufnahme";
        return false;
    }
    if (steam.cs2Exe().empty() || !fileExists(steam.cs2Exe())) {
        err = L"CS2 wurde nicht gefunden – Ordner in den Einstellungen wählen";
        return false;
    }
    if (isProcessRunning(L"cs2.exe")) {
        err = recordVideo ? L"CS2 läuft schon – schließe es, damit FragDesk aufnehmen kann" : L"CS2 läuft schon – schließe es, damit FragDesk die Demo öffnen kann";
        return false;
    }
    if (!isProcessRunning(L"steam.exe") && !isProcessRunning(L"steamchina.exe")) {
        err = L"Steam läuft nicht – bitte Steam starten und noch einmal versuchen";
        return false;
    }
    if (jobs.empty()) {
        err = L"Nichts zum Aufnehmen";
        return false;
    }
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    busy_ = true;
    thread_ = std::thread(&Recorder::run, this, steam, rs, std::move(jobs), recordVideo);
    return true;
}

void Recorder::run(SteamInfo steam, RecordSettings rs, std::vector<RecordJob> jobs, bool recordVideo) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    if (recordVideo) timeBeginPeriod(1);   // genaue Wartezeiten zwischen den Bildern (nur während der Aufnahme)
    const std::wstring cs2Exe = steam.cs2Exe();
    int done = 0, total = int(jobs.size());
    std::wstring failure;
    bool minimized = false;

    // Aufträge nach Demo gruppieren (CS2 startet pro Demo nur einmal) und nach Tick sortieren (Sprünge nach vorn sind schneller)
    std::vector<std::vector<RecordJob*>> groups;
    for (auto& j : jobs) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const std::vector<RecordJob*>& g) { return samePath(g.front()->demo, j.demo); });
        if (it == groups.end()) groups.push_back({&j});
        else it->push_back(&j);
    }
    for (auto& g : groups) std::stable_sort(g.begin(), g.end(), [](const RecordJob* a, const RecordJob* b) { return firstTick(*a) < firstTick(*b); });

    for (auto& group : groups) {
        if (cancel_ || !failure.empty()) break;
        const std::wstring demo = group.front()->demo;
        std::wstring playPath = demo, tempDemo;
        if (isPacked(demo)) {
            setStatus(L"Demo wird entpackt …");
            createDirs(tempDir());
            cleanPlayCopies();
            tempDemo = joinPath(tempDir(), L"play-" + std::to_wstring(GetTickCount64()) + L".dem");
            if (!runUnpack(demo, tempDemo, failure)) break;
            playPath = tempDemo;
        }

        // Startoptionen: Konsole über einen zufälligen Port, Demo direkt abspielen
        int port = 30000 + int(GetTickCount64() % 9000);
        std::wstring args = L"\"" + cs2Exe + L"\" -novid -nojoy -condebug -netconport " + std::to_wstring(port);
        std::vector<wchar_t> env;
        if (recordVideo) {
            bool haveVideoCfg = !newestVideoConfig(steam.steamDir).empty() || fileExists(joinPath(cs2SandboxDir(), L"cfg\\cs2_video.txt"));
            WindowPlan plan = planWindow(rs.height, haveVideoCfg);
            prepareSandboxVideo(steam.steamDir, plan);
            if (!plan.borderless) args += L" -sw -width " + std::to_wstring(plan.w) + L" -height " + std::to_wstring(plan.h);
            args += L" +demo_ui_mode 0";
            env = environmentWith(L"USRLOCALCSGO", cs2SandboxDir());   // eigene Einstellungen, nie deine echten
            logLine(L"Aufnahme-Fenster: %dx%d%ls", plan.w, plan.h, plan.borderless ? L" (randlos)" : L"");
        }
        args += L" +playdemo \"" + playPath + L"\"";

        setStatus(L"CS2 wird gestartet …");
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> cmd(args.begin(), args.end());
        cmd.push_back(0);
        std::wstring workDir = parentDir(cs2Exe);
        if (!CreateProcessW(cs2Exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT,
                            env.empty() ? nullptr : env.data(), workDir.c_str(), &si, &pi)) {
            failure = L"CS2 startet nicht (Fehler " + std::to_wstring(GetLastError()) + L")";
            break;
        }
        logLine(L"CS2 gestartet: %ls", args.c_str());
        Console con;
        con.addLog(joinPath(steam.csgoDir(), L"console.log"));
        if (recordVideo) con.addLog(joinPath(cs2SandboxDir(), L"console.log"));
        if (!con.connect(port, cancel_, pi.hProcess, 150000)) {
            if (!cancel_)
                failure = WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0 ? L"CS2 wurde beendet, bevor die Demo lief" : L"Keine Verbindung zur Konsole von CS2";
            if (recordVideo) TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            break;
        }
        setStatus(L"Demo wird geladen …");
        bool loaded = con.waitFor({"UI_STATE_INGAME", "Playing demo", "playing demo"}, 180000, cancel_, pi.hProcess);
        if (!loaded && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            failure = L"CS2 wurde beendet, bevor die Demo lief";
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            break;
        }
        Sleep(3000);
        con.send("demo_pause");

        if (!recordVideo) {
            // Nur ansehen: zum Anfang springen, Spieler wählen, abspielen – deine CS2-Einstellungen bleiben unberührt
            const RecordJob* job = group.front();
            double tr = job->tickrate > 0 ? job->tickrate : 64;
            con.send("demo_gototick " + std::to_string(std::max(96, firstTick(*job) - int(2 * tr))));
            Sleep(3500);
            con.send("spec_mode 1");
            con.send("spec_player " + std::to_string(job->slot));
            Sleep(300);
            con.send("demo_resume");
            done = 1;
            con.close();
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            continue;   // CS2 bleibt offen
        }

        // Aufnahme: HUD bis auf den Killfeed aus, kein Röntgenblick, Ton auch ohne Fokus (alles nur im eigenen Einstellungsordner)
        for (const std::string& c : {std::string("demo_ui_mode 0"), std::string("spec_show_xray 0"),
                                     std::string("cl_draw_only_deathnotices ") + (rs.onlyKillfeed ? "1" : "0"), std::string("snd_mute_losefocus 0"),
                                     std::string("cl_hud_telemetry_frametime_show 0"), std::string("cl_hud_telemetry_ping_show 0"),
                                     std::string("cl_hud_telemetry_net_misdelivery_show 0"),
                                     std::string("cl_hud_telemetry_serverrecvmargin_graph_show 0"), std::string("cl_trueview_show_status 0"),
                                     std::string("r_show_build_info 0"), std::string("tv_listen_voice_indices ") + (rs.voices ? "-1" : "0"),
                                     std::string("tv_listen_voice_indices_h ") + (rs.voices ? "-1" : "0"),
                                     std::string("fps_max ") + std::to_string(rs.fps * 2)})
            con.send(c);

        HWND gameWin = nullptr;
        RECT area = {};
        for (int i = 0; i < 40 && !gameArea(pi.dwProcessId, gameWin, area); ++i) Sleep(250);

        for (RecordJob* job : group) {
            if (cancel_ || !failure.empty()) break;
            ++done;
            double tr = job->tickrate > 0 ? job->tickrate : 64;
            setStatus(L"Moment " + std::to_wstring(done) + L" von " + std::to_wstring(total) + L": „" + job->title + L"“ – bitte den PC nicht benutzen");
            if (!minimized && hwnd_) {
                ShowWindow(hwnd_, SW_MINIMIZE);
                minimized = true;
            }
            if (gameWin) SetForegroundWindow(gameWin);
            Sleep(400);
            if (!gameArea(pi.dwProcessId, gameWin, area)) {
                failure = L"Das Fenster von CS2 ist nicht zu sehen";
                break;
            }
            ScreenCapture cap;
            std::wstring err;
            if (!cap.open(area, err)) {
                failure = err;
                break;
            }
            ProcessAudio audio;
            bool withAudio = rs.sound && audio.start(pi.dwProcessId, err);
            if (rs.sound && !withAudio) logLine(L"Ton: %ls", err.c_str());
            createDirs(parentDir(job->out));
            Mp4Writer mp4;
            double bpp = rs.quality == 1 ? 0.06 : rs.quality == 3 ? 0.16 : 0.10;
            int bitrate = int(std::min(80e6, bpp * cap.width() * cap.height() * rs.fps));
            if (!mp4.open(job->out, cap.width(), cap.height(), rs.fps, bitrate, withAudio, err)) {
                failure = err;
                break;
            }
            const double frameDur = 1.0 / rs.fps;
            int64_t base = 0;   // Startzeit des Abschnitts im Video (100 ns)
            int64_t frames = 0;
            std::vector<uint8_t> img;
            std::vector<int16_t> pcm;
            for (const auto& seg : job->segments) {
                if (cancel_ || !failure.empty()) break;
                // anspringen (1,5 s Vorlauf zum Einschwingen der Kamera), Spieler wählen, abspielen
                con.send("demo_pause");
                con.send("demo_gototick " + std::to_string(std::max(96, seg.first - int(1.5 * tr))));
                Sleep(3500);
                con.send("spec_mode 1");
                con.send("spec_player " + std::to_string(job->slot));
                Sleep(300);
                con.send("demo_resume");
                Sleep(1500);
                con.send("spec_player " + std::to_string(job->slot));
                const double length = std::max(0.5, (seg.second - seg.first) / tr);
                const int64_t audioMax = int64_t(length * 48000);   // Ton nie länger als das Bild dieses Abschnitts
                int64_t segFrames = 0, segAudio = 0;
                auto writePcm = [&](std::vector<int16_t>& p) {
                    int64_t n = std::min<int64_t>(int64_t(p.size() / 2), audioMax - segAudio);
                    if (n > 0) {
                        mp4.writeAudio(p.data(), size_t(n), base + segAudio * 10000000LL / 48000);
                        segAudio += n;
                    }
                    p.clear();
                };
                audio.take(pcm);   // Ton vom Springen verwerfen
                pcm.clear();
                auto segStart = Clock::now();
                double lastSpec = 0;
                while (!cancel_ && double(segFrames) * frameDur < length) {
                    double t = secondsSince(segStart);
                    double next = double(segFrames) * frameDur;
                    if (t < next) {   // bis zum nächsten Bild warten
                        Sleep(DWORD(std::max(1.0, (next - t) * 1000)));
                        continue;
                    }
                    if (!cap.grab(img, 0, err)) {
                        if (!err.empty()) {
                            failure = err;
                            break;
                        }
                        Sleep(2);   // noch kein Bild da
                        continue;
                    }
                    // fällig gewordene Bilder schreiben (bei Verzögerung das letzte Bild wiederholen: das Video bleibt in Echtzeit)
                    int64_t due = int64_t(t / frameDur) + 1;
                    while (segFrames < due && double(segFrames) * frameDur < length) {
                        mp4.writeFrame(img.data(), cap.width() * 4, base + int64_t(double(segFrames) * frameDur * 1e7));
                        ++segFrames;
                    }
                    if (withAudio) {
                        audio.take(pcm);
                        int64_t want = int64_t(t * 48000), have = int64_t(pcm.size() / 2);
                        if (segAudio + have < want - 4800)   // Spiel war still (keine Pakete): Stille einfügen, damit Bild und Ton zusammenbleiben
                            pcm.insert(pcm.begin(), size_t(want - 4800 - segAudio - have) * 2, int16_t(0));
                        writePcm(pcm);
                    }
                    if (t - lastSpec > 2.0) {   // Kamera beim Spieler halten
                        con.send("spec_player " + std::to_string(job->slot));
                        lastSpec = t;
                    }
                }
                con.send("demo_pause");
                if (withAudio) {   // Rest und Stille bis zum Ende des Abschnitts
                    audio.take(pcm);
                    writePcm(pcm);
                    int64_t videoAudio = std::min<int64_t>(audioMax, int64_t(double(segFrames) * frameDur * 48000));
                    if (segAudio < videoAudio) {
                        std::vector<int16_t> silence(size_t(videoAudio - segAudio) * 2, 0);
                        writePcm(silence);
                    }
                }
                base += int64_t(double(segFrames) * frameDur * 1e7);
                frames += segFrames;
            }
            audio.stop();
            setStatus(L"Video wird gespeichert …");
            bool ok = mp4.close();
            if (cancel_ || !failure.empty() || !ok || frames == 0) {
                DeleteFileW(job->out.c_str());
                if (failure.empty() && !cancel_) failure = frames == 0 ? L"Kein Bild aufgenommen" : L"Video konnte nicht gespeichert werden";
                break;
            }
            auto* e = new RecordEvent();
            e->kind = RecordEvent::ClipDone;
            e->job = *job;
            e->seconds = double(base) / 1e7;
            post(e);
        }

        setStatus(L"CS2 wird beendet …");
        con.send("quit");
        if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 0);
        con.close();
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (!tempDemo.empty()) DeleteFileW(tempDemo.c_str());
    }

    if (minimized && hwnd_) ShowWindow(hwnd_, SW_RESTORE);
    if (recordVideo) timeEndPeriod(1);
    auto* e = new RecordEvent();
    if (!failure.empty()) {
        e->kind = RecordEvent::Failed;
        e->text = failure;
    } else if (cancel_) {
        e->kind = RecordEvent::Finished;
        e->text = recordVideo ? L"Aufnahme abgebrochen" : L"Abgebrochen";
    } else {
        e->kind = RecordEvent::Finished;
        e->text = recordVideo ? (total == 1 ? L"Video fertig" : std::to_wstring(total) + L" Videos fertig") : L"CS2 zeigt den Moment";
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.clear();
    }
    busy_ = false;
    post(e);
    WSACleanup();
    CoUninitialize();
}

}  // namespace fdw
