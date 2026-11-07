// FragDesk – Bibliothek (siehe library.h)
#include "library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "common.h"

namespace fdw {

namespace {

bool isDemoName(const std::wstring& name) {
    for (const wchar_t* ext : {L".dem", L".dem.zst", L".dem.gz", L".dem.bz2"})
        if (endsWithI(name, ext)) return true;
    // FACEIT-Downloads ohne .dem (z. B. 1-abc…-1-1.zst)
    if ((endsWithI(name, L".zst") || endsWithI(name, L".gz") || endsWithI(name, L".bz2")) && name.rfind(L"1-", 0) == 0) return true;
    return false;
}

int64_t nowEpoch() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return int64_t((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

std::wstring stepText(const std::string& step) {
    if (step == "unpack") return L"Wird entpackt …";
    if (step == "parse") return L"Demo wird gelesen …";
    if (step == "stats") return L"Statistiken werden berechnet …";
    if (step == "write") return L"Wird gespeichert …";
    return L"Läuft …";
}

struct Known {
    std::wstring path, name;
    int64_t size;
};

std::vector<Known> knownList(const std::vector<std::unique_ptr<fd::Match>>& matches) {
    std::vector<Known> out;
    for (const auto& m : matches) {
        std::wstring p = widen(m->demoPath);
        out.push_back({p, lowerCase(fileName(p)), m->demoSize});
    }
    return out;
}

bool matchesKnown(const std::vector<Known>& known, const std::wstring& path, int64_t size) {
    std::wstring name = lowerCase(fileName(path));
    for (const auto& k : known)
        if (k.size == size && (k.name == name || samePath(k.path, path))) return true;
    return false;
}

}  // namespace

std::wstring analysisErrorText(const std::string& code) {
    if (code == "NOT_FOUND") return L"Datei nicht gefunden";
    if (code == "READ") return L"Datei kann nicht gelesen werden";
    if (code == "WRITE") return L"Ergebnis kann nicht gespeichert werden";
    if (code == "FORMAT") return L"Das ist keine CS2-Demo";
    if (code == "CSGO") return L"CS:GO-Demos werden nicht unterstützt";
    if (code == "UNPACK") return L"Entpacken fehlgeschlagen – Datei unvollständig?";
    if (code == "POV") return L"Selbst aufgenommene POV-Demos werden nicht unterstützt";
    if (code == "SOURCE") return L"Diese Demo-Quelle wird nicht unterstützt";
    if (code == "CORRUPT") return L"Die Demo ist beschädigt";
    if (code == "EMPTY") return L"Die Demo enthält keine vollständige Runde";
    if (code == "MISSING") return L"fdanalyze.exe fehlt – bitte FragDesk neu installieren";
    if (code == "START") return L"Auswertung konnte nicht gestartet werden";
    return L"Unerwarteter Fehler bei der Auswertung";
}

Library::~Library() {
    stopWatching();
    {
        std::lock_guard<std::mutex> lock(mu_);
        quit_ = true;
        if (child_) TerminateProcess(child_, 1);
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

static void postTo(HWND hwnd, UINT msg, void* p) {
    if (!hwnd || !PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(p))) {
        // Fenster weg: Nachricht verwerfen (Objekte gehören dem Empfänger)
        if (msg == WM_APP_LIBRARY) delete static_cast<LibraryLoad*>(p);
        else if (msg == WM_APP_DEMOS) delete static_cast<ScanResult*>(p);
        else if (msg == WM_APP_ANALYSIS) delete static_cast<AnalysisEvent*>(p);
    }
}

void Library::post(UINT msg, void* p) { postTo(hwnd_, msg, p); }

void Library::setFolders(const std::vector<std::pair<std::wstring, std::wstring>>& folders) {
    folders_.clear();
    for (const auto& f : folders)
        if (!f.first.empty()) folders_.push_back(f);
}

std::wstring Library::matchFile(const std::string& checksum) {
    std::wstring safe;
    for (char c : checksum)
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_') safe += wchar_t(c);
    if (safe.empty()) safe = L"unbenannt";
    return joinPath(matchesDir(), safe + L".json");
}

// ---- Laden ----------------------------------------------------------------------------------------------------
static LibraryLoad* loadAll() {
    auto* r = new LibraryLoad();
    WIN32_FIND_DATAW fd;
    std::wstring dir = matchesDir();
    HANDLE f = FindFirstFileW(joinPath(dir, L"*.json").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return r;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (name.rfind(L"tmp-", 0) == 0) continue;
        std::string text, err;
        auto m = std::make_unique<fd::Match>();
        if (readFile(joinPath(dir, name), text, 64u << 20) && fd::parseMatch(text, *m, err, false)) r->matches.push_back(std::move(m));
        else {
            ++r->failed;
            logLine(L"Match %ls nicht lesbar: %ls", name.c_str(), widen(err).c_str());
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return r;
}

void Library::loadAsync() {
    HWND h = hwnd_;   // der Thread darf die Bibliothek nicht überleben müssen
    std::thread([h] { postTo(h, WM_APP_LIBRARY, loadAll()); }).detach();
}

void Library::loadNow() { onLoaded(loadAll()); }

void Library::onLoaded(LibraryLoad* r) {
    std::unique_ptr<LibraryLoad> own(r);
    matches = std::move(r->matches);
    sortMatches();
    loaded_ = true;
    logLine(L"Bibliothek: %zu Matches geladen, %d nicht lesbar", matches.size(), r->failed);
}

void Library::sortMatches() {
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a->date > b->date; });
}

const fd::Match* Library::find(const std::string& checksum) const {
    for (const auto& m : matches)
        if (m->checksum == checksum) return m.get();
    return nullptr;
}

bool Library::loadDetail(const std::string& checksum, fd::Match& out, std::wstring& err) const {
    std::string text, perr;
    if (!readFile(matchFile(checksum), text, 64u << 20)) {
        err = L"Datei der Auswertung fehlt";
        return false;
    }
    if (!fd::parseMatch(text, out, perr, true)) {
        err = L"Auswertung nicht lesbar: " + widen(perr);
        return false;
    }
    return true;
}

bool Library::remove(const std::string& checksum) {
    DeleteFileW(matchFile(checksum).c_str());
    DeleteFileW(joinPath(matchesDir(), widen(checksum) + L".replay").c_str());   // 2D-Daten
    auto it = std::find_if(matches.begin(), matches.end(), [&](const auto& m) { return m->checksum == checksum; });
    if (it == matches.end()) return false;
    matches.erase(it);
    return true;
}

void Library::insertMatch(std::unique_ptr<fd::Match> m) {
    for (auto& old : matches)
        if (old->checksum == m->checksum) {
            old = std::move(m);
            sortMatches();
            return;
        }
    matches.push_back(std::move(m));
    sortMatches();
}

// ---- Demos suchen ---------------------------------------------------------------------------------------------
std::string Library::ignoreKey(const DemoFile& d) { return narrow(lowerCase(d.path)) + "|" + std::to_string(d.size); }

bool Library::isKnown(const std::wstring& path, int64_t size) const { return matchesKnown(knownList(matches), path, size); }

void Library::scanAsync() {
    auto folders = folders_;
    HWND hwnd = hwnd_;
    std::thread([hwnd, folders] {
        auto* r = new ScanResult();
        int64_t now = nowEpoch();
        for (const auto& f : folders) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(joinPath(f.first, L"*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            int seen = 0;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (++seen > 20000) break;   // riesige Ordner nicht ewig durchsuchen
                std::wstring name = fd.cFileName;
                if (!isDemoName(name)) continue;
                int64_t size = (int64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                if (size < (1 << 20)) continue;   // echte Demos sind deutlich größer
                ULARGE_INTEGER u;
                u.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                u.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                int64_t mtime = u.QuadPart > 116444736000000000ULL ? int64_t((u.QuadPart - 116444736000000000ULL) / 10000000ULL) : 0;
                if (now - mtime < 3) continue;   // wird vermutlich noch geschrieben
                DemoFile d;
                d.path = joinPath(f.first, name);
                d.size = size;
                d.mtime = mtime;
                d.origin = f.second;
                r->files.push_back(std::move(d));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        postTo(hwnd, WM_APP_DEMOS, r);
    }).detach();
}

void Library::onScanned(ScanResult* r) {
    std::unique_ptr<ScanResult> own(r);
    auto known = knownList(matches);
    std::vector<DemoFile> next;
    for (auto& f : r->files) {
        if (matchesKnown(known, f.path, f.size)) continue;
        if (std::find(ignored_.begin(), ignored_.end(), ignoreKey(f)) != ignored_.end()) continue;
        bool dup = false;
        for (const auto& n : next)
            if (n.size == f.size && lowerCase(fileName(n.path)) == lowerCase(fileName(f.path))) dup = true;
        if (dup) continue;
        // Zustand laufender Aufträge übernehmen
        for (const auto& old : demos)
            if (samePath(old.path, f.path)) {
                f.state = old.state;
                f.step = old.step;
                f.error = old.error;
            }
        next.push_back(std::move(f));
    }
    // Hinzugefügte Dateien und laufende Aufträge behalten, auch wenn sie in keinem Ordner liegen
    for (const auto& old : demos) {
        bool present = std::any_of(next.begin(), next.end(), [&](const DemoFile& n) { return samePath(n.path, old.path); });
        if (!present && (old.origin == L"Hinzugefügt" || old.state == DemoFile::State::Queued || old.state == DemoFile::State::Running) &&
            fileExists(old.path))
            next.push_back(old);
    }
    std::stable_sort(next.begin(), next.end(), [](const DemoFile& a, const DemoFile& b) { return a.mtime > b.mtime; });
    demos = std::move(next);
}

void Library::addFiles(const std::vector<std::wstring>& files) {
    for (const auto& p : files) {
        int64_t size = fileSize(p);
        if (size <= 0) continue;
        if (isKnown(p, size)) continue;
        bool present = false;
        for (auto& d : demos)
            if (samePath(d.path, p)) present = true;
        if (!present) {
            DemoFile d;
            d.path = p;
            d.size = size;
            d.mtime = fileTime(p);
            d.origin = L"Hinzugefügt";
            demos.insert(demos.begin(), d);
        }
        analyze(p);
    }
}

// ---- Auswerten ------------------------------------------------------------------------------------------------
void Library::analyze(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& q : queue_)
            if (samePath(q, path)) return;
        if (!running_.empty() && samePath(running_, path)) return;
        queue_.push_back(path);
    }
    for (auto& d : demos)
        if (samePath(d.path, path)) {
            d.state = DemoFile::State::Queued;
            d.error.clear();
            d.step = L"Wartet …";
        }
    if (!worker_.joinable()) worker_ = std::thread(&Library::worker, this);
    cv_.notify_one();
}

void Library::analyzeAll(bool retryFailed) {
    for (const auto& d : demos)
        if (d.state == DemoFile::State::New || (retryFailed && d.state == DemoFile::State::Failed)) analyze(d.path);
}

bool Library::busy() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mu_));
    return !queue_.empty() || !running_.empty();
}

int Library::queued() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mu_));
    return int(queue_.size()) + (running_.empty() ? 0 : 1);
}

void Library::onEvent(AnalysisEvent* e) {
    std::unique_ptr<AnalysisEvent> own(e);
    for (auto it = demos.begin(); it != demos.end(); ++it) {
        if (!samePath(it->path, e->path)) continue;
        switch (e->kind) {
            case AnalysisEvent::Step:
                it->state = DemoFile::State::Running;
                it->step = e->text;
                break;
            case AnalysisEvent::Failed:
                it->state = DemoFile::State::Failed;
                it->error = e->text;
                lastError = fileName(e->path) + L": " + e->text;
                break;
            case AnalysisEvent::Done:
                demos.erase(it);
                break;
        }
        break;
    }
    if (e->kind == AnalysisEvent::Done && e->match) insertMatch(std::move(e->match));
}

void Library::worker() {
    std::wstring exe = joinPath(selfDir(), kAnalyzerExe);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {   // fdanalyze endet mit FragDesk
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    int counter = 0;
    for (;;) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            path = queue_.front();
            queue_.pop_front();
            running_ = path;
        }
        auto send = [&](AnalysisEvent::Kind k, const std::wstring& text, std::unique_ptr<fd::Match> m = nullptr) {
            auto* e = new AnalysisEvent();
            e->kind = k;
            e->path = path;
            e->text = text;
            e->match = std::move(m);
            post(WM_APP_ANALYSIS, e);
        };
        send(AnalysisEvent::Step, L"Wird gestartet …");
        createDirs(matchesDir());
        createDirs(tempDir());
        wchar_t tmpName[64];
        swprintf(tmpName, 64, L"tmp-%lu-%d.json", GetCurrentProcessId(), ++counter);
        std::wstring out = joinPath(matchesDir(), tmpName);

        std::string code;
        std::wstring failText;
        if (!fileExists(exe)) {
            failText = analysisErrorText("MISSING");
        } else {
            SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
            HANDLE rd = nullptr, wr = nullptr;
            CreatePipe(&rd, &wr, &sa, 0);
            SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
            std::wstring cmd = L"\"" + exe + L"\" -demo \"" + path + L"\" -out \"" + out + L"\" -temp \"" + tempDir() + L"\"";
            STARTUPINFOW si = {};
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = wr;
            si.hStdError = wr;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION pi = {};
            std::vector<wchar_t> buf(cmd.begin(), cmd.end());
            buf.push_back(0);
            BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
            CloseHandle(wr);
            if (!ok) {
                failText = analysisErrorText("START");
                logLine(L"fdanalyze startet nicht (%lu)", GetLastError());
            } else {
                if (job) AssignProcessToJobObject(job, pi.hProcess);
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    child_ = pi.hProcess;
                }
                ResumeThread(pi.hThread);
                std::string pending, done;
                char chunk[512];
                DWORD got = 0;
                while (ReadFile(rd, chunk, sizeof chunk, &got, nullptr) && got > 0) {
                    pending.append(chunk, got);
                    size_t nl;
                    while ((nl = pending.find('\n')) != std::string::npos) {
                        std::string line = pending.substr(0, nl);
                        pending.erase(0, nl + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.rfind("STEP ", 0) == 0) send(AnalysisEvent::Step, stepText(line.substr(5)));
                        else if (line.rfind("ERROR ", 0) == 0) {
                            std::string rest = line.substr(6);
                            size_t sp = rest.find(' ');
                            code = rest.substr(0, sp);
                            logLine(L"Auswertung %ls: %ls", path.c_str(), widen(rest).c_str());
                        } else if (line.rfind("DONE ", 0) == 0) done = line;
                    }
                }
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exitCode = 1;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    child_ = nullptr;
                }
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                if (exitCode != 0 || done.empty()) {
                    failText = analysisErrorText(code.empty() ? "INTERNAL" : code);
                    if (code.empty()) logLine(L"fdanalyze endete mit %lu", exitCode);
                }
            }
            CloseHandle(rd);
        }

        if (failText.empty()) {
            std::string text, err;
            auto m = std::make_unique<fd::Match>();
            if (readFile(out, text, 64u << 20) && fd::parseMatch(text, *m, err, false) && !m->checksum.empty()) {
                std::wstring final = matchFile(m->checksum);
                if (MoveFileExW(out.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    logLine(L"Ausgewertet: %ls -> %ls", path.c_str(), final.c_str());
                    send(AnalysisEvent::Done, L"", std::move(m));
                } else failText = analysisErrorText("WRITE");
            } else {
                failText = analysisErrorText("INTERNAL");
                logLine(L"Ergebnis nicht lesbar: %ls", widen(err).c_str());
            }
        }
        DeleteFileW(out.c_str());
        if (!failText.empty()) send(AnalysisEvent::Failed, failText);
        {
            std::lock_guard<std::mutex> lock(mu_);
            running_.clear();
        }
    }
    if (job) CloseHandle(job);
}

// ---- Ordner überwachen ----------------------------------------------------------------------------------------
void Library::startWatching() {
    stopWatching();
    std::vector<std::wstring> dirs;
    for (const auto& f : folders_)
        if (dirExists(f.first)) dirs.push_back(f.first);
    if (dirs.empty() || !hwnd_) return;
    watchStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND hwnd = hwnd_;
    HANDLE stop = watchStop_;
    watcher_ = std::thread([dirs, hwnd, stop] {
        std::vector<HANDLE> handles = {stop};
        for (const auto& d : dirs) {
            if (handles.size() >= MAXIMUM_WAIT_OBJECTS) break;
            HANDLE h = FindFirstChangeNotificationW(d.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
            if (h != INVALID_HANDLE_VALUE) handles.push_back(h);
        }
        for (;;) {
            DWORD r = WaitForMultipleObjects(DWORD(handles.size()), handles.data(), FALSE, INFINITE);
            if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
            size_t i = r - WAIT_OBJECT_0;
            if (i == 0 || i >= handles.size()) continue;
            PostMessageW(hwnd, WM_APP_FOLDER, 0, 0);
            // Downloads melden viele Änderungen hintereinander: kurz sammeln, die Oberfläche wartet ohnehin 1,5 s
            if (WaitForSingleObject(stop, 250) == WAIT_OBJECT_0) break;
            if (!FindNextChangeNotification(handles[i])) {   // Ordner gelöscht, Laufwerk weg: nicht weiter überwachen
                FindCloseChangeNotification(handles[i]);
                handles.erase(handles.begin() + std::ptrdiff_t(i));
            }
        }
        for (size_t i = 1; i < handles.size(); ++i) FindCloseChangeNotification(handles[i]);
    });
}

void Library::stopWatching() {
    if (watchStop_) SetEvent(watchStop_);
    if (watcher_.joinable()) watcher_.join();
    if (watchStop_) CloseHandle(watchStop_);
    watchStop_ = nullptr;
}

}  // namespace fdw
