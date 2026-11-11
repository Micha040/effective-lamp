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
#include <functional>

#include "../core/cs2con.h"
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

namespace {
std::atomic<fd::Cs2Mode> g_cs2Mode{fd::Cs2Mode::Performance};   // bis die Einstellungen geladen sind: Rücksicht aufs Spiel
std::atomic<bool> g_recording{false};
}

void setCs2Mode(fd::Cs2Mode mode) { g_cs2Mode = mode; }
fd::Cs2Mode cs2Mode() { return g_cs2Mode.load(); }
void setRecordingActive(bool on) { g_recording = on; }
bool recordingActive() { return g_recording.load(); }

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

    // tick: einmal pro Versuch (z. B. für die Statusanzeige); liefert er false, wird nicht weiter gewartet
    bool connect(int port, std::atomic<bool>& cancel, HANDLE process, int timeoutMs, const std::function<bool()>& tick) {
        auto start = Clock::now();
        int lastErr = 0;
        while (!cancel && secondsSince(start) * 1000 < timeoutMs) {
            if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return false;
            if (tick && !tick()) return false;
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
                    logLine(L"Konsole von CS2 verbunden (Port %d, nach %.0f s)", port, secondsSince(start));
                    return true;
                }
                int e = WSAGetLastError();
                if (e != lastErr) logLine(L"Konsole von CS2 noch nicht erreichbar (Port %d, Winsock-Fehler %d)", port, e);
                lastErr = e;
                closesocket(s);
            }
            drain();   // console.log weiter mitlesen: zeigt im Protokoll, was CS2 macht
            Sleep(1000);
        }
        return false;
    }

    bool saw(const char* text) const { return seen_.find(text) != std::string::npos; }

    // Stand der Ausgabe als Marke (etwa vor einem Sprung); zählt weiter, auch wenn vorne Altes wegfällt
    size_t mark() {
        drain();
        return dropped_ + seen_.size();
    }
    // Letzter Zustand der Oberfläche seit der Marke ("INGAME", "LOADINGSCREEN" …), leer: seitdem kein Wechsel
    std::string uiStateSince(size_t m) {
        drain();
        return fd::lastUiState(seen_, m > dropped_ ? m - dropped_ : 0);
    }

    // "FATAL ERROR: …" in der Ausgabe (CS2 stürzt ab); liefert die Zeile
    bool fatal(std::wstring& line) {
        drain();
        size_t at = seen_.find("FATAL ERROR", fatalScan_ > 16 ? fatalScan_ - 16 : 0);   // nur Neues durchsuchen (alle 0,25 s)
        fatalScan_ = seen_.size();
        if (at == std::string::npos) return false;
        size_t end = seen_.find_first_of("\r\n", at);
        line = widen(seen_.substr(at, std::min<size_t>(end == std::string::npos ? std::string::npos : end - at, 300)));
        return true;
    }

    // Letzte Zeilen der Ausgabe ins Protokoll (bei Fehlern)
    void logTail(size_t lines) {
        drain();
        std::vector<std::string> out;
        size_t end = seen_.size();
        while (end > 0 && out.size() < lines) {
            size_t nl = end > 1 ? seen_.rfind('\n', end - 2) : std::string::npos;
            size_t begin = nl == std::string::npos ? 0 : nl + 1;
            std::string line = seen_.substr(begin, end - begin);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (!line.empty()) out.push_back(line);
            end = begin;
        }
        if (out.empty()) {
            logLine(L"Von CS2 kam keine Ausgabe (weder Konsole noch console.log)");
            return;
        }
        logLine(L"Letzte Ausgabe von CS2:");
        const std::string* prev = nullptr;   // Konsole und console.log liefern oft dieselbe Zeile: nur einmal
        for (auto it = out.rbegin(); it != out.rend(); ++it) {
            if (prev && *prev == *it) continue;
            logLine(L"  CS2 | %ls", widen(it->substr(0, 400)).c_str());
            prev = &*it;
        }
    }

    // console.log mitlesen (nur was ab jetzt dazukommt)
    void addLog(const std::wstring& path) { logs_.push_back({path, std::max<int64_t>(0, fileSize(path)), readHead(path)}); }

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
            else if (size > lg.pos && lg.pos > 0 && !lg.head.empty()) {
                // neu geschrieben und schon wieder größer als vorher? Dann stimmt der Anfang nicht mehr
                std::string now = readHead(lg.path);
                bool rewritten = now.compare(0, lg.head.size(), lg.head) != 0;
                if (rewritten) lg.pos = 0;
                if (rewritten || now.size() >= kHead) lg.head.clear();   // danach nicht mehr prüfen
            }
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
        logStates();
        if (seen_.size() > (1 << 20)) {
            size_t cut = seen_.size() - (1 << 19);
            seen_.erase(0, cut);
            dropped_ += cut;
            fatalScan_ = fatalScan_ > cut ? fatalScan_ - cut : 0;
            stateScan_ = stateScan_ > cut ? stateScan_ - cut : 0;
        }
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
    // Zustandswechsel von CS2 (Hauptmenü, Ladebildschirm, im Spiel) mit Uhrzeit ins Protokoll: Zeigt später, wie lange CS2
    // nach einem Sprung geladen hat. Konsole und console.log liefern dieselbe Zeile – kurz hintereinander nur einmal.
    void logStates() {
        size_t limit = seen_.rfind('\n');
        if (limit == std::string::npos || limit < stateScan_) return;
        for (size_t at = seen_.find("ChangeGameUIState", stateScan_); at != std::string::npos && at < limit;
             at = seen_.find("ChangeGameUIState", at + 1)) {
            size_t end = seen_.find_first_of("\r\n", at);
            std::string line = seen_.substr(at, std::min<size_t>(end - at, 200));
            if (line == lastState_ && secondsSince(lastStateAt_) < 3) continue;
            lastState_ = line;
            lastStateAt_ = Clock::now();
            logLine(L"  CS2 | %ls", widen(line).c_str());
        }
        stateScan_ = limit + 1;
    }

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

    static constexpr size_t kHead = 256;
    // Anfang einer Datei (zum Erkennen, ob sie inzwischen neu geschrieben wurde)
    static std::string readHead(const std::wstring& path) {
        std::string head;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return head;
        head.resize(kHead);
        DWORD got = 0;
        if (!ReadFile(h, &head[0], DWORD(kHead), &got, nullptr)) got = 0;
        head.resize(got);
        CloseHandle(h);
        return head;
    }

    struct Log {
        std::wstring path;
        int64_t pos;
        std::string head;   // Anfang beim Start (leer: nicht mehr prüfen)
    };
    SOCKET sock_ = INVALID_SOCKET;
    std::thread reader_;
    std::atomic<bool> run_{false};
    std::mutex mu_;
    std::string buf_, seen_;
    size_t fatalScan_ = 0;   // bis hierhin ist seen_ schon nach "FATAL ERROR" durchsucht
    size_t stateScan_ = 0;   // … nach Zustandswechseln (logStates)
    size_t dropped_ = 0;     // vorne aus seen_ entfernt (für mark)
    std::string lastState_;
    Clock::time_point lastStateAt_{};
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

// Fehlermeldung von CS2 (etwa "FATAL ERROR: CopyNewEntity: …" bei Demos, die CS2 selbst nicht abspielen kann): ein
// Dialogfenster (#32770) des Prozesses. Den Text liefert WM_GETTEXT mit Zeitlimit – das Spiel steht an der Stelle.
bool errorDialog(DWORD pid, std::wstring& text) {
    for (HWND h : processWindows(pid)) {
        wchar_t cls[16] = {};
        if (!GetClassNameW(h, cls, 16) || wcscmp(cls, L"#32770") != 0) continue;
        std::wstring best;
        EnumChildWindows(
            h,
            [](HWND c, LPARAM lp) -> BOOL {
                wchar_t k[16] = {}, buf[512] = {};
                DWORD_PTR n = 0;
                if (GetClassNameW(c, k, 16) && _wcsicmp(k, L"Static") == 0 &&
                    SendMessageTimeoutW(c, WM_GETTEXT, 512, reinterpret_cast<LPARAM>(buf), SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &n) && n > 0) {
                    auto* b = reinterpret_cast<std::wstring*>(lp);
                    if (wcslen(buf) > b->size()) *b = buf;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&best));
        if (best.empty()) {
            wchar_t title[128] = {};
            GetWindowTextW(h, title, 128);
            best = title;
        }
        text = best;
        return true;
    }
    return false;
}

// "FATAL ERROR: CopyNewEntity: invalid class index …" -> "CopyNewEntity" (für die kurze Meldung)
std::wstring crashLabel(std::wstring text) {
    for (const wchar_t* p : {L"FATAL ERROR:", L"FATAL ERROR"})
        if (text.rfind(p, 0) == 0) text.erase(0, wcslen(p));
    while (!text.empty() && iswspace(text.front())) text.erase(0, 1);
    size_t colon = text.find(L':');
    if (colon != std::wstring::npos && colon > 0) text.resize(colon);
    if (text.size() > 40) text = text.substr(0, 39) + L"…";
    return text.empty() ? L"Fehlermeldung" : text;
}

// Innenbereich eines Fensters in Bildschirmkoordinaten
bool clientArea(HWND h, RECT& area) {
    RECT rc;
    if (!GetClientRect(h, &rc)) return false;
    POINT tl = {0, 0};
    if (!ClientToScreen(h, &tl)) return false;
    area = {tl.x, tl.y, tl.x + (rc.right - rc.left), tl.y + (rc.bottom - rc.top)};
    return true;
}

// größtes sichtbares Fenster des Prozesses, Innenbereich in Bildschirmkoordinaten
bool gameArea(DWORD pid, HWND& win, RECT& area) {
    long best = 0;
    for (HWND h : processWindows(pid)) {
        RECT r;
        if (!clientArea(h, r)) continue;
        long w = r.right - r.left, ht = r.bottom - r.top;
        if (w * ht > best && w >= 320 && ht >= 200) {
            area = r;
            win = h;
            best = w * ht;
        }
    }
    return best > 0;
}

// Mauszeiger aus dem Bild: Liegt er (samt Bild des Zeigers) über dem CS2-Fenster, an den nächsten freien Platz daneben
// schieben – nur die Position, kein Klick, keine Eingabe ans Spiel. Ein CS2 ohne Fokus blendet den Zeiger nicht aus.
void parkCursor(const RECT& area) {
    POINT p;
    if (!GetCursorPos(&p)) return;
    std::vector<fd::IRect> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT r, LPARAM lp) -> BOOL {
            reinterpret_cast<std::vector<fd::IRect>*>(lp)->push_back({int(r->left), int(r->top), int(r->right), int(r->bottom)});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    int size = std::max(64, 2 * std::max(GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR)));
    int x = 0, y = 0;
    if (!fd::cursorParkPoint({int(area.left), int(area.top), int(area.right), int(area.bottom)}, int(p.x), int(p.y), size, monitors, x, y)) return;
    SetCursorPos(x, y);
    logLine(L"Mauszeiger aus dem CS2-Fenster geschoben (%d,%d → %d,%d)", int(p.x), int(p.y), x, y);
}

// CS2 für die Aufnahme nach vorn holen und (topmost) obenauf halten wie „Immer im Vordergrund“: Klickst du währenddessen
// woanders hin, verdeckt kein anderes Fenster das Bild. Ruft FragDesk auf, solange es selbst noch vorne ist – danach
// lässt Windows ein Programm kein anderes Fenster mehr aktivieren.
void bringToFront(HWND win, bool topmost) {
    if (topmost) SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(win);
}

// Gepackte Demo für CS2 entpacken (fdanalyze -unpack); bricht bei cancel ab
bool runUnpack(const std::wstring& demo, const std::wstring& out, std::wstring& err, std::atomic<bool>& cancel) {
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
    CloseHandle(pi.hThread);
    while (WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT)
        if (cancel) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            DeleteFileW(out.c_str());
            DeleteFileW((out + L".part").c_str());
            return false;
        }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0 || !fileExists(out)) {
        err = L"Demo konnte nicht entpackt werden";
        return false;
    }
    return true;
}

// CS2 liest Startoptionen und Umgebungsvariablen nicht sicher als Unicode: Pfade mit Sonderzeichen (z. B. „ö“ im
// Benutzernamen) als kurzen 8.3-Namen übergeben, falls Windows einen hat
bool isAscii(const std::wstring& s) { return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c < 128; }); }

std::wstring asciiPath(const std::wstring& path) {
    if (isAscii(path)) return path;
    DWORD n = GetShortPathNameW(path.c_str(), nullptr, 0);
    if (n) {
        std::wstring s(n, L'\0');
        n = GetShortPathNameW(path.c_str(), &s[0], n);
        s.resize(n);
        if (n && isAscii(s)) return s;
    }
    logLine(L"Pfad mit Sonderzeichen ohne kurzen Namen: %ls", path.c_str());
    return path;
}

// Sicherheitsnetz für deine echten CS2-Einstellungen: Für Aufnahmen startet CS2 mit eigenem Einstellungsordner
// (USRLOCALCSGO). Sollte CS2 das einmal nicht beachten, stellt FragDesk danach jede geänderte Datei wieder her.
class ConfigGuard {
public:
    void snapshot(const std::wstring& steamDir) {
        files_.clear();
        if (steamDir.empty()) return;
        std::wstring users = joinPath(steamDir, L"userdata");
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW(joinPath(users, L"*").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::wstring cfg = joinPath(joinPath(users, fd.cFileName), L"730\\local\\cfg");
            for (const wchar_t* pattern : {L"cs2_*.vcfg", L"cs2_video.txt"}) {
                WIN32_FIND_DATAW g;
                HANDLE h = FindFirstFileW(joinPath(cfg, pattern).c_str(), &g);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    File file;
                    file.path = joinPath(cfg, g.cFileName);
                    if (!(g.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && readFile(file.path, file.data, 4u << 20)) files_.push_back(file);
                } while (FindNextFileW(h, &g));
                FindClose(h);
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    int restore() const {
        int n = 0;
        for (const auto& f : files_) {
            std::string now;
            if (readFile(f.path, now, 4u << 20) && now != f.data && writeFileAtomic(f.path, f.data)) {
                logLine(L"CS2-Einstellung wiederhergestellt: %ls", f.path.c_str());
                ++n;
            }
        }
        return n;
    }

private:
    struct File {
        std::wstring path;
        std::string data;
    };
    std::vector<File> files_;
};

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

void Recorder::releaseTopmost() {
    keepTopmost_ = false;
    // asynchron: hängt CS2 gerade, wartet die Oberfläche von FragDesk nicht darauf
    if (HWND w = topmost_.exchange(nullptr)) SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

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
    if (workshopTools(steam) == Tools::Missing) {
        err = L"Erst die CS2 Workshop Tools installieren (Steam › Counter-Strike 2 › Eigenschaften › DLC)";
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
    keepTopmost_ = true;
    topmost_ = nullptr;
    video_ = recordVideo;
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
    int done = 0, total = int(jobs.size()), clips = 0;
    std::wstring failure;
    std::vector<std::wstring> crashes;   // Demos, bei denen CS2 abgestürzt ist (dann geht es mit der nächsten Demo weiter)
    bool minimized = false;
    ConfigGuard guard;
    if (recordVideo) guard.snapshot(steam.steamDir);

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
        // entpackte Kopie: bei Aufnahmen danach löschen, beim Ansehen braucht CS2 sie noch (wird beim nächsten Mal aufgeräumt)
        struct TempDemo {
            std::wstring path;
            bool keep = false;
            ~TempDemo() {
                if (!keep && !path.empty()) DeleteFileW(path.c_str());
            }
        } temp;
        temp.keep = !recordVideo;
        std::wstring playPath = demo;
        if (isPacked(demo)) {
            setStatus(L"Demo wird entpackt …");
            createDirs(tempDir());
            cleanPlayCopies();
            temp.path = joinPath(tempDir(), L"play-" + std::to_wstring(GetTickCount64()) + L".dem");
            if (!runUnpack(demo, temp.path, failure, cancel_)) break;
            playPath = temp.path;
        }

        // Startoptionen: Konsole über einen zufälligen Port, Demo direkt abspielen. -tools: Unter Windows öffnet CS2 die Konsole
        // für -netconport nur im Workshop-Tools-Modus; -noassetbrowser lässt dabei den Asset Browser weg (sonst ein zweites Fenster)
        int port = 30000 + int(GetTickCount64() % 9000);
        std::wstring args = L"\"" + cs2Exe + L"\" -tools -noassetbrowser -novid -nojoy -condebug -netconport " + std::to_wstring(port);
        std::vector<wchar_t> env;
        if (recordVideo) {
            bool haveVideoCfg = !newestVideoConfig(steam.steamDir).empty() || fileExists(joinPath(cs2SandboxDir(), L"cfg\\cs2_video.txt"));
            WindowPlan plan = planWindow(rs.height, haveVideoCfg);
            prepareSandboxVideo(steam.steamDir, plan);
            if (!plan.borderless) args += L" -sw -width " + std::to_wstring(plan.w) + L" -height " + std::to_wstring(plan.h);
            // Ohne Fokus legt sich CS2 sonst bei jedem Bild schlafen: Laden und Springen ziehen sich dann ewig, das Video ruckelt
            args += L" +demo_ui_mode 0 +engine_no_focus_sleep 0";
            createDirs(cs2SandboxDir());
            env = environmentWith(L"USRLOCALCSGO", asciiPath(cs2SandboxDir()));   // eigene Einstellungen, nie deine echten
            logLine(L"Aufnahme-Fenster: %dx%d%ls", plan.w, plan.h, plan.borderless ? L" (randlos)" : L"");
        }
        args += L" +playdemo \"" + asciiPath(playPath) + L"\"";

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
        // Gerade eben hast du in FragDesk geklickt: CS2 darf sein Fenster gleich selbst nach vorn holen
        AllowSetForegroundWindow(pi.dwProcessId);
        // Kein Vollzugriff auf das Spiel behalten: nur warten und (bei Aufnahmen) zur Not beenden dürfen
        CloseHandle(pi.hThread);
        HANDLE proc = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &proc, SYNCHRONIZE | (recordVideo ? PROCESS_TERMINATE : 0), FALSE,
                             0))
            proc = nullptr;
        CloseHandle(pi.hProcess);
        const DWORD pid = pi.dwProcessId;
        if (!proc) {
            failure = L"CS2 wurde gestartet, lässt sich aber nicht verfolgen";
            break;
        }

        Console con;
        con.addLog(joinPath(steam.csgoDir(), L"console.log"));
        if (recordVideo) con.addLog(joinPath(cs2SandboxDir(), L"console.log"));
        // Absturz von CS2 erkennen: Fehlerfenster oder "FATAL ERROR" in der Ausgabe
        std::wstring crash;
        auto crashed = [&]() -> bool {
            if (!crash.empty()) return true;
            std::wstring text;
            if (!errorDialog(pid, text) && !con.fatal(text)) return false;
            crash = text.empty() ? L"Fehlermeldung ohne Text" : text;
            logLine(L"CS2 meldet einen Fehler: %ls", crash.c_str());
            con.logTail(25);
            return true;
        };
        // warten und dabei auf Abstürze achten; false = CS2 abgestürzt oder abgebrochen
        auto pause = [&](int ms) -> bool {
            auto t0 = Clock::now();
            for (;;) {
                if (cancel_ || crashed()) return false;
                double left = ms - secondsSince(t0) * 1000;
                if (left <= 0) return true;
                Sleep(DWORD(std::min(250.0, left)));
            }
        };
        // Ende: Aufnahme-CS2 beenden (bei Abbruch nicht lange warten, nach einem Absturz gar nicht), beim Ansehen bleibt CS2 offen
        auto finish = [&](bool quit) {
            if (quit) {
                bool sent = crash.empty() && con.send("quit");
                if (WaitForSingleObject(proc, sent ? (cancel_ ? 5000 : 20000) : 0) != WAIT_OBJECT_0) {
                    TerminateProcess(proc, 0);
                    WaitForSingleObject(proc, 5000);
                }
            }
            con.close();
            CloseHandle(proc);
        };

        // Status: erst "wird gestartet", sobald das Fenster da ist "wartet auf die Konsole"
        bool windowShown = false;
        // Die Konsole öffnet CS2 ganz am Anfang. Steht es schon eine halbe Minute im Hauptmenü (laut console.log) und
        // sie fehlt immer noch, kommt sie nicht mehr – dann nicht die vollen 2,5 Minuten warten
        Clock::time_point menuAt{};
        auto connectTick = [&]() -> bool {
            if (!windowShown && !processWindows(pid).empty()) {
                windowShown = true;
                setStatus(L"CS2 ist offen – warte auf die Konsole …");
            }
            if (con.saw("UI_STATE_MAINMENU")) {
                if (menuAt == Clock::time_point{}) menuAt = Clock::now();
                else if (secondsSince(menuAt) > 30) {
                    logLine(L"CS2 ist seit 30 s im Hauptmenü, die Konsole fehlt weiterhin");
                    return false;
                }
            }
            return true;
        };
        if (!con.connect(port, cancel_, proc, 150000, connectTick)) {
            if (!cancel_) {
                bool exited = WaitForSingleObject(proc, 0) == WAIT_OBJECT_0;
                failure = exited ? L"CS2 wurde beendet, bevor die Demo lief"
                                 : L"CS2 öffnet keine Konsole – sind die CS2 Workshop Tools installiert? Details: fragdesk.log im Datenordner";
                con.logTail(40);
            }
            finish(recordVideo);
            break;
        }
        setStatus(L"Demo wird geladen …");
        // "UI_STATE_INGAME" erscheint erst, wenn die Map geladen ist und die Demo läuft. Bleibt CS2 im Hauptmenü stehen
        // (+playdemo beim Start nicht ausgeführt), die Demo einmal über die Konsole starten.
        bool loaded = false, retried = false;
        auto loadStart = Clock::now();
        Clock::time_point idleMenu{};   // seit wann CS2 im Hauptmenü steht, ohne dass je geladen wurde
        while (!cancel_ && secondsSince(loadStart) < 180) {
            if (con.waitFor({"UI_STATE_INGAME"}, 1000, cancel_, proc)) {
                loaded = true;
                break;
            }
            if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0 || crashed()) break;
            if (retried || !con.saw("UI_STATE_MAINMENU") || con.saw("UI_STATE_LOADINGSCREEN")) continue;
            if (idleMenu == Clock::time_point{}) idleMenu = Clock::now();
            else if (secondsSince(idleMenu) > 20) {
                retried = true;
                logLine(L"CS2 steht seit 20 s im Hauptmenü – Demo wird über die Konsole gestartet");
                con.send("playdemo \"" + narrow(asciiPath(playPath)) + "\"");
            }
        }
        // Manche Demos bringen CS2 direkt nach dem Laden zum Absturz (Fehler in CS2): dann mit der nächsten Demo weitermachen
        if (loaded) pause(3000);
        bool exited = WaitForSingleObject(proc, 0) == WAIT_OBJECT_0;
        if (cancel_) {
            finish(recordVideo);
            break;
        }
        if (crashed()) {
            crashes.push_back(crash);
            done += int(group.size());
            finish(recordVideo);
            continue;
        }
        if (!loaded) logLine(L"Kein UI_STATE_INGAME von CS2 (%ls)", exited ? L"CS2 beendet" : L"Zeit abgelaufen");
        if (!loaded && (exited || recordVideo)) {   // beim Ansehen trotzdem weitermachen: CS2 ist ja zu sehen
            failure = exited ? L"CS2 wurde beendet, bevor die Demo lief" : L"CS2 hat nach 3 Minuten nicht gemeldet, dass die Demo läuft";
            con.logTail(40);
            finish(recordVideo);
            break;
        }
        con.send("demo_pause");

        if (!recordVideo) {
            // Nur ansehen: zum Anfang springen, Spieler wählen, abspielen – deine CS2-Einstellungen bleiben unberührt
            const RecordJob* job = group.front();
            double tr = job->tickrate > 0 ? job->tickrate : 64;
            con.send("demo_gototick " + std::to_string(std::max(96, firstTick(*job) - int(2 * tr))));
            if (pause(3500)) {
                con.send("spec_mode 1");
                con.send("spec_player " + std::to_string(job->slot));
                pause(300);
                con.send("demo_resume");
            }
            if (!crash.empty()) crashes.push_back(crash);   // CS2 zeigt die Meldung selbst und schließt sich nach "OK"
            done = 1;
            finish(false);
            continue;
        }

        // Aufnahme: HUD bis auf den Killfeed aus, kein Röntgenblick, Ton und volles Tempo auch ohne Fokus (alles nur im eigenen
        // Einstellungsordner)
        for (const std::string& c : {std::string("demo_ui_mode 0"), std::string("spec_show_xray 0"),
                                     std::string("cl_draw_only_deathnotices ") + (rs.onlyKillfeed ? "1" : "0"), std::string("snd_mute_losefocus 0"),
                                     std::string("engine_no_focus_sleep 0"),
                                     std::string("cl_hud_telemetry_frametime_show 0"), std::string("cl_hud_telemetry_ping_show 0"),
                                     std::string("cl_hud_telemetry_net_misdelivery_show 0"),
                                     std::string("cl_hud_telemetry_serverrecvmargin_graph_show 0"), std::string("cl_trueview_show_status 0"),
                                     std::string("r_show_build_info 0"), std::string("tv_listen_voice_indices ") + (rs.voices ? "-1" : "0"),
                                     std::string("tv_listen_voice_indices_h ") + (rs.voices ? "-1" : "0"),
                                     std::string("fps_max ") + std::to_string(rs.fps * 2)})
            con.send(c);

        HWND gameWin = nullptr;
        RECT area = {};
        for (int i = 0; i < 40 && !gameArea(pid, gameWin, area); ++i) Sleep(250);

        // Nach einem Sprung (demo_gototick) zeigt CS2 oft den Ladebildschirm – manchmal sofort, manchmal erst beim
        // Weiterspielen. Erst aufnehmen, wenn CS2 wieder „im Spiel“ meldet; nach 90 s Abbruch statt eines Clips vom
        // Ladebildschirm. Meldet CS2 keinen Wechsel, bleibt es bei den festen Wartezeiten.
        auto waitWhileLoading = [&](size_t since, const std::wstring& what) -> bool {
            if (con.uiStateSince(since) != "LOADINGSCREEN") return true;
            setStatus(what + L" (CS2 lädt …)");
            auto t0 = Clock::now();
            while (con.uiStateSince(since) == "LOADINGSCREEN") {
                if (secondsSince(t0) > 90) {
                    failure = L"CS2 hängt nach dem Springen im Ladebildschirm fest – Details in fragdesk.log";
                    con.logTail(30);
                    return false;
                }
                if (!pause(250)) return false;
            }
            logLine(L"Ladebildschirm nach %.1f s vorbei", secondsSince(t0));
            setStatus(what);
            return pause(1000);   // die ersten Bilder nach dem Laden abwarten
        };
        // Zum Tick springen (angehalten), Spieler wählen, abspielen. false: abgebrochen, abgestürzt oder hängt (failure)
        auto jumpTo = [&](int tick, int slot, const std::wstring& what) -> bool {
            for (int attempt = 1;; ++attempt) {
                con.send("demo_pause");
                size_t jumped = con.mark();
                con.send("demo_gototick " + std::to_string(std::max(96, tick)));
                if (!pause(3500) || !waitWhileLoading(jumped, what)) return false;
                con.send("spec_mode 1");
                con.send("spec_player " + std::to_string(slot));
                if (!pause(300)) return false;
                size_t resumed = con.mark();
                con.send("demo_resume");
                if (!pause(1500)) return false;
                if (con.uiStateSince(resumed) != "LOADINGSCREEN") return true;
                // CS2 lädt erst beim Weiterspielen, der Vorlauf ist damit weg: abwarten und noch einmal anspringen (beim
                // zweiten Mal lieber etwas später anfangen als gar nicht)
                logLine(L"CS2 lädt erst beim Abspielen (Versuch %d)", attempt);
                if (!waitWhileLoading(resumed, what)) return false;
                if (attempt >= 2) return true;
            }
        };

        const int doneBefore = done;
        for (RecordJob* job : group) {
            if (cancel_ || !failure.empty() || !crash.empty()) break;
            ++done;
            double tr = job->tickrate > 0 ? job->tickrate : 64;
            const std::wstring momentText =
                L"Moment " + std::to_wstring(done) + L" von " + std::to_wstring(total) + L": „" + job->title + L"“ – bitte den PC nicht benutzen";
            setStatus(momentText);
            if (gameWin) {   // vor dem Minimieren: solange FragDesk vorne ist, lässt Windows das zu
                bool top = keepTopmost_;
                bringToFront(gameWin, top);
                if (top) topmost_ = gameWin;
            }
            if (!minimized && hwnd_) {
                ShowWindowAsync(hwnd_, SW_MINIMIZE);
                minimized = true;
            }
            Sleep(400);
            if (!gameArea(pid, gameWin, area)) {
                failure = L"Das Fenster von CS2 ist nicht zu sehen";
                break;
            }
            logLine(L"CS2-Fenster %ls, %ls", GetForegroundWindow() == gameWin ? L"im Vordergrund" : L"ohne Fokus (läuft trotzdem in vollem Tempo)",
                    (GetWindowLongPtrW(gameWin, GWL_EXSTYLE) & WS_EX_TOPMOST) ? L"obenauf" : L"nicht obenauf");
            ScreenCapture cap;
            std::wstring capErr, audioErr, encErr;
            if (!cap.open(area, capErr)) {
                failure = capErr;
                break;
            }
            ProcessAudio audio;
            bool withAudio = rs.sound && audio.start(pid, audioErr);
            if (rs.sound && !withAudio) logLine(L"Ton: %ls", audioErr.c_str());
            createDirs(parentDir(job->out));
            Mp4Writer mp4;
            double bpp = rs.quality == 1 ? 0.06 : rs.quality == 3 ? 0.16 : 0.10;
            int bitrate = int(std::min(80e6, bpp * cap.width() * cap.height() * rs.fps));
            if (!mp4.open(job->out, cap.width(), cap.height(), rs.fps, bitrate, withAudio, encErr)) {
                failure = encErr;
                break;
            }
            withAudio = withAudio && mp4.hasAudio();
            const double frameDur = 1.0 / rs.fps;
            int64_t base = 0;   // Startzeit des Abschnitts im Video (100 ns)
            int64_t frames = 0;
            std::vector<uint8_t> img;
            std::vector<int16_t> pcm;
            for (const auto& seg : job->segments) {
                if (cancel_ || !failure.empty() || !crash.empty()) break;
                // anspringen (1,5 s Vorlauf zum Einschwingen der Kamera), Spieler wählen, abspielen
                if (!jumpTo(seg.first - int(1.5 * tr), job->slot, momentText)) break;
                con.send("spec_player " + std::to_string(job->slot));
                parkCursor(area);
                logLine(L"Abschnitt Tick %d–%d wird aufgenommen (CS2 %ls)", seg.first, seg.second,
                        GetForegroundWindow() == gameWin ? L"im Vordergrund" : L"ohne Fokus");
                const double length = std::max(0.5, (seg.second - seg.first) / tr);
                const int64_t segVideoFrames = int64_t(std::ceil(length / frameDur - 1e-9));
                const int64_t audioMax = int64_t(double(segVideoFrames) * frameDur * 48000);   // Ton genau so lang wie das Bild
                int64_t segFrames = 0, segAudio = 0;
                auto writePcm = [&](std::vector<int16_t>& p) {
                    int64_t n = std::min<int64_t>(int64_t(p.size() / 2), audioMax - segAudio);
                    bool ok = true;
                    if (n > 0) {
                        ok = mp4.writeAudio(p.data(), size_t(n), base + segAudio * 10000000LL / 48000);
                        segAudio += n;
                    }
                    p.clear();
                    return ok;
                };
                audio.take(pcm);   // Ton vom Springen verwerfen
                pcm.clear();
                auto segStart = Clock::now();
                double lastSpec = 0, lastCheck = 0;
                while (!cancel_ && failure.empty() && segFrames < segVideoFrames) {
                    double t = secondsSince(segStart);
                    // läuft CS2 noch, und ist das Fenster unverändert da? Sonst würde der Bildschirm dahinter aufgenommen
                    if (t - lastCheck >= 0.25) {
                        lastCheck = t;
                        RECT now;
                        if (crashed()) break;   // Fehlermeldung von CS2 statt Spielbild
                        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) failure = L"CS2 wurde während der Aufnahme beendet";
                        else if (!IsWindow(gameWin) || IsIconic(gameWin) || !clientArea(gameWin, now) || !EqualRect(&now, &area))
                            failure = L"Das CS2-Fenster wurde minimiert, verschoben oder geschlossen – Aufnahme abgebrochen";
                        if (!failure.empty()) break;
                    }
                    double next = double(segFrames) * frameDur;
                    if (t < next) {   // bis zum nächsten Bild warten
                        Sleep(DWORD(std::max(1.0, (next - t) * 1000)));
                        continue;
                    }
                    if (!cap.grab(img, 0, capErr)) {
                        if (!capErr.empty()) {
                            failure = capErr;
                            break;
                        }
                        Sleep(2);   // noch kein Bild da
                        continue;
                    }
                    if (cap.width() != mp4.width() || cap.height() != mp4.height()) {
                        failure = L"Die Bildgröße hat sich während der Aufnahme geändert";
                        break;
                    }
                    // fällig gewordene Bilder schreiben (bei Verzögerung das letzte Bild wiederholen: das Video bleibt in Echtzeit)
                    int64_t due = int64_t(t / frameDur) + 1;
                    while (segFrames < due && segFrames < segVideoFrames) {
                        if (!mp4.writeFrame(img.data(), cap.width() * 4, base + int64_t(double(segFrames) * frameDur * 1e7))) {
                            failure = L"Der Video-Encoder meldet einen Fehler";
                            break;
                        }
                        ++segFrames;
                    }
                    if (!failure.empty()) break;
                    if (withAudio) {
                        audio.take(pcm);
                        int64_t want = int64_t(t * 48000), have = int64_t(pcm.size() / 2);
                        // Spiel war still (keine Pakete): mit Stille auffüllen, sodass das neue Paket bei "jetzt" endet
                        if (segAudio + have < want - 4800) pcm.insert(pcm.begin(), size_t(want - segAudio - have) * 2, int16_t(0));
                        if (!writePcm(pcm)) failure = L"Der Ton-Encoder meldet einen Fehler";
                    }
                    if (t - lastSpec > 2.0) {   // Kamera beim Spieler halten
                        con.send("spec_player " + std::to_string(job->slot));
                        lastSpec = t;
                    }
                }
                if (!crash.empty()) break;
                con.send("demo_pause");
                if (withAudio && failure.empty()) {   // Rest und Stille bis zum Ende des Abschnitts
                    audio.take(pcm);
                    writePcm(pcm);
                    int64_t videoAudio = int64_t(double(segFrames) * frameDur * 48000);
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
            if (cancel_ || !failure.empty() || !crash.empty() || !ok || frames == 0) {
                DeleteFileW(job->out.c_str());
                if (failure.empty() && crash.empty() && !cancel_)
                    failure = frames == 0 ? L"Kein Bild aufgenommen" : L"Video konnte nicht gespeichert werden";
                break;
            }
            ++clips;
            auto* e = new RecordEvent();
            e->kind = RecordEvent::ClipDone;
            e->job = *job;
            e->seconds = double(base) / 1e7;
            post(e);
        }

        if (!crash.empty()) {   // übrige Momente dieser Demo überspringen, weiter mit der nächsten Demo
            crashes.push_back(crash);
            done = doneBefore + int(group.size());
        }
        setStatus(L"CS2 wird beendet …");
        finish(true);
    }

    if (recordVideo && guard.restore() > 0)   // sollte nie nötig sein – siehe ConfigGuard
        logLine(L"Hinweis: CS2 hat den eigenen Einstellungsordner nicht benutzt; deine Einstellungen wurden wiederhergestellt");
    if (minimized && hwnd_) ShowWindowAsync(hwnd_, SW_RESTORE);
    if (recordVideo) timeEndPeriod(1);
    auto* e = new RecordEvent();
    if (!failure.empty()) {
        logLine(L"Abbruch: %ls", failure.c_str());
        e->kind = RecordEvent::Failed;
        e->text = failure;
    } else if (cancel_) {
        e->kind = RecordEvent::Finished;
        e->text = recordVideo ? L"Aufnahme abgebrochen" : L"Abgebrochen";
    } else if (!crashes.empty()) {
        // z. B. "FATAL ERROR: CopyNewEntity: invalid class index (221), out of range 0" – CS2 kann manche Demos selbst nicht abspielen
        std::wstring label = crashLabel(crashes.front());
        e->kind = RecordEvent::Failed;
        if (recordVideo && clips > 0)
            e->text = std::to_wstring(clips) + (clips == 1 ? L" Video fertig" : L" Videos fertig") + L" – bei " +
                      (crashes.size() == 1 ? std::wstring(L"einer Demo") : std::to_wstring(crashes.size()) + L" Demos") +
                      L" ist CS2 abgestürzt (" + label + L"), Details in fragdesk.log";
        else
            e->text = L"CS2 ist mit dieser Demo abgestürzt (" + label + L") – ein Fehler in CS2 selbst, Details in fragdesk.log";
        logLine(L"Abgestürzt: %d Demo(s), %d Video(s) fertig", int(crashes.size()), clips);
    } else {
        e->kind = RecordEvent::Finished;
        e->text = recordVideo ? (total == 1 ? L"Video fertig" : std::to_wstring(total) + L" Videos fertig")
                              : L"CS2 zeigt den Moment – für Matches CS2 danach einmal normal neu starten";
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
