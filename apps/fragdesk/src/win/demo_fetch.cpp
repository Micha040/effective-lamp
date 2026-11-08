// FragDesk – Demo von Valve laden (siehe demo_fetch.h)
#include "demo_fetch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "library.h"
#include "net.h"
#include "recorder.h"

namespace fdw {

namespace {

constexpr int64_t kMaxDemo = int64_t(1) << 30;          // 1 GB (Valve-Demos: meist 50–200 MB gepackt)
constexpr ULONGLONG kMaxActiveMs = 30ull * 60 * 1000;   // höchstens 30 Minuten laden (Pausen zählen nicht)
constexpr ULONGLONG kPauseMs = 5000;                    // in der Pause so oft nachsehen, ob CS2 noch läuft
constexpr int64_t kKeepSeconds = 30 * 86400;            // Valve hebt Demos etwa einen Monat auf

struct DemoFetchEvent {
    enum Kind { Progress, Paused, Done, Failed, Gone, Cancelled } kind = Progress;
    std::string checksum;
    int64_t done = 0, total = -1;
    std::wstring path, text;
};

struct Shared {
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> cancel{false}, quit{false}, finished{false};
    std::mutex mu;
    std::condition_variable cv;
    HttpClient http;   // nur für diesen Download; abort() beim Abbrechen und Beenden
};

void post(Shared& sh, DemoFetchEvent* e) {
    HWND h = sh.hwnd.load();
    if (!h || !PostMessageW(h, WM_APP_DEMOFETCH, 0, reinterpret_cast<LPARAM>(e))) delete e;
}

// .dem.bz2 (Valve), ungepackt (PBDEMS2), gzip, zstd – was fdanalyze liest
bool looksLikeDemo(const char* p, size_t n) {
    auto has = [&](const char* magic, size_t len) { return n >= len && std::memcmp(p, magic, len) == 0; };
    return has("BZh", 3) || has("PBDEMS2\0", 8) || has("\x1f\x8b", 2) || has("\x28\xb5\x2f\xfd", 4);
}

// wartet ms oder bis abgebrochen wird; false = abgebrochen
bool waitFor(Shared& sh, ULONGLONG ms) {
    std::unique_lock<std::mutex> lk(sh.mu);
    return !sh.cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return sh.cancel.load() || sh.quit.load(); });
}

void worker(std::shared_ptr<Shared> sh, std::string checksum, WebUrl url, std::wstring file) {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // Prozessor und Festplatte: Hintergrund
    auto send = [&](DemoFetchEvent::Kind k, int64_t done, int64_t total, const std::wstring& text = L"", const std::wstring& path = L"") {
        auto* e = new DemoFetchEvent();
        e->kind = k;
        e->checksum = checksum;
        e->done = done;
        e->total = total;
        e->text = text;
        e->path = path;
        post(*sh, e);
    };
    ULONGLONG active = 0, cs2At = 0;
    bool cs2 = false, paused = false;
    auto cs2Now = [&] {   // nur der Prozessname (Toolhelp), höchstens alle 2 s
        ULONGLONG now = GetTickCount64();
        if (!cs2At || now - cs2At >= 2000) {
            cs2 = isProcessRunning(L"cs2.exe");
            cs2At = now;
        }
        return cs2;
    };
    const std::wstring part = file + L".part";
    int netFails = 0;
    int64_t done = std::max<int64_t>(0, fileSize(part)), total = -1;
    logLine(L"Demo von Valve: lade %ls%hs nach %ls%ls", url.host.c_str(), url.path.c_str(), file.c_str(), done > 0 ? L" (geht weiter)" : L"");
    for (;;) {
        if (sh->cancel || sh->quit) break;
        if (cs2Now()) {   // CS2 läuft: nichts laden, der Ping soll nicht leiden
            if (!paused) logLine(L"Demo von Valve: Pause, CS2 läuft");
            paused = true;
            send(DemoFetchEvent::Paused, done, total);
            if (!waitFor(*sh, kPauseMs)) break;
            continue;
        }
        paused = false;
        ULONGLONG t0 = GetTickCount64(), lastPost = 0;
        bool timeUp = false;
        DownloadRequest rq;
        rq.url = url;
        rq.file = file;
        rq.maxBytes = kMaxDemo;
        rq.check = looksLikeDemo;
        rq.cancel = [&] {
            if (sh->cancel || sh->quit || cs2Now()) return true;
            if (active + (GetTickCount64() - t0) > kMaxActiveMs) timeUp = true;
            return timeUp;
        };
        rq.progress = [&](int64_t d, int64_t t) {
            done = d;
            total = t;
            ULONGLONG now = GetTickCount64();
            if (now - lastPost >= 400) {
                lastPost = now;
                send(DemoFetchEvent::Progress, d, t);
            }
        };
        send(DemoFetchEvent::Progress, done, total);
        DownloadResult r = sh->http.download(rq);
        active += GetTickCount64() - t0;
        if (r.total >= 0) total = r.total;
        if (r.complete) {
            logLine(L"Demo von Valve: fertig, %lld Bytes in %.0f s: %ls", static_cast<long long>(r.done), active / 1000.0, file.c_str());
            send(DemoFetchEvent::Done, r.done, r.done, L"", file);
            return;
        }
        if (sh->cancel || sh->quit) break;
        if (timeUp) {
            send(DemoFetchEvent::Failed, done, total, L"Nach 30 Minuten angehalten – noch einmal klicken, es geht an der Stelle weiter.");
            return;
        }
        if (r.cancelled) continue;   // CS2 gestartet: Pause, danach an derselben Stelle weiter
        if (r.gone) {
            DeleteFileW(part.c_str());
            logLine(L"Demo von Valve: %d – Valve hat die Demo nicht mehr", r.status);
            send(DemoFetchEvent::Gone, 0, -1, L"Valve hat diese Demo nicht mehr – Valve hebt Demos nur etwa einen Monat auf.");
            return;
        }
        if (!r.problem.empty()) {
            logLine(L"Demo von Valve: %ls", r.problem.c_str());
            send(DemoFetchEvent::Failed, done, total, r.problem);
            return;
        }
        if (r.status == 0 || r.interrupted) {   // Netz: noch zwei Versuche, es geht an der Stelle weiter
            std::wstring why = netErrorText(r.error);
            logLine(L"Demo von Valve: abgebrochen bei %lld Bytes (%ls)", static_cast<long long>(r.done), why.c_str());
            if (++netFails <= 2) {
                if (!waitFor(*sh, 5000)) break;
                continue;
            }
            send(DemoFetchEvent::Failed, done, total,
                 (r.status == 0 ? L"Valve nicht erreichbar (" : L"Verbindung zu Valve abgebrochen (") + why +
                     L") – noch einmal klicken, es geht an der Stelle weiter.");
            return;
        }
        logLine(L"Demo von Valve: Valve antwortet mit %d", r.status);
        send(DemoFetchEvent::Failed, done, total, L"Valve antwortet mit Fehler " + std::to_wstring(r.status) + L" – später noch einmal versuchen.");
        return;
    }
    if (sh->cancel) {   // „Abbrechen“: die angefangene Datei braucht niemand
        DeleteFileW(part.c_str());
        logLine(L"Demo von Valve: abgebrochen");
    }
    send(DemoFetchEvent::Cancelled, 0, -1);
}

}  // namespace

int64_t demoUntil(const PulledInfo& p, const fd::Match& m) {
    if (p.until > 0) return p.until;
    int64_t t = p.matchtime;
    if (t <= 0 && !fd::parseIsoUtc(m.date, t)) return 0;
    return t + kKeepSeconds;
}

struct DemoFetch::Impl {
    App& app;
    std::shared_ptr<Shared> sh;
    std::thread thread;
    DemoFetchState st;
    explicit Impl(App& a) : app(a) {}

    void join() {   // vorigen Download aufräumen (ist schon fertig oder hört gleich auf)
        if (!thread.joinable()) return;
        if (sh && !sh->finished) {
            thread.detach();   // hängt noch in Windows: der Thread hält seine Daten selbst
            return;
        }
        thread.join();
    }
};

DemoFetch::DemoFetch(App& app) : d_(std::make_unique<Impl>(app)) {}

DemoFetch::~DemoFetch() { stop(); }

const DemoFetchState& DemoFetch::state() const { return d_->st; }

bool DemoFetch::busy() const { return d_->st.state == DemoFetchState::Running || d_->st.state == DemoFetchState::Paused; }

void DemoFetch::start(const std::string& checksum) {
    Impl& d = *d_;
    App& app = d.app;
    const fd::Match* m = app.lib.find(checksum);
    const PulledInfo* p = app.lib.pulled(checksum);
    if (!m || !p) return;
    if (busy()) {
        app.notify(d.st.checksum == checksum ? L"Die Demo lädt schon." : L"Es lädt schon eine andere Demo – eine nach der anderen.", true);
        return;
    }
    WebUrl url;
    if (p->url.empty() || !parseDemoUrl(p->url, url)) {
        app.notify(L"Für dieses Match kennt der Server keine Adresse bei Valve, die FragDesk laden kann.", true);
        return;
    }
    int64_t until = demoUntil(*p, *m);
    if (p->gone || (until > 0 && int64_t(time(nullptr)) > until)) {
        app.notify(L"Valve hat diese Demo nicht mehr – Valve hebt Demos nur etwa einen Monat auf.", true);
        return;
    }
    std::wstring file = joinPath(demosDir(), Library::downloadName(p->url, checksum));
    if (fileSize(file) > (1 << 20)) {   // liegt schon da (z. B. FragDesk wurde nach dem Laden beendet): nur verbinden
        app.lib.linkDemo(checksum, file);
        app.demosChanged();
        if (app.matchId == checksum) app.refreshDetail();
        app.notify(L"Demo gefunden – Ansehen und Aufnehmen in CS2 gehen jetzt.");
        return;
    }
    if (!createDirs(demosDir())) {
        app.notify(L"Der Ordner für Demos lässt sich nicht anlegen: " + demosDir(), true);
        return;
    }
    d.join();
    d.sh = std::make_shared<Shared>();
    d.sh->hwnd = app.hwnd;
    d.st = DemoFetchState();
    d.st.state = DemoFetchState::Running;
    d.st.checksum = checksum;
    d.st.done = std::max<int64_t>(0, fileSize(file + L".part"));
    std::shared_ptr<Shared> sh = d.sh;
    d.thread = std::thread([sh, checksum, url, file] {
        worker(sh, checksum, url, file);
        sh->finished = true;
    });
    app.invalidate();
}

void DemoFetch::cancel() {
    Impl& d = *d_;
    if (!d.sh || !busy()) return;
    d.sh->cancel = true;
    d.sh->http.abort();   // bricht ein Warten in WinHTTP sofort ab
    d.sh->cv.notify_all();
}

void DemoFetch::stop() {
    Impl& d = *d_;
    if (!d.sh) return;
    d.sh->hwnd = nullptr;
    d.sh->quit = true;
    d.sh->http.abort();
    d.sh->cv.notify_all();
    if (!d.thread.joinable()) return;
    for (int i = 0; i < 30 && !d.sh->finished; ++i) Sleep(100);   // höchstens 3 s, dann hält der Thread seine Daten selbst
    if (d.sh->finished) d.thread.join();
    else d.thread.detach();
}

LRESULT DemoFetch::onMessage(WPARAM, LPARAM lp) {
    std::unique_ptr<DemoFetchEvent> e(reinterpret_cast<DemoFetchEvent*>(lp));
    if (!e) return 0;
    Impl& d = *d_;
    App& app = d.app;
    if (e->checksum != d.st.checksum) return 0;
    DemoFetchState& st = d.st;
    st.done = e->done;
    if (e->total >= 0) st.total = e->total;
    switch (e->kind) {
        case DemoFetchEvent::Progress: st.state = DemoFetchState::Running; break;
        case DemoFetchEvent::Paused: st.state = DemoFetchState::Paused; break;
        case DemoFetchEvent::Done: {
            st = DemoFetchState();
            app.lib.linkDemo(e->checksum, e->path);
            app.demosChanged();
            if (app.matchId == e->checksum) app.refreshDetail();
            app.lib.startWatching();   // der Ordner ist jetzt sicher da
            app.lib.scanAsync();
            const fd::Match* m = app.lib.find(e->checksum);
            app.notify(L"Demo geladen" + (m ? L": " + matchTitle(*m, app.me) : std::wstring()) + L" – Ansehen und Aufnehmen in CS2 gehen jetzt.");
            break;
        }
        case DemoFetchEvent::Gone:
            app.lib.markDemoGone(e->checksum);
            st.state = DemoFetchState::Failed;
            st.error = e->text;
            app.notify(e->text, true);
            break;
        case DemoFetchEvent::Failed:
            st.state = DemoFetchState::Failed;
            st.error = e->text;
            app.notify(e->text, true);
            break;
        case DemoFetchEvent::Cancelled:
            st = DemoFetchState();
            break;
    }
    if (app.page == Page::Match) app.invalidate();
    return 0;
}

}  // namespace fdw
