// efficient lamp – Aufträge (siehe jobs.h)
#include "jobs.h"

#include <cctype>
#include <vector>

#include "../core/sha256.h"
#include "../core/zip.h"
#include "common.h"
#include "net.h"
#include "system.h"

namespace lamp {

namespace {

std::wstring safeVersionDir(const std::string& v) {
    std::wstring out;
    for (char c : v) out += (std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? wchar_t(c) : L'_';
    return out.empty() ? L"0" : out;
}

HANDLE g_quit = nullptr;   // gemeinsames Beenden-Signal für wartende Threads

struct Waiter {
    HANDLE process;
    HWND hwnd;
};

// Für Skripte, auf die der Launcher nicht warten muss (Einstellungsmenü): danach den Zustand neu erkennen
DWORD WINAPI waitThenRefresh(LPVOID p) {
    Waiter* w = static_cast<Waiter*>(p);
    HANDLE hs[2] = {w->process, g_quit};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0) PostMessageW(w->hwnd, WM_APP_REFRESH, 0, 0);
    CloseHandle(w->process);
    delete w;
    return 0;
}

}  // namespace

Jobs::Jobs() { InitializeCriticalSection(&cs_); }

Jobs::~Jobs() {
    stop();
    DeleteCriticalSection(&cs_);
}

std::wstring Jobs::packageRoot(const std::string& appId) { return joinPath(joinPath(dataDir(), L"packages"), widen(appId)); }

void Jobs::start(HWND notify) {
    hwnd_ = notify;
    if (!g_quit) g_quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, threadMain, this, 0, nullptr);
}

void Jobs::stop() {
    if (!thread_) return;
    quit_ = true;
    cancelCurrent_ = true;
    SetEvent(g_quit);
    SetEvent(wake_);
    WaitForSingleObject(thread_, 5000);
    CloseHandle(thread_);
    CloseHandle(wake_);
    thread_ = nullptr;
}

void Jobs::add(JobKind kind, const cat::App& app) {
    EnterCriticalSection(&cs_);
    queue_.push_back(Job{kind, app});
    LeaveCriticalSection(&cs_);
    post(Job{kind, app}, JobPhase::Queued);
    SetEvent(wake_);
}

bool Jobs::pending(const std::string& appId) const {
    EnterCriticalSection(&cs_);
    bool found = current_ == appId;
    for (const Job& j : queue_)
        if (j.app.id == appId) found = true;
    LeaveCriticalSection(&cs_);
    return found;
}

bool Jobs::busy() const {
    EnterCriticalSection(&cs_);
    bool b = !current_.empty() || !queue_.empty();
    LeaveCriticalSection(&cs_);
    return b;
}

bool Jobs::setupRunning() const { return setupRunning_; }

bool Jobs::cancel(const std::string& appId) {
    EnterCriticalSection(&cs_);
    bool done = false;
    for (auto it = queue_.begin(); it != queue_.end(); ++it)
        if (it->app.id == appId) {
            Job j = *it;
            queue_.erase(it);
            post(j, JobPhase::Finished, -1, false, L"abgebrochen");
            done = true;
            break;
        }
    if (!done && current_ == appId && !setupRunning_) {
        cancelCurrent_ = true;   // greift beim nächsten Download-Stück
        done = true;
    }
    LeaveCriticalSection(&cs_);
    return done;
}

void Jobs::post(const Job& job, JobPhase phase, double progress, bool ok, const std::wstring& msg) {
    auto* e = new JobEvent();
    e->appId = job.app.id;
    e->kind = job.kind;
    e->phase = phase;
    e->progress = progress;
    e->ok = ok;
    e->message = msg;
    if (!hwnd_ || !PostMessageW(hwnd_, WM_APP_JOB, 0, reinterpret_cast<LPARAM>(e))) delete e;
}

DWORD WINAPI Jobs::threadMain(LPVOID self) {
    static_cast<Jobs*>(self)->loop();
    return 0;
}

void Jobs::loop() {
    while (!quit_) {
        Job job;
        bool have = false;
        EnterCriticalSection(&cs_);
        if (!queue_.empty()) {
            job = queue_.front();
            queue_.pop_front();
            current_ = job.app.id;
            cancelCurrent_ = false;
            have = true;
        }
        LeaveCriticalSection(&cs_);
        if (!have) {
            WaitForSingleObject(wake_, INFINITE);
            continue;
        }
        run(job);
        EnterCriticalSection(&cs_);
        current_.clear();
        LeaveCriticalSection(&cs_);
    }
}

bool Jobs::ensurePackage(const Job& job, std::wstring& dir, std::wstring& err) {
    const cat::App& app = job.app;
    std::wstring root = packageRoot(app.id);
    dir = joinPath(root, safeVersionDir(app.version));
    std::wstring marker = joinPath(dir, L".efficient-lamp");
    std::string have;
    if (readFile(marker, have, 1024) && have == app.sha256) return true;   // schon entpackt und geprüft

    post(job, JobPhase::Download, 0);
    std::string zipData;
    ULONGLONG lastPost = 0;
    bool ok = download(widen(app.url), zipData, err, [&](uint64_t done, uint64_t total) {
        if (cancelCurrent_) return false;
        if (!total) total = app.size;
        ULONGLONG now = GetTickCount64();
        if (total && now - lastPost > 80) {
            lastPost = now;
            post(job, JobPhase::Download, double(done) / double(total));
        }
        return true;
    });
    if (!ok) {
        err = L"Download fehlgeschlagen: " + err;
        return false;
    }
    post(job, JobPhase::Extract);
    if (app.size && zipData.size() != app.size) {
        err = L"Download hat die falsche Größe – bitte später erneut versuchen";
        return false;
    }
    std::string hash = sha256Hex(zipData.data(), zipData.size());
    if (hash != app.sha256) {
        logLine(L"%ls: SHA-256 erwartet %ls, erhalten %ls", widen(app.id).c_str(), widen(app.sha256).c_str(), widen(hash).c_str());
        err = L"Prüfsumme stimmt nicht – Download verworfen, nichts installiert.";
        return false;
    }

    std::vector<zip::Entry> entries;
    std::string zerr;
    if (!zip::list(zipData, entries, zerr)) {
        err = L"Paket unlesbar: " + widen(zerr);
        return false;
    }
    std::wstring tmp = dir + L".part";
    deleteTree(tmp);
    if (!createDirs(tmp)) {
        err = L"Ordner konnte nicht angelegt werden: " + tmp;
        return false;
    }
    const std::string prefix = app.folder.empty() ? std::string() : app.folder + "/";
    int files = 0;
    for (const zip::Entry& e : entries) {
        if (e.isDir) continue;
        if (!prefix.empty() && e.name.compare(0, prefix.size(), prefix) != 0) continue;   // gehört nicht zum Programm
        std::string rel = e.name.substr(prefix.size());
        if (rel.empty()) continue;
        if (!zip::safeName(rel)) {
            err = L"Paket enthält einen unsicheren Dateinamen – abgebrochen";
            deleteTree(tmp);
            return false;
        }
        std::string data;
        if (!zip::read(zipData, e, data, zerr)) {
            err = L"Paket beschädigt: " + widen(zerr);
            deleteTree(tmp);
            return false;
        }
        std::wstring wrel = widen(rel);
        for (wchar_t& c : wrel)
            if (c == L'/') c = L'\\';
        std::wstring target = joinPath(tmp, wrel);
        createDirs(parentDir(target));
        if (!writeFileAtomic(target, data)) {
            err = L"Datei konnte nicht geschrieben werden: " + wrel;
            deleteTree(tmp);
            return false;
        }
        ++files;
    }
    if (files == 0) {
        err = L"Paket ist leer";
        deleteTree(tmp);
        return false;
    }
    deleteTree(dir);
    if (!MoveFileExW(tmp.c_str(), dir.c_str(), 0)) {
        err = L"Paket konnte nicht abgelegt werden (Fehler " + std::to_wstring(GetLastError()) + L")";
        deleteTree(tmp);
        return false;
    }
    writeFileAtomic(marker, app.sha256);
    logLine(L"%ls %ls: Paket geladen und geprüft (%d Dateien)", widen(app.name).c_str(), widen(app.version).c_str(), files);
    return true;
}

void Jobs::run(const Job& job) {
    const cat::App& app = job.app;
    const cat::Command& cmd = job.kind == JobKind::Install ? app.install : job.kind == JobKind::Uninstall ? app.uninstall : app.open;
    std::wstring dir, err;
    if (cmd.script.empty()) {
        post(job, JobPhase::Finished, -1, false, L"Für diese Aktion ist kein Skript hinterlegt");
        return;
    }
    if (!ensurePackage(job, dir, err)) {
        logLine(L"%ls: %ls", widen(app.name).c_str(), err.c_str());
        post(job, JobPhase::Finished, -1, false, cancelCurrent_ ? L"abgebrochen" : err);
        return;
    }
    if (quit_) return;
    post(job, JobPhase::Setup);
    HANDLE proc = startScript(dir, widen(cmd.script), widen(cmd.args), err);
    if (!proc) {
        post(job, JobPhase::Finished, -1, false, err);
        return;
    }
    if (!cmd.wait) {   // z. B. Einstellungsmenü: nicht blockieren
        CreateThread(nullptr, 0, waitThenRefresh, new Waiter{proc, hwnd_}, 0, nullptr);
        post(job, JobPhase::Finished, -1, true);
        return;
    }
    setupRunning_ = true;
    HANDLE hs[2] = {proc, g_quit};
    DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
    setupRunning_ = false;
    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    if (w != WAIT_OBJECT_0) return;   // Launcher wird beendet; das Setup-Fenster bleibt offen
    logLine(L"%ls: Skript %ls beendet (Code %lu)", widen(app.name).c_str(), widen(cmd.script).c_str(), code);
    if (job.kind == JobKind::Install) {
        // ältere Paketversionen aufräumen
        WIN32_FIND_DATAW fd;
        std::wstring root = packageRoot(app.id);
        HANDLE f = FindFirstFileW(joinPath(root, L"*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                if (lowerCase(fd.cFileName) != lowerCase(safeVersionDir(app.version))) deleteTree(joinPath(root, fd.cFileName));
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    if (job.kind == JobKind::Uninstall) deleteTree(packageRoot(app.id));   // Paket wird nicht mehr gebraucht
    post(job, JobPhase::Finished, -1, true);
}

}  // namespace lamp
