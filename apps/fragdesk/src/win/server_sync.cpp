// FragDesk – Handy-App über den eigenen Server (siehe server_sync.h)
#include "server_sync.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "../api/jsonw.h"
#include "../api/mobile.h"
#include "../core/json.h"
#include "app.h"
#include "common.h"
#include "library.h"
#include "mobile_host.h"
#include "net.h"
#include "recorder.h"

namespace fdw {

namespace {

constexpr size_t kChunk = 4u << 20;          // Stück eines Videos (der Server nimmt bis 8 MB)
constexpr int64_t kMaxMatchFile = 4 << 20;   // .json und .heat höchstens 4 MB
constexpr size_t kMaxThumb = 1u << 20;       // Vorschaubild höchstens 1 MB
constexpr ULONGLONG kPausePoll = 15000;      // in der Pause so oft nachsehen, ob CS2 noch läuft
constexpr ULONGLONG kCs2Check = 2000;        // die Prozessliste höchstens alle 2 s ansehen
constexpr int kInviteDays = 7;

bool safeChecksum(const std::string& s) {   // so steht die Prüfsumme auch im Dateinamen und in der Adresse
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char ch : s)
        if (!(std::isalnum(ch) || ch == '-' || ch == '_')) return false;
    return true;
}

bool safeClipId(const std::string& s) {   // clipId: 16 Hex-Zeichen
    if (s.size() != 16) return false;
    for (unsigned char ch : s)
        if (!std::isxdigit(ch)) return false;
    return true;
}

std::wstring heatFile(const std::string& checksum) { return joinPath(matchesDir(), widen(checksum) + L".heat"); }

// ---- Schnappschuss: was der Abgleich braucht, im UI-Thread zusammengestellt ------------------------------------------
struct SyncClip {
    std::string id;   // clipId wie in der Handy-App
    std::wstring path;
    std::string title, match, highlight, map;
    int64_t created = 0;
    double seconds = 0;
};

struct Snapshot {
    uint64_t epoch = 0;
    std::string url, token;
    bool videos = true;
    std::string name, steamId, profile;   // Profil jetzt und zuletzt gemeldet
    std::vector<std::string> matches;     // Prüfsummen, neueste zuerst
    std::vector<SyncClip> clips;          // neueste zuerst
};

// ---- Ergebnisse für den UI-Thread (WM_APP_SYNC, lParam) --------------------------------------------------------------
struct SyncEvent {
    enum Kind { Status, Synced, Profile, Account, Registered, ConnectFailed, Pairing, Invite, ActionFailed } kind = Status;
    uint64_t epoch = 0;   // für welches Konto (ältere werden verworfen)
    SyncStatus status;
    std::string url, token, user, userId, code, link, profile;
    bool admin = false, auth = false;
    int64_t time = 0;     // Synced: Zeitpunkt; Pairing, Invite: gültig bis
    std::wstring text;
    int action = 0;       // ActionFailed: Job::Kind
};

struct Job {
    enum Kind { Register, Logout, Pairing, Invite } kind = Register;
    uint64_t epoch = 0;
    std::string url, token, invite, name, steamId;
};

struct Shared {
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> quit{false}, recording{false};
    std::atomic<bool> videos{true};   // „Videos hochladen“ – ausgeschaltet hört ein laufendes Video nach dem Stück auf
    std::atomic<uint64_t> epoch{1};   // ändert sich mit Modus und Konto: ein laufender Abgleich hört dann auf
    std::mutex mu;                    // schützt die Felder bis „finished“
    std::condition_variable cv;
    std::shared_ptr<const Snapshot> snap;
    bool pending = false, manual = false;
    ULONGLONG due = 0, backoffUntil = 0;
    int failures = 0;
    bool finished = false;
    HttpClient http;   // nur für den Abgleich-Thread; abort() beim Beenden
    // nur im Abgleich-Thread
    std::string authFailed;           // Schlüssel, den der Server nicht mehr kennt (401)
    std::set<std::string> rejected;   // vom Server abgelehnt ("Prüfsumme|Größe"): in dieser Sitzung nicht noch einmal
    std::set<std::string> noThumb;    // Vorschaubild ließ sich nicht erzeugen
    ULONGLONG cs2At = 0;
    bool cs2 = false;
    SyncStatus::State lastLogged = SyncStatus::Running;   // was zuletzt im Protokoll steht (der erste Abgleich kommt immer hinein)
};

void post(Shared& sh, SyncEvent* e) {
    HWND h = sh.hwnd.load();
    if (!h || !PostMessageW(h, WM_APP_SYNC, 0, reinterpret_cast<LPARAM>(e))) delete e;
}

// Pause? Nur der Name aus der Prozessliste (Toolhelp) – CS2 selbst wird nie geöffnet
bool blockedNow(Shared& sh, SyncStatus::State& why) {
    if (sh.recording) {
        why = SyncStatus::PausedRecording;
        return true;
    }
    ULONGLONG now = GetTickCount64();
    if (!sh.cs2At || now - sh.cs2At >= kCs2Check) {
        sh.cs2 = isProcessRunning(L"cs2.exe");
        sh.cs2At = now;
    }
    why = SyncStatus::PausedCs2;
    return sh.cs2;
}

std::wstring serverError(const HttpResponse& r) {
    json::Value v;
    if (json::parse(r.body, v) && v.isObject()) {
        std::string e = v.getString("error");
        if (!e.empty()) return widen(e.substr(0, 400));
    }
    return L"Der Server antwortet mit Fehler " + std::to_wstring(r.status) + L".";
}

int64_t sizeField(const HttpResponse& r) {
    json::Value v;
    if (!json::parse(r.body, v) || !v.isObject()) return -1;
    const json::Value* s = v.find("size");
    return s && s->isNumber() && s->num >= 0 ? int64_t(s->num) : -1;
}

std::string idText(const json::Value& v) {   // Kennung als Text oder Zahl
    if (v.isString()) return v.str;
    if (v.isNumber()) return std::to_string(int64_t(v.num));
    return std::string();
}

// Vorschaubild mit Zeitlimit: Media Foundation kann an einem kaputten Video hängen bleiben (Codec, Netzlaufwerk) – der
// Abgleich wartet höchstens 20 s darauf und startet kein weiteres, solange das alte noch hängt (fertige aus dem
// Zwischenspeicher gehen trotzdem).
std::atomic<int> g_thumbsRunning{0};

enum class Thumb { Ok, Failed, Busy, TimedOut };

Thumb thumbnailWithin(const std::wstring& video, const std::string& clipId, std::string& jpeg) {
    if (videoThumbnail(video, clipId, jpeg, true)) return Thumb::Ok;
    if (g_thumbsRunning > 0) return Thumb::Busy;
    struct Result {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false, ok = false;
        std::string jpeg;
    };
    auto res = std::make_shared<Result>();
    ++g_thumbsRunning;
    std::thread([res, video, clipId] {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        std::string j;
        bool ok = videoThumbnail(video, clipId, j);
        --g_thumbsRunning;
        std::lock_guard<std::mutex> lk(res->mu);
        res->ok = ok;
        res->jpeg = std::move(j);
        res->done = true;
        res->cv.notify_all();
    }).detach();
    std::unique_lock<std::mutex> lk(res->mu);
    if (!res->cv.wait_for(lk, std::chrono::seconds(20), [&] { return res->done; })) {
        logLine(L"Server: Vorschaubild für %ls hängt – Videos gehen erst einmal ohne hoch", video.c_str());
        return Thumb::TimedOut;
    }
    jpeg = std::move(res->jpeg);
    return res->ok ? Thumb::Ok : Thumb::Failed;
}

bool readAt(HANDLE h, int64_t offset, char* buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        OVERLAPPED ov = {};   // Lesen an fester Stelle
        uint64_t at = uint64_t(offset) + done;
        ov.Offset = DWORD(at & 0xFFFFFFFFu);
        ov.OffsetHigh = DWORD(at >> 32);
        DWORD got = 0;
        if (!ReadFile(h, buf + done, DWORD(std::min<size_t>(len - done, 1u << 24)), &got, &ov) || got == 0) return false;
        done += got;
    }
    return true;
}

// ---- Stand auf dem Server ------------------------------------------------------------------------------------------
struct RemoteClip {
    int64_t size = 0;
    bool complete = false, thumb = false;
};

struct Remote {
    std::map<std::string, std::pair<int64_t, int64_t>> matches;   // Prüfsumme -> Bytes (.json, .heat)
    std::map<std::string, RemoteClip> clips;
    int64_t used = -1, limit = 0;
};

bool parseState(const std::string& body, Remote& out) {
    json::Value v;
    if (!json::parse(body, v) || !v.isObject()) return false;
    for (const auto& kv : v["matches"].obj)
        out.matches[kv.first] = {int64_t(kv.second.getNumber("json", -1)), int64_t(kv.second.getNumber("heat", -1))};
    for (const auto& kv : v["clips"].obj) {
        RemoteClip c;
        c.size = int64_t(kv.second.getNumber("size"));
        c.complete = kv.second.getBool("complete");
        c.thumb = kv.second.getBool("thumb");
        out.clips[kv.first] = c;
    }
    const json::Value& q = v["quota"];
    if (q.isObject()) {
        out.used = int64_t(q.getNumber("used", -1));
        out.limit = int64_t(q.getNumber("limit", 0));
    }
    return true;
}

// ---- Ein Durchgang des Abgleichs -------------------------------------------------------------------------------------
enum class Fail { None, Cancel, Paused, Net, Auth, Full, Item };

class Pass {
public:
    Pass(Shared& sh, const Snapshot& s) : sh_(sh), s_(s) {}
    Fail run();

    SyncStatus st;                                      // Stand, auch für die Anzeige danach
    std::wstring text;                                  // Grund bei Net, Full, Auth, Item
    SyncStatus::State pausedBy = SyncStatus::PausedCs2;
    int uploads = 0, deletes = 0;

private:
    struct LocalMatch {
        std::string checksum;
        int64_t json = 0, heat = 0;
        bool jsonOk = false, heatOk = false;
    };
    struct LocalClip {
        const SyncClip* clip;
        int64_t size;
    };

    Shared& sh_;
    const Snapshot& s_;
    ServerUrl url_;
    ULONGLONG posted_ = 0;
    bool changed_ = false;

    bool cancelled() const { return sh_.quit || sh_.epoch != s_.epoch; }
    bool blocked() { return blockedNow(sh_, pausedBy); }
    HttpResponse call(const wchar_t* method, const std::string& path, const std::string& body = std::string(), const char* type = nullptr,
                      bool stoppable = false);
    Fail check(const HttpResponse& r);
    void progress(bool force = false);
    Fail profile();
    Fail removeGone(const Remote& rem);
    Fail uploadMatches(std::vector<LocalMatch>& todo);
    Fail uploadClip(const SyncClip& c, int64_t size, const RemoteClip* e, int index, int count);
    void uploadThumb(const SyncClip& c);
    void account();
};

HttpResponse Pass::call(const wchar_t* method, const std::string& path, const std::string& body, const char* type, bool stoppable) {
    HttpResponse r;
    if (cancelled() || blocked()) {   // vor jeder Anfrage: CS2 gestartet, Aufnahme, Konto gewechselt, Beenden?
        r.cancelled = true;
        return r;
    }
    HttpRequest rq;
    rq.method = method;
    rq.path = path;
    rq.token = s_.token;
    if (type) {
        rq.contentType = type;
        rq.body = body.data();
        rq.bodySize = body.size();
    }
    rq.maxBody = 16u << 20;   // /sync/state mit sehr vielen Matches und Videos
    if (stoppable) rq.cancel = [this] { return cancelled() || blocked(); };
    return sh_.http.send(url_, rq);
}

Fail Pass::check(const HttpResponse& r) {
    if (r.cancelled) return cancelled() ? Fail::Cancel : Fail::Paused;
    if (r.status == 0) {
        text = L"Server nicht erreichbar: " + netErrorText(r.error);
        return Fail::Net;
    }
    if (r.status >= 200 && r.status < 300) return Fail::None;
    if (r.status >= 300 && r.status < 400) {
        text = L"Der Server leitet weiter (" + std::to_wstring(r.status) + L") – stimmt die Adresse?";
        return Fail::Net;
    }
    text = serverError(r);
    if (r.status == 401) return Fail::Auth;
    if (r.status == 507) return Fail::Full;
    if (r.status >= 500 || r.status == 408 || r.status == 429) {
        if (r.status >= 500) text = L"Problem auf dem Server: " + text;
        return Fail::Net;   // später noch einmal
    }
    return Fail::Item;   // diese eine Datei nicht
}

void Pass::progress(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - posted_ < 400) return;
    posted_ = now;
    auto* e = new SyncEvent();
    e->kind = SyncEvent::Status;
    e->epoch = s_.epoch;
    e->status = st;
    post(sh_, e);
}

Fail Pass::run() {
    if (!parseServerUrl(s_.url, url_)) {
        text = L"Die Server-Adresse ist ungültig – bitte neu verbinden.";
        return Fail::Auth;
    }
    st.state = SyncStatus::Running;
    st.current = L"Gleicht ab …";
    progress(true);
    Fail f = profile();
    if (f != Fail::None) return f;

    HttpResponse r = call(L"GET", "api/v1/sync/state");
    f = check(r);
    if (f == Fail::Item) {
        text = L"Unerwartete Antwort vom Server: " + text;
        return Fail::Net;
    }
    if (f != Fail::None) return f;
    Remote rem;
    if (!parseState(r.body, rem)) {
        text = L"Unerwartete Antwort vom Server.";
        return Fail::Net;
    }
    st.used = rem.used;
    st.limit = rem.limit;

    // Was liegt hier, was fehlt dort?
    std::vector<LocalMatch> todo;
    for (const auto& cs : s_.matches) {
        LocalMatch m;
        m.checksum = cs;
        m.json = fileSize(Library::matchFile(cs));
        if (m.json <= 0) continue;   // gerade gelöscht
        m.heat = fileSize(heatFile(cs));
        auto it = rem.matches.find(cs);
        m.jsonOk = it != rem.matches.end() && it->second.first == m.json;
        m.heatOk = m.heat <= 0 || (it != rem.matches.end() && it->second.second == m.heat);
        ++st.matchesTotal;
        if (m.jsonOk) ++st.matchesDone;
        if (!m.jsonOk || !m.heatOk) todo.push_back(m);
    }
    std::vector<LocalClip> clips;
    if (s_.videos)
        for (const auto& c : s_.clips) {
            int64_t size = fileSize(c.path);
            if (size <= 0) continue;   // Video-Datei nicht da (z. B. Laufwerk ab)
            clips.push_back({&c, size});
            ++st.clipsTotal;
            auto it = rem.clips.find(c.id);
            if (it != rem.clips.end() && it->second.complete && it->second.size == size) ++st.clipsDone;
        }
    st.counted = true;
    progress(true);

    f = removeGone(rem);   // zuerst: macht Platz
    if (f != Fail::None) return f;
    f = uploadMatches(todo);
    if (f != Fail::None) return f;
    int count = st.clipsTotal - st.clipsDone, index = 0;
    for (const auto& lc : clips) {
        if (!sh_.videos) break;
        auto it = rem.clips.find(lc.clip->id);
        const RemoteClip* e = it == rem.clips.end() ? nullptr : &it->second;
        if (e && e->complete && e->size == lc.size) {
            if (!e->thumb) uploadThumb(*lc.clip);   // Vorschaubild fehlte beim letzten Mal
            continue;
        }
        f = uploadClip(*lc.clip, lc.size, e, ++index, count);
        if (f != Fail::None) return f;
    }
    if (changed_) account();
    st.state = SyncStatus::Idle;
    st.current.clear();
    return Fail::None;
}

// Spielername oder SteamID geändert: dem Server melden (für später, wenn Freunde sich gegenseitig ansehen)
Fail Pass::profile() {
    std::string now = s_.name + "\n" + s_.steamId;
    if (s_.steamId.empty() || now == s_.profile) return Fail::None;
    fdapi::JsonW j;
    j.obj().kv("name", s_.name).kv("steamId", s_.steamId).end();
    HttpResponse r = call(L"PUT", "api/v1/account/profile", j.str(), "application/json");
    Fail f = check(r);
    if (f == Fail::Item) logLine(L"Server: Profil abgelehnt – %ls", text.c_str());   // gilt trotzdem als gemeldet: nicht bei jedem Abgleich
    else if (f != Fail::None) return f;
    else changed_ = true;   // danach Konto neu holen (Name)
    auto* e = new SyncEvent();
    e->kind = SyncEvent::Profile;
    e->epoch = s_.epoch;
    e->profile = now;
    post(sh_, e);
    return Fail::None;
}

// Was es in FragDesk nicht mehr gibt, auch vom Server löschen. Ein FragDesk ganz ohne Matches und Videos (neuer PC,
// anderer Datenordner) löscht nichts – sonst wäre mit einem Klick alles weg.
Fail Pass::removeGone(const Remote& rem) {
    if (s_.matches.empty() && s_.clips.empty()) {
        if (!rem.matches.empty() || !rem.clips.empty())
            logLine(L"Server: in FragDesk gibt es keine Matches und Videos – auf dem Server wird deshalb nichts gelöscht");
        return Fail::None;
    }
    std::set<std::string> matches(s_.matches.begin(), s_.matches.end()), clips;
    for (const auto& c : s_.clips) clips.insert(c.id);
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<std::string> gone;
        if (kind == 0) {
            for (const auto& kv : rem.matches)
                if (!matches.count(kv.first) && safeChecksum(kv.first)) gone.push_back(kv.first);
        } else {
            for (const auto& kv : rem.clips)
                if (!clips.count(kv.first) && safeClipId(kv.first)) gone.push_back(kv.first);
        }
        for (const auto& id : gone) {
            st.current = L"Löscht, was es in FragDesk nicht mehr gibt …";
            progress();
            HttpResponse r = call(L"DELETE", (kind == 0 ? "api/v1/sync/match/" : "api/v1/sync/clip/") + id);
            Fail f = check(r);
            if (f == Fail::Item) {
                if (r.status != 404) logLine(L"Server: %hs nicht gelöscht – %ls", id.c_str(), text.c_str());   // 404: schon weg
                continue;
            }
            if (f != Fail::None) return f;
            ++deletes;
            changed_ = true;
        }
    }
    return Fail::None;
}

Fail Pass::uploadMatches(std::vector<LocalMatch>& todo) {
    int index = 0;
    for (auto& m : todo) {
        st.current = L"Match " + std::to_wstring(++index) + L" von " + std::to_wstring(todo.size());
        progress();
        for (int part = 0; part < 2; ++part) {   // erst die Auswertung, dann die Heatmap
            bool json = part == 0;
            if (json ? m.jsonOk : (m.heatOk || !m.jsonOk)) continue;
            int64_t size = json ? m.json : m.heat;
            std::string key = m.checksum + (json ? ".json|" : ".heat|") + std::to_string(size);
            if (sh_.rejected.count(key)) continue;
            if (size > kMaxMatchFile) {
                logLine(L"Server: %hs ist zu groß (%lld Bytes) – bleibt nur in FragDesk", key.c_str(), static_cast<long long>(size));
                sh_.rejected.insert(key);
                continue;
            }
            std::string body;
            if (!readFile(json ? Library::matchFile(m.checksum) : heatFile(m.checksum), body, size_t(kMaxMatchFile)) || body.empty())
                continue;   // gerade gelöscht oder neu ausgewertet: beim nächsten Mal
            HttpResponse r = call(L"PUT", "api/v1/sync/match/" + m.checksum + (json ? "/json" : "/heat"), body,
                                  json ? "application/json" : "application/octet-stream");
            Fail f = check(r);
            if (f == Fail::Item) {
                logLine(L"Server: %hs abgelehnt – %ls", key.c_str(), text.c_str());
                sh_.rejected.insert(key);
                continue;
            }
            if (f != Fail::None) return f;
            (json ? m.jsonOk : m.heatOk) = true;
            if (json) ++st.matchesDone;
            ++uploads;
            changed_ = true;
        }
        progress();
    }
    return Fail::None;
}

// Ein Video in Stücken à 4 MB; ein unterbrochenes geht an der Stelle weiter, die der Server kennt
Fail Pass::uploadClip(const SyncClip& c, int64_t size, const RemoteClip* e, int index, int count) {
    std::string key = c.id + "|" + std::to_string(size);
    if (sh_.rejected.count(key)) return Fail::None;
    int64_t offset = 0;
    if (e && (e->complete || e->size > size)) {   // ein anderes Video unter derselben ID (neu aufgenommen): neu anfangen
        HttpResponse r = call(L"DELETE", "api/v1/sync/clip/" + c.id);
        Fail f = check(r);
        if (f != Fail::None && !(f == Fail::Item && r.status == 404)) return f == Fail::Item ? Fail::None : f;
    } else if (e) {
        offset = e->size;
        if (offset > 0) logLine(L"Server: Video %ls geht bei %lld von %lld Bytes weiter", c.path.c_str(), static_cast<long long>(offset),
                                static_cast<long long>(size));
    }
    HANDLE h = CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Fail::None;   // gerade nicht lesbar: beim nächsten Mal
    std::string buf;
    int conflicts = 0;
    Fail result = Fail::None;
    std::wstring which = L"Video " + std::to_wstring(index) + L" von " + std::to_wstring(count);
    while (offset < size) {
        if (!sh_.videos) break;   // „Videos hochladen“ gerade ausgeschaltet: der Rest bleibt für später
        int pct = int(offset * 100 / size);
        st.current = which + L" · " + std::to_wstring(pct) + L" %";
        progress();
        size_t n = size_t(std::min<int64_t>(int64_t(kChunk), size - offset));
        buf.resize(n);
        if (!readAt(h, offset, &buf[0], n)) {
            logLine(L"Server: %ls lässt sich nicht lesen", c.path.c_str());
            result = Fail::Item;
            break;
        }
        char q[80];
        snprintf(q, sizeof q, "/data?offset=%lld&total=%lld", static_cast<long long>(offset), static_cast<long long>(size));
        HttpResponse r = call(L"PUT", "api/v1/sync/clip/" + c.id + q, buf, "application/octet-stream", true);
        if (r.status == 409) {   // der Server hat einen anderen Stand: dort weitermachen
            int64_t at = sizeField(r);
            if (at < 0 || at > size || ++conflicts > 5) {
                logLine(L"Server: Video %ls – Stand passt nicht (%lld), später neu", c.path.c_str(), static_cast<long long>(at));
                result = Fail::Item;
                break;
            }
            logLine(L"Server: Video %ls – der Server hat %lld Bytes, weiter ab dort", c.path.c_str(), static_cast<long long>(at));
            offset = at;
            continue;
        }
        result = check(r);
        if (result != Fail::None) break;
        int64_t next = sizeField(r);
        if (next <= offset || next > size) {
            logLine(L"Server: Video %ls – unerwartete Größe %lld nach dem Hochladen", c.path.c_str(), static_cast<long long>(next));
            result = Fail::Item;
            break;
        }
        offset = next;
        changed_ = true;
    }
    CloseHandle(h);
    buf.clear();
    buf.shrink_to_fit();
    if (result == Fail::Item) {
        sh_.rejected.insert(key);
        return Fail::None;   // die anderen Videos trotzdem
    }
    if (result != Fail::None) return result;
    if (offset < size) return Fail::None;   // abgebrochen, weil „Videos hochladen“ aus ist
    st.current = which + L" · 100 %";
    progress();
    uploadThumb(c);
    fdapi::JsonW j;
    j.obj()
        .kv("title", c.title)
        .kv("match", c.match)
        .kv("highlight", c.highlight)
        .kv("map", c.map)
        .ki("created", c.created)
        .kn("seconds", c.seconds)
        .ki("total", size)
        .end();
    HttpResponse r = call(L"POST", "api/v1/sync/clip/" + c.id + "/done", j.str(), "application/json");
    Fail f = check(r);
    if (f == Fail::Item) {
        logLine(L"Server: Video %ls nicht übernommen – %ls", c.path.c_str(), text.c_str());
        sh_.rejected.insert(key);
        return Fail::None;
    }
    if (f != Fail::None) return f;
    ++st.clipsDone;
    ++uploads;
    changed_ = true;
    progress(true);
    return Fail::None;
}

// Vorschaubild wie in der Handy-App direkt vom PC; fehlt es, ist das kein Fehler (nächstes Mal wieder)
void Pass::uploadThumb(const SyncClip& c) {
    if (sh_.noThumb.count(c.id) || cancelled() || blocked()) return;   // während CS2 läuft auch kein Video dekodieren
    std::string jpeg;
    Thumb t = thumbnailWithin(c.path, c.id, jpeg);
    if (t != Thumb::Ok || jpeg.empty() || jpeg.size() > kMaxThumb) {
        if (t != Thumb::Busy) sh_.noThumb.insert(c.id);   // in dieser Sitzung nicht wieder (hängt gerade ein anderes: später)
        return;
    }
    HttpResponse r = call(L"PUT", "api/v1/sync/clip/" + c.id + "/thumb", jpeg, "image/jpeg");
    if (r.status < 200 || r.status >= 300) logLine(L"Server: Vorschaubild für %ls nicht hochgeladen (%d)", c.path.c_str(), r.status);
}

// Nach Änderungen: Speicher und Konto frisch vom Server (z. B. Admin geworden)
void Pass::account() {
    HttpResponse r = call(L"GET", "api/v1/account");
    json::Value v;
    if (check(r) != Fail::None || !json::parse(r.body, v) || !v.isObject()) return;
    const json::Value& q = v["quota"];
    if (q.isObject()) {
        st.used = int64_t(q.getNumber("used", double(st.used)));
        st.limit = int64_t(q.getNumber("limit", double(st.limit)));
    }
    const json::Value& u = v["user"];
    if (!u.isObject()) return;
    auto* e = new SyncEvent();
    e->kind = SyncEvent::Account;
    e->epoch = s_.epoch;
    e->user = u.getString("name");
    e->userId = idText(u["id"]);
    e->admin = u.getBool("admin");
    post(sh_, e);
}

// ---- Abgleich-Thread -------------------------------------------------------------------------------------------------
int backoffSeconds(int failures) {
    static const int steps[] = {30, 60, 120, 300, 600, 900};   // höchstens alle 15 Minuten
    return steps[std::min(std::max(failures, 1), 6) - 1];
}

void postStatus(Shared& sh, uint64_t epoch, const SyncStatus& st) {
    auto* e = new SyncEvent();
    e->kind = SyncEvent::Status;
    e->epoch = epoch;
    e->status = st;
    post(sh, e);
}

// Nächsten Durchgang einplanen (Wartezeit nach Fehlern bzw. Pause)
void schedule(Shared& sh, ULONGLONG delayMs, bool backoff) {
    std::lock_guard<std::mutex> lk(sh.mu);
    ULONGLONG at = GetTickCount64() + delayMs;
    if (backoff) {
        sh.backoffUntil = at;
        if (sh.pending && !sh.manual) sh.due = std::max(sh.due, at);   // neue Anstöße warten mit
        else if (!sh.pending) sh.due = at;
    } else if (!sh.pending || at < sh.due) sh.due = at;
    sh.pending = true;
}

void runOnce(Shared& sh, const Snapshot& s) {
    if (s.epoch != sh.epoch) return;
    if (!s.token.empty() && s.token == sh.authFailed) {   // der Server kennt den Schlüssel nicht mehr: erst neu verbinden
        SyncStatus st;
        st.state = SyncStatus::AuthFailed;
        postStatus(sh, s.epoch, st);
        return;
    }
    SyncStatus::State why;
    if (blockedNow(sh, why)) {   // gar nicht erst anfangen
        SyncStatus st;
        st.state = why;
        postStatus(sh, s.epoch, st);
        if (sh.lastLogged != why) logLine(why == SyncStatus::PausedCs2 ? L"Server: Pause, CS2 läuft" : L"Server: Pause, FragDesk nimmt auf");
        sh.lastLogged = why;
        schedule(sh, kPausePoll, false);
        return;
    }
    Pass pass(sh, s);
    Fail f = pass.run();
    SyncStatus st = pass.st;
    st.current.clear();
    switch (f) {
        case Fail::None: {
            {
                std::lock_guard<std::mutex> lk(sh.mu);
                sh.failures = 0;
                sh.backoffUntil = 0;
            }
            st.state = SyncStatus::Idle;
            if (pass.uploads || pass.deletes || sh.lastLogged != SyncStatus::Idle)
                logLine(L"Server: abgeglichen – %d von %d Matches, %d von %d Videos (%d hochgeladen, %d gelöscht)", st.matchesDone, st.matchesTotal,
                        st.clipsDone, st.clipsTotal, pass.uploads, pass.deletes);
            sh.lastLogged = SyncStatus::Idle;
            auto* e = new SyncEvent();
            e->kind = SyncEvent::Synced;
            e->epoch = s.epoch;
            e->time = fdapi::nowEpoch();
            post(sh, e);
            break;
        }
        case Fail::Paused:
            st.state = pass.pausedBy;
            if (sh.lastLogged != st.state)
                logLine(st.state == SyncStatus::PausedCs2 ? L"Server: Pause, CS2 läuft" : L"Server: Pause, FragDesk nimmt auf");
            sh.lastLogged = st.state;
            schedule(sh, kPausePoll, false);
            break;
        case Fail::Net: {
            int failures;
            {
                std::lock_guard<std::mutex> lk(sh.mu);
                failures = ++sh.failures;
            }
            int wait = backoffSeconds(failures);
            st.state = SyncStatus::Offline;
            st.error = pass.text;
            st.retryAt = fdapi::nowEpoch() + wait;
            logLine(L"Server: Abgleich ging nicht (%ls) – neuer Versuch in %d s", pass.text.c_str(), wait);
            sh.lastLogged = st.state;
            schedule(sh, ULONGLONG(wait) * 1000, true);
            break;
        }
        case Fail::Full:
            st.state = SyncStatus::Full;
            st.error = pass.text;
            if (sh.lastLogged != st.state) logLine(L"Server: Speicher voll – %ls", pass.text.c_str());
            sh.lastLogged = st.state;
            break;   // nächster Versuch mit dem nächsten Anstoß (spätestens nach 15 Minuten)
        case Fail::Auth:
            sh.authFailed = s.token;
            st.state = SyncStatus::AuthFailed;
            st.error = pass.text;
            logLine(L"Server: Anmeldung abgelehnt – %ls", pass.text.c_str());
            sh.lastLogged = st.state;
            break;
        case Fail::Cancel:
        case Fail::Item: return;   // Konto gewechselt oder FragDesk wird beendet
    }
    postStatus(sh, s.epoch, st);
}

void worker(std::shared_ptr<Shared> sh) {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // Prozessor und Festplatte: Hintergrund
    std::unique_lock<std::mutex> lk(sh->mu);
    while (!sh->quit) {
        if (!sh->pending || !sh->snap) {
            sh->cv.wait(lk);
            continue;
        }
        ULONGLONG now = GetTickCount64();
        if (now < sh->due) {
            sh->cv.wait_for(lk, std::chrono::milliseconds(sh->due - now));
            continue;
        }
        std::shared_ptr<const Snapshot> snap = sh->snap;
        sh->pending = false;
        sh->manual = false;
        lk.unlock();
        runOnce(*sh, *snap);
        lk.lock();
    }
    sh->finished = true;
    sh->cv.notify_all();
}

// ---- Aktionen (eigene kurze Threads) ---------------------------------------------------------------------------------
void runJob(std::shared_ptr<Shared> sh, Job job) {
    std::thread([sh, job] {
        auto fail = [&](const std::wstring& text, bool auth = false) {
            auto* e = new SyncEvent();
            e->kind = job.kind == Job::Register ? SyncEvent::ConnectFailed : SyncEvent::ActionFailed;
            e->action = job.kind;
            e->epoch = job.epoch;
            e->text = text;
            e->auth = auth;
            post(*sh, e);
        };
        ServerUrl url;
        if (!parseServerUrl(job.url, url)) return fail(L"Die Server-Adresse ist ungültig.");
        fdapi::JsonW j;
        HttpRequest rq;
        rq.method = L"POST";
        rq.token = job.token;
        switch (job.kind) {
            case Job::Register:
                rq.path = "api/v1/account/register";
                rq.token.clear();
                j.obj().kv("invite", job.invite).kv("name", job.name).kv("steamId", job.steamId).kv("client", std::string("FragDesk ") + kVersionA).end();
                break;
            case Job::Logout: rq.path = "api/v1/account/logout"; break;
            case Job::Pairing: rq.path = "api/v1/sync/pairing"; break;
            case Job::Invite:
                rq.path = "api/v1/admin/invite";
                j.obj().kb("admin", false).ki("days", kInviteDays).end();
                break;
        }
        std::string body = j.str().empty() ? std::string("{}") : j.take();
        rq.contentType = "application/json";
        rq.body = body.data();
        rq.bodySize = body.size();
        HttpClient http;
        HttpResponse r = http.send(url, rq);
        if (job.kind == Job::Logout) {
            if (r.status >= 200 && r.status < 300) logLine(L"Server: abgemeldet von %ls", url.label().c_str());
            else logLine(L"Server: Abmelden bei %ls ging nicht (%ls)", url.label().c_str(), r.status ? serverError(r).c_str() : netErrorText(r.error).c_str());
            return;
        }
        if (r.status == 0) return fail(L"Server nicht erreichbar: " + netErrorText(r.error));
        if (r.status < 200 || r.status >= 300) return fail(serverError(r), r.status == 401 && job.kind != Job::Register);
        json::Value v;
        if (!json::parse(r.body, v) || !v.isObject()) return fail(L"Unerwartete Antwort vom Server.");
        auto* e = new SyncEvent();
        e->epoch = job.epoch;
        switch (job.kind) {
            case Job::Register: {
                const json::Value& u = v["user"];
                e->kind = SyncEvent::Registered;
                e->url = url.text();
                e->token = v.getString("token");
                e->user = u.getString("name", job.name);
                e->userId = idText(u["id"]);
                e->admin = u.getBool("admin");
                e->profile = job.name + "\n" + job.steamId;
                if (!validToken(e->token)) {
                    delete e;
                    return fail(L"Unerwartete Antwort vom Server (kein gültiger Schlüssel).");
                }
                break;
            }
            case Job::Pairing: {
                e->kind = SyncEvent::Pairing;
                e->code = v.getString("code");
                e->time = int64_t(v.getNumber("until"));
                ServerUrl app;   // Adresse der Handy-App; ohne gültige die des Servers
                e->url = parseServerUrl(v.getString("url"), app) ? app.text() : url.text();
                bool digits = e->code.size() >= 4 && e->code.size() <= 12 &&
                              std::all_of(e->code.begin(), e->code.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
                if (!digits) {
                    delete e;
                    return fail(L"Unerwartete Antwort vom Server (kein Code).");
                }
                if (e->time <= fdapi::nowEpoch()) e->time = fdapi::nowEpoch() + 600;
                break;
            }
            case Job::Invite: {
                e->kind = SyncEvent::Invite;
                e->link = v.getString("link");
                e->time = int64_t(v.getNumber("expires"));
                std::string server, code;
                if (!parseInvite(widen(e->link), server, code)) {
                    delete e;
                    return fail(L"Unerwartete Antwort vom Server (kein Einladungslink).");
                }
                break;
            }
            case Job::Logout: break;
        }
        post(*sh, e);
    }).detach();
}

std::wstring trimmed(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool parseInvite(const std::wstring& text, std::string& server, std::string& code) {
    std::wstring t = trimmed(text);
    if (t.size() > 400) return false;
    size_t h = t.find(L"#invite=");
    if (h == std::wstring::npos) return false;
    std::wstring c = t.substr(h + 8);
    size_t amp = c.find(L'&');
    if (amp != std::wstring::npos) c.resize(amp);
    if (c.size() < 4 || c.size() > 128) return false;
    for (wchar_t ch : c)
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_')) return false;
    ServerUrl u;
    if (!parseServerUrl(narrow(t.substr(0, h)), u)) return false;
    server = u.text();
    code = narrow(c);
    return true;
}

// ---- ServerSync -----------------------------------------------------------------------------------------------------
struct ServerSync::Impl {
    App& app;
    std::shared_ptr<Shared> sh = std::make_shared<Shared>();
    std::thread thread;
    std::string key;   // Konto, für das gerade abgeglichen wird (leer = aus)
    SyncStatus status;
    bool connecting = false, pairingBusy = false, inviteBusy = false;
    std::string pairCode, pairUrl;
    int64_t pairUntil = 0;
    explicit Impl(App& a) : app(a) {}

    void ensureThread() {
        if (thread.joinable() || sh->quit) return;
        std::shared_ptr<Shared> s = sh;
        thread = std::thread([s] { worker(s); });
    }
    Job job(Job::Kind kind) const {
        Job j;
        j.kind = kind;
        j.epoch = sh->epoch;
        j.url = app.settings.mobile.srv.url;
        j.token = app.settings.mobile.srv.token;
        return j;
    }
};

ServerSync::ServerSync(App& app) : d_(std::make_unique<Impl>(app)) {}

ServerSync::~ServerSync() { stop(); }

bool ServerSync::active() const { return !d_->key.empty(); }

void ServerSync::apply() {
    Impl& d = *d_;
    const MobileSettings& ms = d.app.settings.mobile;
    ServerUrl url;
    bool on = ms.server && !d.app.renderMode && validToken(ms.srv.token) && parseServerUrl(ms.srv.url, url);
    std::string key = on ? ms.srv.url + "|" + ms.srv.token : std::string();
    d.sh->hwnd = d.app.hwnd;
    if (key != d.key) {
        d.key = key;
        ++d.sh->epoch;   // ein laufender Abgleich für das alte Konto hört auf
        {
            std::lock_guard<std::mutex> lk(d.sh->mu);
            d.sh->pending = false;
            d.sh->snap.reset();
            d.sh->failures = 0;
            d.sh->backoffUntil = 0;
        }
        d.status = SyncStatus();
        d.status.used = ms.srv.used;   // bis der Server antwortet: Speicher vom letzten Mal
        d.status.limit = ms.srv.limit;
        d.pairCode.clear();
        d.pairingBusy = d.inviteBusy = false;
        if (on) request(2000);
    }
    if (d.app.hwnd) {
        if (on) SetTimer(d.app.hwnd, kSyncTimer, 15 * 60 * 1000, nullptr);
        else KillTimer(d.app.hwnd, kSyncTimer);
    }
}

void ServerSync::request(int delayMs, bool manual) {
    Impl& d = *d_;
    if (!active() || !d.app.lib.loaded() || d.sh->quit) return;
    const ServerSettings& srv = d.app.settings.mobile.srv;
    auto snap = std::make_shared<Snapshot>();
    snap->epoch = d.sh->epoch;
    snap->url = srv.url;
    snap->token = srv.token;
    snap->videos = srv.videos;
    snap->name = d.app.meName.empty() ? std::string("Spieler") : narrow(d.app.meName);
    snap->steamId = d.app.me;
    snap->profile = srv.profile;
    for (const auto& m : d.app.lib.matches)
        if (safeChecksum(m->checksum)) snap->matches.push_back(m->checksum);
    for (const Clip& c : d.app.clips) {
        SyncClip sc;
        sc.path = c.path;
        sc.id = fdapi::clipId(narrow(c.path));
        sc.title = narrow(c.title);
        sc.match = c.match;
        sc.highlight = c.highlight;
        sc.map = c.map;
        sc.created = c.created;
        sc.seconds = c.seconds;
        snap->clips.push_back(std::move(sc));
    }
    d.sh->videos = srv.videos;
    {
        std::lock_guard<std::mutex> lk(d.sh->mu);
        d.sh->snap = snap;
        ULONGLONG at = GetTickCount64() + ULONGLONG(std::max(0, delayMs));
        if (manual) {   // „Jetzt abgleichen“: auch während der Wartezeit nach Fehlern
            d.sh->failures = 0;
            d.sh->backoffUntil = 0;
            d.sh->manual = true;
            at = GetTickCount64();
        }
        at = std::max(at, d.sh->backoffUntil);
        if (!d.sh->pending || at < d.sh->due) d.sh->due = at;
        d.sh->pending = true;
    }
    d.sh->cv.notify_all();
    d.ensureThread();
}

void ServerSync::setRecording(bool on) {
    if (d_->sh->recording.exchange(on) == on) return;
    if (!on) {   // weiter, sobald die Aufnahme fertig ist
        std::lock_guard<std::mutex> lk(d_->sh->mu);
        if (d_->sh->pending) d_->sh->due = std::min(d_->sh->due, std::max(GetTickCount64() + 3000, d_->sh->backoffUntil));
    }
    d_->sh->cv.notify_all();
}

void ServerSync::stop() {
    Impl& d = *d_;
    std::shared_ptr<Shared> sh = d.sh;
    sh->hwnd = nullptr;
    sh->quit = true;
    sh->http.abort();
    {
        std::lock_guard<std::mutex> lk(sh->mu);
    }
    sh->cv.notify_all();
    if (!d.thread.joinable()) return;
    bool done;
    {
        std::unique_lock<std::mutex> lk(sh->mu);
        done = sh->cv.wait_for(lk, std::chrono::seconds(3), [&] { return sh->finished; });
    }
    if (done) d.thread.join();
    else d.thread.detach();   // hängt noch in Windows: der Thread hält seine Daten selbst, der Prozess endet ohnehin
}

void ServerSync::connectFromClipboard() {
    Impl& d = *d_;
    if (d.connecting) return;
    std::string url, code;
    if (!parseInvite(clipboardText(d.app.hwnd), url, code)) {
        d.app.notify(L"In der Zwischenablage steht keine Einladung. Kopiere den Link (https://…/#invite=…) und klicke noch einmal.", true);
        return;
    }
    Job j = d.job(Job::Register);
    j.url = url;
    j.token.clear();
    j.invite = code;
    j.name = d.app.meName.empty() ? std::string("Spieler") : narrow(d.app.meName);
    j.steamId = d.app.me;
    d.connecting = true;
    ServerUrl u;
    parseServerUrl(url, u);
    logLine(L"Server: verbinde mit %ls …", u.label().c_str());
    runJob(d.sh, j);
    d.app.invalidate();
}

void ServerSync::disconnect() {
    Impl& d = *d_;
    ServerSettings& srv = d.app.settings.mobile.srv;
    if (srv.token.empty() && srv.url.empty()) return;
    if (MessageBoxW(d.app.hwnd,
                    L"Vom Server trennen?\n\nDeine Daten auf dem Server bleiben erhalten. Um wieder zu verbinden, brauchst du eine neue Einladung.",
                    L"FragDesk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    std::wstring label = serverLabel();
    if (validToken(srv.token)) runJob(d.sh, d.job(Job::Logout));   // Schlüssel auf dem Server ungültig machen
    bool videos = srv.videos;
    srv = ServerSettings();
    srv.videos = videos;
    d.app.saveSettings();
    apply();
    logLine(L"Server: getrennt von %ls", label.c_str());
    d.app.notify(L"Vom Server getrennt: " + label);
}

void ServerSync::startPairing() {
    Impl& d = *d_;
    if (!active() || d.pairingBusy) return;
    d.pairingBusy = true;
    runJob(d.sh, d.job(Job::Pairing));
    d.app.invalidate();
}

void ServerSync::stopPairing() {
    d_->pairCode.clear();
    d_->pairUntil = 0;
}

void ServerSync::invite() {
    Impl& d = *d_;
    if (!active() || !d.app.settings.mobile.srv.admin || d.inviteBusy) return;
    d.inviteBusy = true;
    runJob(d.sh, d.job(Job::Invite));
    d.app.invalidate();
}

const SyncStatus& ServerSync::status() const { return d_->status; }
bool ServerSync::connecting() const { return d_->connecting; }
bool ServerSync::pairingBusy() const { return d_->pairingBusy; }
bool ServerSync::inviteBusy() const { return d_->inviteBusy; }

std::string ServerSync::pairingCode() const { return fdapi::nowEpoch() <= d_->pairUntil ? d_->pairCode : std::string(); }
int64_t ServerSync::pairingUntil() const { return d_->pairCode.empty() ? 0 : d_->pairUntil; }
std::string ServerSync::pairingBase() const { return d_->pairUrl; }

std::string ServerSync::pairingUrl() const {
    std::string code = pairingCode();
    if (code.empty()) return std::string();
    return d_->pairUrl + (d_->pairUrl.find('?') == std::string::npos ? "?pair=" : "&pair=") + code;
}

std::wstring ServerSync::serverLabel() const {
    ServerUrl u;
    return parseServerUrl(d_->app.settings.mobile.srv.url, u) ? u.label() : widen(d_->app.settings.mobile.srv.url);
}

LRESULT ServerSync::onMessage(WPARAM, LPARAM lp) {
    std::unique_ptr<SyncEvent> e(reinterpret_cast<SyncEvent*>(lp));
    if (!e) return 0;
    Impl& d = *d_;
    App& app = d.app;
    ServerSettings& srv = app.settings.mobile.srv;
    bool current = e->epoch == d.sh->epoch;
    switch (e->kind) {
        case SyncEvent::Status: {
            if (!current) return 0;
            SyncStatus next = e->status;
            if (next.used < 0 && d.status.used >= 0) {   // Durchgang ohne Antwort vom Server: letzter bekannter Speicher
                next.used = d.status.used;
                next.limit = d.status.limit;
            }
            if (!next.counted && d.status.counted) {   // … und letzte bekannte Zahlen
                next.counted = true;
                next.matchesDone = d.status.matchesDone;
                next.matchesTotal = d.status.matchesTotal;
                next.clipsDone = d.status.clipsDone;
                next.clipsTotal = d.status.clipsTotal;
            }
            d.status = next;
            if (next.used >= 0) {   // wird mit dem nächsten Speichern gemerkt
                srv.used = next.used;
                srv.limit = next.limit;
            }
            if (app.page == Page::Settings) app.invalidate();   // andere Seiten zeigen den Stand nicht
            return 0;
        }
        case SyncEvent::Synced:
            if (current) {
                srv.lastSync = e->time;
                app.saveSettings();
            }
            break;
        case SyncEvent::Profile:
            if (current) {
                srv.profile = e->profile;
                app.saveSettings();
            }
            break;
        case SyncEvent::Account:
            if (current && (srv.user != e->user || srv.admin != e->admin || (!e->userId.empty() && srv.userId != e->userId))) {
                if (!e->user.empty()) srv.user = e->user;
                if (!e->userId.empty()) srv.userId = e->userId;
                srv.admin = e->admin;
                app.saveSettings();
            }
            break;
        case SyncEvent::Registered: {
            // Die Einladung ist jetzt verbraucht: das Konto immer übernehmen
            d.connecting = false;
            bool videos = srv.videos;
            srv = ServerSettings();
            srv.videos = videos;
            srv.url = e->url;
            srv.token = e->token;
            srv.user = e->user;
            srv.userId = e->userId;
            srv.admin = e->admin;
            srv.profile = e->profile;
            app.settings.mobile.server = true;
            app.mobile->stopPairing();
            app.saveSettings();
            app.mobile->apply();   // falls bisher direkt vom PC: nicht mehr lauschen
            d.key.clear();         // auch beim selben Server neu anfangen
            apply();
            std::wstring label = serverLabel();
            logLine(L"Server: verbunden mit %ls als %ls%ls", label.c_str(), widen(e->user).c_str(), e->admin ? L" (Admin)" : L"");
            app.notify(L"Verbunden mit " + label + L" als " + widen(e->user));
            break;
        }
        case SyncEvent::ConnectFailed:
            d.connecting = false;
            logLine(L"Server: Verbinden ging nicht – %ls", e->text.c_str());
            app.notify(L"Verbinden ging nicht: " + e->text, true);
            break;
        case SyncEvent::Pairing:
            d.pairingBusy = false;
            if (current) {
                d.pairCode = e->code;
                d.pairUntil = e->time;
                d.pairUrl = e->url;
                if (app.hwnd) SetTimer(app.hwnd, kMobileTimer, 1000, nullptr);
            }
            break;
        case SyncEvent::Invite:
            d.inviteBusy = false;
            if (current) {
                std::wstring link = widen(e->link);
                std::wstring until = e->time > 0 ? L"gilt bis " + dateText(e->time) : L"gilt " + std::to_wstring(kInviteDays) + L" Tage";
                if (setClipboardText(app.hwnd, link))
                    app.notify(L"Einladungslink kopiert (" + until + L", nur einmal) – schick ihn deinem Freund per Messenger.");
                else app.notify(L"Einladungslink ließ sich nicht kopieren: " + link, true);
                logLine(L"Server: Einladung erstellt (%ls)", until.c_str());
            }
            break;
        case SyncEvent::ActionFailed:
            if (e->action == Job::Pairing) d.pairingBusy = false;
            if (e->action == Job::Invite) d.inviteBusy = false;
            if (!current) break;
            if (e->auth) {
                d.status = SyncStatus();
                d.status.state = SyncStatus::AuthFailed;
            }
            app.notify(e->text, true);
            break;
    }
    app.invalidate();
    return 0;
}

}  // namespace fdw
