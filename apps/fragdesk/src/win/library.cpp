// FragDesk – Bibliothek (siehe library.h)
#include "library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../api/jsonw.h"
#include "../core/json.h"
#include "common.h"
#include "replay_maker.h"

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
    // nach einem CS2-Update liest fdanalyze neue Demos oft erst mit einem FragDesk-Update wieder
    if (code == "CORRUPT") return L"Die Demo ist beschädigt – oder neuer als dieses FragDesk (nach einem CS2-Update: FragDesk aktualisieren)";
    if (code == "CRASH") return L"fdanalyze ist abgestürzt – Demo beschädigt oder neuer als dieses FragDesk (nach einem CS2-Update: FragDesk aktualisieren)";
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

static std::wstring safeStem(const std::string& checksum) {
    std::wstring safe;
    for (char c : checksum)
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_') safe += wchar_t(c);
    if (safe.empty()) safe = L"unbenannt";
    return safe;
}

std::wstring Library::matchFile(const std::string& checksum) { return joinPath(matchesDir(), safeStem(checksum) + L".json"); }
std::wstring Library::pulledFile(const std::string& checksum) { return joinPath(matchesDir(), safeStem(checksum) + L".src"); }

// matches\<Prüfsumme>.src: {"from":"server","fetched":…,"url":…,"matchtime":…,"until":…,"shareCode":…,"demo":…,"gone":…}
bool Library::writePulled(const std::string& checksum, const PulledInfo& p) {
    fdapi::JsonW j;
    j.obj()
        .kv("from", "server")
        .kb("fetched", p.fetched)
        .kv("url", p.url)
        .ki("matchtime", p.matchtime)
        .ki("until", p.until)
        .kv("shareCode", p.shareCode)
        .kv("demo", narrow(p.demo))
        .kb("gone", p.gone)
        .end();
    createDirs(matchesDir());
    return writeFileAtomic(pulledFile(checksum), j.take() + "\n");
}

bool Library::readPulled(const std::wstring& path, PulledInfo& p) {
    std::string text;
    json::Value v;
    if (!readFile(path, text, 64 * 1024) || !json::parse(text, v) || !v.isObject() || v.getString("from") != "server") return false;
    p.fetched = v.getBool("fetched");
    p.url = v.getString("url");
    p.shareCode = v.getString("shareCode");
    p.matchtime = std::max<int64_t>(0, int64_t(v.getNumber("matchtime")));
    p.until = std::max<int64_t>(0, int64_t(v.getNumber("until")));
    p.demo = widen(v.getString("demo"));
    p.gone = v.getBool("gone");
    return true;
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
        if (name.rfind(L"tmp-", 0) == 0 || !endsWithI(name, L".json")) continue;
        std::string text, err;
        auto m = std::make_unique<fd::Match>();
        if (readFile(joinPath(dir, name), text, 64u << 20) && fd::parseMatch(text, *m, err, false)) r->matches.push_back(std::move(m));
        else {
            ++r->failed;
            logLine(L"Match %ls nicht lesbar: %ls", name.c_str(), widen(err).c_str());
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    // vom eigenen Server geholt: Markierungen (.src) dazu, eine von Valve geladene Demo gleich eintragen
    std::map<std::string, fd::Match*> byChecksum;
    for (auto& m : r->matches) byChecksum[m->checksum] = m.get();
    f = FindFirstFileW(joinPath(dir, L"*.src").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !endsWithI(name, L".src")) continue;
            std::string cs = narrow(name.substr(0, name.size() - 4));
            auto it = byChecksum.find(cs);
            PulledInfo p;
            if (it == byChecksum.end() || !Library::readPulled(joinPath(dir, name), p)) {
                // ohne Auswertung (Abgleich unterbrochen) oder unlesbar: der nächste Abgleich holt das Match neu
                DeleteFileW(joinPath(dir, name).c_str());
                continue;
            }
            if (!p.demo.empty() && fileExists(p.demo)) {
                it->second->demoPath = narrow(p.demo);
                it->second->demoSize = fileSize(p.demo);
            } else p.demo.clear();   // im Explorer gelöscht: wieder ohne Demo
            if (it->second->shareCode.empty()) it->second->shareCode = p.shareCode;
            r->pulled[cs] = p;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
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
    pulled_ = std::move(r->pulled);
    sortMatches();
    loaded_ = true;
    ++generation;
    logLine(L"Bibliothek: %zu Matches geladen (%zu vom Server geholt), %d nicht lesbar", matches.size(), pulled_.size(), r->failed);
}

// ---- Vom eigenen Server geholt --------------------------------------------------------------------------------
const PulledInfo* Library::pulled(const std::string& checksum) const {
    auto it = pulled_.find(checksum);
    return it == pulled_.end() ? nullptr : &it->second;
}

std::vector<std::string> Library::pulledChecksums() const {
    std::vector<std::string> out;
    for (const auto& kv : pulled_) out.push_back(kv.first);
    return out;
}

void Library::applyLink(fd::Match& m) const {
    auto it = pulled_.find(m.checksum);
    if (it == pulled_.end()) return;
    if (m.shareCode.empty()) m.shareCode = it->second.shareCode;   // mit diesem Freigabecode hat der Server es gefunden
    if (it->second.demo.empty()) return;
    m.demoPath = narrow(it->second.demo);
    m.demoSize = std::max<int64_t>(0, fileSize(it->second.demo));
}

void Library::addPulled(std::vector<PulledMatch>& batch) {
    if (batch.empty()) return;
    for (PulledMatch& pm : batch) {
        if (!pm.match) continue;
        const std::string cs = pm.match->checksum;
        auto it = pulled_.find(cs);
        PulledInfo next = pm.info;
        if (it != pulled_.end()) {   // schon einmal geholt: geladene Demo bleibt; Quelle neu schreiben, falls geändert
            next.demo = it->second.demo;
            next.gone = it->second.gone && next.url == it->second.url;
            const PulledInfo& old = it->second;
            if (old.url != next.url || old.matchtime != next.matchtime || old.until != next.until || old.fetched != next.fetched ||
                old.shareCode != next.shareCode)
                writePulled(cs, next);
        }
        pulled_[cs] = next;
        applyLink(*pm.match);
        bool replaced = false;
        for (auto& old : matches)
            if (old->checksum == cs) {
                old = std::move(pm.match);
                replaced = true;
                break;
            }
        if (!replaced) matches.push_back(std::move(pm.match));
    }
    sortMatches();
    ++generation;
}

void Library::linkDemo(const std::string& checksum, const std::wstring& demo) {
    auto it = pulled_.find(checksum);
    if (it == pulled_.end()) return;
    it->second.demo = demo;
    it->second.gone = false;
    writePulled(checksum, it->second);
    for (auto& m : matches)
        if (m->checksum == checksum) applyLink(*m);
    ++generation;
}

std::wstring Library::downloadName(const std::string& url, const std::string& checksum) {
    auto digits = [](const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    std::string host, path;
    size_t scheme = url.find("://");
    if (scheme != std::string::npos) {
        size_t slash = url.find('/', scheme + 3);
        host = url.substr(scheme + 3, slash == std::string::npos ? std::string::npos : slash - scheme - 3);
        path = slash == std::string::npos ? std::string() : url.substr(slash);
        size_t q = path.find_first_of("?#");
        if (q != std::string::npos) path.resize(q);
    }
    std::string ext = ".dem.bz2";
    for (const char* e : {".dem.bz2", ".dem.gz", ".dem.zst", ".dem"})
        if (path.size() > strlen(e) && path.compare(path.size() - strlen(e), strlen(e), e) == 0) {
            ext = e;
            break;
        }
    std::string region;   // replay183.valve.net → 183
    if (host.compare(0, 6, "replay") == 0) {
        size_t n = 6;
        while (n < host.size() && host[n] >= '0' && host[n] <= '9') ++n;
        region = host.substr(6, n - 6);
    }
    // …/730/<a>_<b>.dem.bz2
    std::string name;
    if (path.size() > ext.size()) {
        std::string stem = path.substr(0, path.size() - ext.size());
        size_t s2 = stem.rfind('/');
        size_t s1 = s2 == std::string::npos || s2 == 0 ? std::string::npos : stem.rfind('/', s2 - 1);
        if (s1 != std::string::npos) {
            std::string app = stem.substr(s1 + 1, s2 - s1 - 1), file = stem.substr(s2 + 1);
            size_t us = file.find('_');
            if (digits(app) && us != std::string::npos && digits(file.substr(0, us)) && digits(file.substr(us + 1)))
                name = "match" + app + "_" + file + (region.empty() ? "" : "_" + region) + ext;
        }
    }
    if (name.empty()) name = "match730_" + narrow(safeStem(checksum)) + ext;
    return widen(name);
}

void Library::markDemoGone(const std::string& checksum) {
    auto it = pulled_.find(checksum);
    if (it == pulled_.end() || it->second.gone) return;
    it->second.gone = true;
    writePulled(checksum, it->second);
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
    applyLink(out);   // vom Server geholt, Demo von Valve geladen: die liegt hier
    return true;
}

bool Library::remove(const std::string& checksum) {
    DeleteFileW(matchFile(checksum).c_str());
    DeleteFileW(joinPath(matchesDir(), widen(checksum) + L".replay").c_str());   // 2D-Daten
    DeleteFileW(joinPath(matchesDir(), widen(checksum) + L".heat").c_str());     // Heatmap
    DeleteFileW(pulledFile(checksum).c_str());                                    // Markierung „vom Server“
    pulled_.erase(checksum);
    auto it = std::find_if(matches.begin(), matches.end(), [&](const auto& m) { return m->checksum == checksum; });
    if (it == matches.end()) return false;
    matches.erase(it);
    ++generation;
    return true;
}

void Library::insertMatch(std::unique_ptr<fd::Match> m) {
    ++generation;
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
    // Von Valve geladen (demosDir): gehört eine Demo zu einem geholten Match ohne Demo, gleich verbinden (z. B. wenn
    // FragDesk nach dem Laden beendet wurde, bevor es sie eintragen konnte)
    if (!pulled_.empty()) {
        std::map<std::wstring, std::string> expect;   // erwarteter Dateiname -> Prüfsumme
        for (const auto& kv : pulled_)
            if (!kv.second.url.empty() && (kv.second.demo.empty() || !fileExists(kv.second.demo)))
                expect[lowerCase(downloadName(kv.second.url, kv.first))] = kv.first;
        std::wstring mine = demosDir();
        for (const auto& f : r->files) {
            if (expect.empty()) break;
            if (!samePath(parentDir(f.path), mine)) continue;
            auto it = expect.find(lowerCase(fileName(f.path)));
            if (it == expect.end()) continue;
            logLine(L"Bibliothek: %ls gehört zu %hs (vom Server geholt)", f.path.c_str(), it->second.c_str());
            linkDemo(it->second, f.path);
            expect.erase(it);
        }
    }
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

void Library::setWithReplay(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    withReplay_ = on;
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
    if (e->kind == AnalysisEvent::Done && e->match) {
        // hier ausgewertet: ab jetzt ein eigenes Match (der Abgleich lädt es hoch). Eine von Valve geladene Kopie, die
        // nicht die ausgewertete Demo ist, braucht es dann nicht mehr.
        auto it = pulled_.find(e->match->checksum);
        if (it != pulled_.end()) {
            if (!it->second.demo.empty() && !samePath(it->second.demo, widen(e->match->demoPath))) DeleteFileW(it->second.demo.c_str());
            DeleteFileW(pulledFile(it->first).c_str());
            pulled_.erase(it);
        }
        insertMatch(std::move(e->match));
    }
}

void Library::worker() {
    std::wstring exe = joinPath(selfDir(), kAnalyzerExe);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {   // fdanalyze endet mit FragDesk und bekommt höchstens kAnalyzerMemoryLimit Arbeitsspeicher
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        li.ProcessMemoryLimit = kAnalyzerMemoryLimit;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    int counter = 0;
    for (;;) {
        std::wstring path;
        bool withReplay = false;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            path = queue_.front();
            queue_.pop_front();
            running_ = path;
            withReplay = withReplay_;
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
        std::wstring replayTmp = out.substr(0, out.size() - 5) + L".replay";   // 2D-Daten (nur mit withReplay)

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
            if (withReplay) cmd += L" -replay \"" + replayTmp + L"\"";
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
                    failText = analysisErrorText(code.empty() ? (exitCode != 0 ? "CRASH" : "INTERNAL") : code);
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
                // Heatmap-Daten (fdanalyze legt sie neben die Ausgabe) mitnehmen; ohne sie keine alte stehen lassen
                std::wstring heatTmp = out.substr(0, out.size() - 5) + L".heat", heatFinal = final.substr(0, final.size() - 5) + L".heat";
                if (!fileExists(heatTmp) || !MoveFileExW(heatTmp.c_str(), heatFinal.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    DeleteFileW(heatTmp.c_str());
                    DeleteFileW(heatFinal.c_str());
                }
                // 2D-Daten aus demselben Lauf; fehlen sie, bleibt eine vorhandene (dieselbe Demo) stehen
                if (withReplay && fileExists(replayTmp) && !MoveFileExW(replayTmp.c_str(), replayFile(m->checksum).c_str(), MOVEFILE_REPLACE_EXISTING))
                    logLine(L"2D-Daten nicht übernommen: %ls", replayTmp.c_str());
                if (MoveFileExW(out.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    DeleteFileW(pulledFile(m->checksum).c_str());   // hier ausgewertet: nicht mehr „vom Server“
                    logLine(L"Ausgewertet: %ls -> %ls", path.c_str(), final.c_str());
                    send(AnalysisEvent::Done, L"", std::move(m));
                } else failText = analysisErrorText("WRITE");
            } else {
                failText = analysisErrorText("INTERNAL");
                logLine(L"Ergebnis nicht lesbar: %ls", widen(err).c_str());
            }
        }
        DeleteFileW(out.c_str());
        DeleteFileW((out.substr(0, out.size() - 5) + L".heat").c_str());   // falls nicht übernommen
        DeleteFileW(replayTmp.c_str());
        DeleteFileW((replayTmp + L".part").c_str());
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
