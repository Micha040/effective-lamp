// fragdesk-server – alle Anfragen (siehe app.h)
#include "app.h"

#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <iostream>

#include "../src/api/jsonw.h"
#include "../src/api/views.h"
#include "../src/core/json.h"
#include "../src/core/model.h"
#include "../src/core/replay.h"
#include "files.h"
#include "web_assets.h"

namespace fdsrv {

using fdapi::JsonW;
using fdapi::nowEpoch;
using fdapi::Request;
using fdapi::Response;

namespace {

struct Busy {
    std::atomic<int>& n;
    explicit Busy(std::atomic<int>& c) : n(c) { ++n; }
    ~Busy() { --n; }
};

const char* kCookie = "fdk";
const int64_t kMaxJson = 64 * 1024;
const char* kNoAccount = "Nicht angemeldet: Dieser Schlüssel gilt auf dem Server nicht (mehr). Verbinde FragDesk neu mit dem Server.";
const char* kNoDevice = "Dieses Handy ist nicht (mehr) mit deinem FragDesk-Server verbunden.";

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool startsWith(const std::string& s, const char* head) { return s.compare(0, std::char_traits<char>::length(head), head) == 0; }

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool sameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

bool loopback(const std::string& ip) { return ip == "127.0.0.1"; }

// Hinter Caddy kommt alles von 127.0.0.1; die echte Adresse steht dann als letzter Eintrag in X-Forwarded-For
// (den setzt Caddy selbst). Von außen gesetzte Köpfe zählen nur, wenn die Verbindung von 127.0.0.1 kommt.
std::string clientIp(const Request& req) {
    std::string xff = req.header("x-forwarded-for");
    if (!loopback(req.remote) || xff.empty()) return req.remote;
    size_t comma = xff.rfind(',');
    std::string ip = trim(comma == std::string::npos ? xff : xff.substr(comma + 1));
    return ip.empty() ? req.remote : ip.substr(0, 64);
}

std::string bearer(const Request& req) {
    std::string a = req.header("authorization");
    if (a.size() > 7 && (a.compare(0, 7, "Bearer ") == 0 || a.compare(0, 7, "bearer ") == 0)) return trim(a.substr(7));
    return std::string();
}

std::string deviceToken(const Request& req) {   // wie in mobile.cpp: Cookie, sonst Bearer
    std::string t = req.cookie(kCookie);
    return t.empty() ? bearer(req) : t;
}

void fail(Response& res, const Fail& f) {
    if (f.size < 0) return res.error(f.status, f.error);
    JsonW j;
    j.obj().kv("error", f.error).ki("size", f.size).end();
    res.status = f.status;
    res.type = "application/json; charset=utf-8";
    res.body = j.take();
}

void ok(Response& res) { res.body = "{\"ok\":true}"; }

bool parseBody(const Request& req, Response& res, json::Value& v) {
    if (int64_t(req.body.size()) > kMaxJson) {
        res.error(413, "Die Anfrage ist zu groß.");
        return false;
    }
    if (!json::parse(req.body.empty() ? std::string("{}") : req.body, v) || !v.isObject()) {
        res.error(400, "Ungültige Anfrage (kein JSON-Objekt).");
        return false;
    }
    return true;
}

bool parseInt(const std::string& s, int64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    int64_t v = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
    }
    out = v;
    return true;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t slash = s.find('/', pos);
        if (slash == std::string::npos) slash = s.size();
        out.push_back(s.substr(pos, slash - pos));
        pos = slash + 1;
    }
    return out;
}

void writeUser(JsonW& j, const UserInfo& i) {
    j.obj().kv("id", i.id).kv("name", i.name).kb("admin", i.admin).kv("steamId", i.steamId).ki("created", i.created).end();
}

void writeQuota(JsonW& j, const UserInfo& i) { j.obj().ki("used", i.used).ki("limit", i.limit).end(); }

void writeEntry(JsonW& j, const UserInfo& i) {   // Zeile der Kontenliste (Admin)
    j.obj()
        .kv("id", i.id)
        .kv("name", i.name)
        .kb("admin", i.admin)
        .ki("used", i.used)
        .ki("limit", i.limit)
        .ki("matches", i.matches)
        .ki("clips", i.clips)
        .ki("created", i.created)
        .ki("lastSeen", i.lastSeen)
        .ki("devices", int64_t(i.devices.size()))
        .kb("autodl", i.autodl)   // Demos automatisch laden an (ohne Codes)
        .end();
}

void writeDevices(JsonW& j, const UserInfo& i) {
    j.arr();
    for (const DeviceRec& d : i.devices) j.obj().kv("id", d.id).kv("name", d.name).ki("created", d.created).ki("lastSeen", d.lastSeen).end();
    j.end();
}

std::string deviceName(const std::string& wanted, const std::string& agent) {   // wie in mobile.cpp
    std::string n = cleanText(wanted, 40);
    if (!n.empty()) return n;
    if (agent.find("iPhone") != std::string::npos) return "iPhone";
    if (agent.find("iPad") != std::string::npos) return "iPad";
    if (agent.find("Android") != std::string::npos) return "Android";
    return "Gerät";
}

fdapi::FileReader openReader(const std::string& path, int64_t& size, int64_t* mtimeNs = nullptr) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return nullptr;
    }
    size = int64_t(st.st_size);
    if (mtimeNs) *mtimeNs = int64_t(st.st_mtim.tv_sec) * 1000000000 + int64_t(st.st_mtim.tv_nsec);
    std::shared_ptr<int> file(new int(fd), [](int* p) {
        close(*p);
        delete p;
    });
    return [file](int64_t off, char* buf, int64_t len) -> int64_t {   // pread: mehrere Anfragen stören sich nicht
        ssize_t k;
        do k = pread(*file, buf, size_t(len), off);
        while (k < 0 && errno == EINTR);
        return int64_t(k);
    };
}

}  // namespace

// ---- Grenzen gegen Raten --------------------------------------------------------------------------------------------
bool RateLimit::blocked(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = fails_.find(key);
    if (it == fails_.end()) return false;
    int64_t from = nowEpoch() - window_;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [&](int64_t t) { return t < from; }), v.end());
    return int(v.size()) >= max_;
}

void RateLimit::fail(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    if (fails_.size() > 20000) fails_.clear();   // Schutz gegen sehr viele verschiedene Adressen
    auto& v = fails_[key];
    v.push_back(nowEpoch());
    if (int(v.size()) > max_ * 2) v.erase(v.begin());
}

void RateLimit::prune() {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t from = nowEpoch() - window_;
    for (auto it = fails_.begin(); it != fails_.end();) {
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(), [&](int64_t t) { return t < from; }), v.end());
        it = v.empty() ? fails_.erase(it) : std::next(it);
    }
}

bool radarImage(const std::string& name, bool lower, std::string& png) {
    std::string file = name + (lower ? "_lower.png" : ".png");
    for (size_t i = 0; i < kRadarFileCount; ++i)
        if (file == kRadarFiles[i].path) {
            png.assign(reinterpret_cast<const char*>(kRadarFiles[i].data), kRadarFiles[i].size);
            return true;
        }
    return false;
}

bool webAsset(const std::string& path, std::string& body) {
    for (size_t i = 0; i < kWebFileCount; ++i)
        if (path == kWebFiles[i].path) {
            body.assign(reinterpret_cast<const char*>(kWebFiles[i].data), kWebFiles[i].size);
            return true;
        }
    return false;
}

// ---- Verteilen ------------------------------------------------------------------------------------------------------
void App::handle(const Request& req, Response& res) {
    Busy b(busy);
    if (stopping) {
        res.error(503, "Der Server startet gerade neu – gleich noch einmal versuchen.");
        res.set("Retry-After", "5");
        return;
    }
    const std::string& p = req.path;
    if (startsWith(p, "/api/")) {
        res.set("Cache-Control", "no-store");
        bool read = req.method == "GET" || req.method == "HEAD";
        // Schreibende Anfragen nur mit eigenem Kopf (Schutz gegen fremde Seiten im selben Browser) – /api/v1/local/*
        // prüft stattdessen den Schlüssel aus local.key (stärker; den kennt keine Webseite)
        if (!read && req.header("x-fragdesk") != "1" && !startsWith(p, "/api/v1/local/")) return res.error(403, "Nicht erlaubt");
        if (p == "/api/v1/health") return health(req, res);
        if (p == "/api/v1/account/register") return registerAccount(req, res);
        if (p == "/api/v1/account" || startsWith(p, "/api/v1/account/")) return account(req, res);
        if (startsWith(p, "/api/v1/sync/")) return sync(req, res);
        if (startsWith(p, "/api/v1/admin/")) {
            UserPtr who = store_.byAccountToken(bearer(req));
            if (!who) who = store_.byDeviceToken(req.cookie(kCookie));
            if (!who) return res.error(401, "Nicht angemeldet.");
            if (!store_.info(who).admin) return res.error(403, "Das dürfen nur Admins.");
            return admin(req, res, who, p.substr(14));
        }
        if (startsWith(p, "/api/v1/local/")) return local(req, res);
        if (p == "/api/v1/pair") return pair(req, res);
        if (p == "/api/v1/me") return me(req, res);
        if (p == "/api/v1/me/relink") return relink(req, res);
        if (p == "/api/v1/me/autodl" || startsWith(p, "/api/v1/me/autodl/")) return meAutodl(req, res);
        if (p == "/api/v1/logout") return phoneLogout(req, res);
        return phone(req, res);   // info, overview, matches, match, stats, moments, session
    }
    if (startsWith(p, "/media/")) return phone(req, res);
    fdapi::Host h = host(nullptr);   // App-Dateien
    fdapi::handle(h, req, res);
}

int64_t App::bodyLimit(const Request& head) {
    const std::string& p = head.path;
    // große Körper nur fürs Hochladen und nur mit gültigem Konto-Token – sonst könnte jeder den Speicher füllen
    if (head.method == "PUT" && startsWith(p, "/api/v1/sync/") && store_.validAccountToken(bearer(head))) {
        if (startsWith(p, "/api/v1/sync/clip/") && endsWith(p, "/data")) return kMaxChunk;
        if (startsWith(p, "/api/v1/sync/clip/") && endsWith(p, "/thumb")) return kMaxThumb;
        if (startsWith(p, "/api/v1/sync/match/") && endsWith(p, "/replay")) return kMaxReplayFile;
        if (startsWith(p, "/api/v1/sync/match/")) return kMaxMatchFile;
    }
    // Ergebnisse des Helfers: genauso, nur mit local.key (127.0.0.1, nicht über Caddy)
    if (head.method == "PUT" && startsWith(p, "/api/v1/local/fetch/") && p.find("/match/") != std::string::npos && localKeyOk(head))
        return endsWith(p, "/replay") ? kMaxReplayFile : kMaxMatchFile;
    return kMaxJson;
}

bool App::localKeyOk(const Request& req) {
    return loopback(req.remote) && req.header("x-forwarded-for").empty() && sameSecret(req.header("x-fragdesk-local"), store_.localKey());
}

void App::maintain() {
    login_.expire(nowEpoch());   // Passwort und Code nie länger als 15 Minuten im Speicher
    store_.maintain();
    views_.unloadIdle(idleUnload);
    if (fdapi::dropIdleReplay(idleUnload)) malloc_trim(0);   // zuletzt gelesene 2D-Daten (1–4 MB) nicht ewig behalten
    inviteLimit_.prune();
    pairLimit_.prune();
    localLimit_.prune();
}

std::string App::serverJson() {
    JsonW j;
    j.obj().kv("version", version_).kv("url", store_.settings().url).end();
    return j.take();
}

bool App::secure(const Request& req) {
    return startsWith(store_.settings().url, "https://") || (loopback(req.remote) && req.header("x-forwarded-proto") == "https");
}

fdapi::Host App::host(const UserPtr& u) {
    fdapi::Host h;
    h.version = version_;
    h.kind = "server";
    h.asset = webAsset;
    h.radar = radarImage;
    if (!u) {
        h.withSource = [](const std::function<void(fdapi::Source&)>&) { return false; };
        return h;
    }
    h.withSource = [this, u](const std::function<void(fdapi::Source&)>& fn) { return views_.with(u, fn); };
    h.openFile = [](const std::string& path, int64_t& size) { return openReader(path, size); };
    h.thumbnail = [this, u](const fdapi::ClipInfo& ci, std::string& jpeg) {
        return validClipId(ci.id) && readFile(store_.clipPath(u->id, ci.id, ".jpg"), jpeg, kMaxThumb);
    };
    // mediaBlocked bleibt leer: auf dem Server läuft kein CS2, Videos gibt es immer
    return h;
}

void App::health(const Request& req, Response& res) {
    if (req.method != "GET" && req.method != "HEAD") return res.error(405, "Nur GET");
    JsonW j;
    j.obj().kb("ok", true).kv("version", version_).end();
    res.body = j.take();
}

// ---- Konto (FragDesk) -----------------------------------------------------------------------------------------------
void App::registerAccount(const Request& req, Response& res) {
    if (req.method != "POST") return res.error(405, "Nur POST");
    std::string ip = clientIp(req);
    if (inviteLimit_.blocked(ip)) return res.error(429, "Zu viele Versuche mit ungültigen Einladungen. Warte 15 Minuten und versuch es dann noch einmal.");
    json::Value v;
    if (!parseBody(req, res, v)) return;
    std::string code = v.getString("invite");
    size_t at = code.find("#invite=");   // auch der ganze Link ist recht
    if (at != std::string::npos) code = code.substr(at + 8);
    code = trim(code);
    UserPtr u;
    std::string token;
    Fail f = store_.registerUser(code, v.getString("name"), v.getString("steamId"), v.getString("client"), u, token);
    if (!f.ok()) {
        if (f.status == 403) inviteLimit_.fail(ip);
        return fail(res, f);
    }
    UserInfo i = store_.info(u);
    JsonW j;
    j.obj().kv("token", token);
    j.key("user");
    writeUser(j, i);
    j.key("server").raw(serverJson());
    j.end();
    res.body = j.take();
}

void App::account(const Request& req, Response& res) {
    std::string token = bearer(req);
    UserPtr u = store_.byAccountToken(token);
    if (!u) return res.error(401, kNoAccount);
    const std::string& p = req.path;
    if (p == "/api/v1/account") {
        if (req.method != "GET" && req.method != "HEAD") return res.error(405, "Nur GET");
        UserInfo i = store_.info(u);
        JsonW j;
        j.obj().key("user");
        writeUser(j, i);
        j.key("quota");
        writeQuota(j, i);
        j.key("server").raw(serverJson());
        j.key("counts").obj().ki("matches", i.matches).ki("clips", i.clips).end();
        j.key("devices");
        writeDevices(j, i);
        j.end();
        res.body = j.take();
        return;
    }
    if (p == "/api/v1/account/profile") {
        if (req.method != "PUT" && req.method != "POST") return res.error(405, "Nur PUT");
        json::Value v;
        if (!parseBody(req, res, v)) return;
        UserInfo old = store_.info(u);
        Fail f = store_.setProfile(u, v.find("name") ? v.getString("name") : old.name, v.find("steamId") ? v.getString("steamId") : old.steamId);
        if (!f.ok()) return fail(res, f);
        views_.profileChanged(u);
        JsonW j;
        j.obj().key("user");
        writeUser(j, store_.info(u));
        j.end();
        res.body = j.take();
        return;
    }
    if (p == "/api/v1/account/logout") {
        if (req.method != "POST") return res.error(405, "Nur POST");
        store_.logoutToken(u, token);
        return ok(res);
    }
    // Demos automatisch laden aus FragDesk am PC: dieselben Anfragen und Antworten wie /api/v1/me/autodl
    if (p == "/api/v1/account/autodl") return autodl(req, res, u, false);
    if (p == "/api/v1/account/autodl/check") return autodl(req, res, u, true);
    if (startsWith(p, "/api/v1/account/devices/")) {   // gekoppeltes Handy entfernen (Erweiterung)
        if (req.method != "DELETE") return res.error(405, "Nur DELETE");
        if (!store_.removeDeviceById(u, p.substr(24))) return res.error(404, "Dieses Handy gibt es nicht.");
        return ok(res);
    }
    res.error(404, "Unbekannte Anfrage");
}

// ---- Abgleich (FragDesk lädt hoch) ------------------------------------------------------------------------------------
void App::sync(const Request& req, Response& res) {
    UserPtr u = store_.byAccountToken(bearer(req));
    if (!u) return res.error(401, kNoAccount);
    std::vector<std::string> part = split(req.path.substr(13));   // nach /api/v1/sync/
    const std::string& m = req.method;

    if (part.size() == 1 && part[0] == "state") {
        if (m != "GET" && m != "HEAD") return res.error(405, "Nur GET");
        auto mf = store_.matchFiles(u);
        auto cf = store_.clipFiles(u);
        JsonW j;
        j.obj().key("matches").obj();
        for (const auto& kv : mf) {
            const MatchRec& r = kv.second;
            if (r.json < 0 && r.heat < 0) continue;
            j.key(kv.first.c_str()).obj().ki("json", std::max<int64_t>(0, r.json)).ki("heat", std::max<int64_t>(0, r.heat));
            j.ki("replay", std::max<int64_t>(0, r.replay));   // 2D-Ansicht (0 = fehlt)
            if (r.noreplay >= 0) j.kb("noreplay", true);       // FragDesk hat gemeldet: keine 2D-Daten möglich
            if (r.src >= 0) {   // der Server hat das Match selbst bei Valve geholt; Adresse der Demo, solange Valve sie aufhebt
                j.kb("fetched", true);
                const MatchSource& s = r.source;
                if (!s.url.empty()) {
                    j.key("src").obj().kv("url", s.url).ki("matchtime", s.matchtime).ki("until", s.until);
                    if (!s.shareCode.empty()) j.kv("shareCode", s.shareCode);
                    j.end();
                }
            }
            j.end();
        }
        j.end().key("clips").obj();
        for (const auto& kv : cf) {
            const ClipRec& c = kv.second;
            if (c.mp4 < 0 && c.part < 0 && c.thumb < 0) continue;
            j.key(kv.first.c_str()).obj().ki("size", c.mp4 >= 0 ? c.mp4 : std::max<int64_t>(0, c.part)).kb("complete", c.complete()).kb("thumb", c.thumb >= 0).end();
        }
        j.end().key("quota").obj().ki("used", store_.used(u)).ki("limit", store_.limit(u)).end();
        j.kb("replays", true);    // nimmt 2D-Daten an – fehlt bei älteren Servern, dann lädt FragDesk keine hoch
        j.kb("download", true);   // liefert Matches aus (GET /sync/match/…) – fehlt bei älteren: FragDesk holt dann nichts
        j.end();
        res.body = j.take();
        return;
    }
    if (part.size() == 1 && part[0] == "pairing") {
        if (m == "DELETE") {
            store_.stopPairing(u);
            return ok(res);
        }
        if (m != "POST") return res.error(405, "Nur POST");
        int64_t until = 0;
        std::string code = store_.startPairing(u, until);
        if (code.empty()) return res.error(500, "Kein Zufallsgenerator verfügbar.");
        JsonW j;
        j.obj().kv("code", code).ki("until", until).kv("url", store_.settings().url).end();
        res.body = j.take();
        return;
    }
    if (part.size() >= 2 && part.size() <= 3 && part[0] == "match") {
        const std::string& checksum = part[1];
        if (!validChecksum(checksum)) return res.error(400, "Ungültige Prüfsumme.");
        if (part.size() == 2) {
            if (m != "DELETE") return res.error(405, "Nur DELETE");
            Fail f = store_.deleteMatch(u, checksum);
            views_.matchChanged(u, checksum);
            if (!f.ok()) return fail(res, f);
            return ok(res);
        }
        if (m == "GET" || m == "HEAD") return getMatch(req, res, u, checksum, part[2]);   // FragDesk holt, was ihm fehlt
        putMatch(req, res, u, checksum, part[2], nullptr);
        return;
    }
    if (part.size() >= 2 && part.size() <= 3 && part[0] == "clip") {
        const std::string& id = part[1];
        if (!validClipId(id)) return res.error(400, "Ungültige Video-Kennung (16 Hex-Zeichen).");
        std::string sub = part.size() == 3 ? part[2] : std::string();
        if (sub.empty()) {
            if (m != "DELETE") return res.error(405, "Nur DELETE");
            Fail f = store_.deleteClip(u, id);
            if (!f.ok()) return fail(res, f);
            return ok(res);
        }
        if (sub == "data") {
            if (m != "PUT") return res.error(405, "Nur PUT");
            int64_t offset = 0, total = 0, size = 0;
            if (!parseInt(req.param("offset"), offset) || !parseInt(req.param("total"), total))
                return res.error(400, "offset und total fehlen oder sind ungültig.");
            if (req.body.empty()) return res.error(400, "Das Stück ist leer.");
            if (int64_t(req.body.size()) > kMaxChunk) return res.error(413, "Das Stück ist zu groß (höchstens 8 MB).");
            Fail f = store_.appendClip(u, id, offset, total, req.body, size);
            if (!f.ok()) return fail(res, f);
            JsonW j;
            j.obj().ki("size", size).end();
            res.body = j.take();
            return;
        }
        if (sub == "thumb") {
            if (m != "PUT") return res.error(405, "Nur PUT");
            if (int64_t(req.body.size()) > kMaxThumb) return res.error(413, "Das Vorschaubild ist zu groß (höchstens 1 MB).");
            const std::string& b = req.body;
            if (b.size() < 4 || uint8_t(b[0]) != 0xFF || uint8_t(b[1]) != 0xD8 || uint8_t(b[2]) != 0xFF) return res.error(400, "Das ist kein JPEG-Bild.");
            Fail f = store_.putThumb(u, id, b);
            if (!f.ok()) return fail(res, f);
            return ok(res);
        }
        if (sub == "done") {
            if (m != "POST") return res.error(405, "Nur POST");
            json::Value v;
            if (!parseBody(req, res, v)) return;
            ClipMeta meta;
            meta.title = cleanText(v.getString("title"), 200);
            meta.match = v.getString("match");
            if (!validChecksum(meta.match)) meta.match.clear();
            meta.highlight = cleanText(v.getString("highlight"), 100);
            meta.map = cleanText(v.getString("map"), 64);
            meta.created = std::max<int64_t>(0, int64_t(v.getNumber("created")));
            meta.seconds = v.getNumber("seconds");
            if (!std::isfinite(meta.seconds) || meta.seconds < 0) meta.seconds = 0;
            if (meta.created == 0) meta.created = nowEpoch();
            int64_t total = int64_t(v.getNumber("total", -1));
            Fail f = store_.finishClip(u, id, total, meta);
            if (!f.ok()) return fail(res, f);
            JsonW j;
            j.obj().kb("ok", true).ki("size", total).end();
            res.body = j.take();
            return;
        }
    }
    res.error(404, "Unbekannte Anfrage");
}

// Ein Teil eines Matches des eigenen Kontos ausliefern (json, heat, replay) – FragDesk holt damit Matches, die der
// Server selbst geladen hat oder die von einem anderen PC kommen. Nur aus dem Ordner des Kontos (Prüfsumme geprüft);
// ETag aus Größe und Änderungszeit, If-None-Match → 304.
void App::getMatch(const Request& req, Response& res, const UserPtr& u, const std::string& checksum, const std::string& what) {
    MatchPart mp;
    if (what == "json") mp = MatchPart::Json;
    else if (what == "heat") mp = MatchPart::Heat;
    else if (what == "replay") mp = MatchPart::Replay;
    else return res.error(404, "Unbekannte Anfrage");
    int64_t size = -1, mtime = 0;
    fdapi::FileReader rd = validChecksum(checksum) ? openReader(store_.matchPath(u->id, checksum, matchExt(mp)), size, &mtime) : nullptr;
    if (!rd) return res.error(404, mp == MatchPart::Json ? "Dieses Match gibt es auf dem Server nicht (mehr)." : "Diese Datei gibt es auf dem Server nicht.");
    std::string etag = "\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"";
    res.set("ETag", etag);
    if (req.header("if-none-match") == etag) {
        res.status = 304;
        return;
    }
    res.type = mp == MatchPart::Json ? "application/json; charset=utf-8" : "application/octet-stream";
    res.file = rd;
    res.fileSize = size;
}

bool App::putMatch(const Request& req, Response& res, const UserPtr& u, const std::string& checksum, const std::string& what, bool* isNew) {
    MatchPart mp = what == "heat" ? MatchPart::Heat : what == "replay" ? MatchPart::Replay : what == "noreplay" ? MatchPart::NoReplay : MatchPart::Json;
    if (mp == MatchPart::Json && what != "json") {
        res.error(404, "Unbekannte Anfrage");
        return false;
    }
    if (req.method != "PUT") {
        res.error(405, "Nur PUT");
        return false;
    }
    auto bad = [&](int status, const std::string& text) {
        res.error(status, text);
        return false;
    };
    std::string marker;   // NoReplay: nur der Grund, sonst der Körper der Anfrage (ohne Kopie)
    const std::string& body = mp == MatchPart::NoReplay ? marker : req.body;
    if (mp == MatchPart::Replay) {   // 2D-Ansicht: ganz lesen – was hier liegt, muss die Handy-App auch zeigen können
        if (int64_t(body.size()) > kMaxReplayFile) return bad(413, "Die Datei ist zu groß (höchstens 16 MB).");
        if (body.size() < 8 || body.compare(0, 4, "FDRP") != 0) return bad(400, "Das sind keine 2D-Daten von FragDesk.");
        fd::Replay r;
        std::string err;
        if (!fd::parseReplay(body, r, err)) return bad(400, "Die 2D-Daten lassen sich nicht lesen: " + err);
    } else if (mp != MatchPart::NoReplay && int64_t(body.size()) > kMaxMatchFile) {
        return bad(413, "Die Datei ist zu groß (höchstens 4 MB).");
    } else if (mp == MatchPart::Heat) {
        if (body.size() < 8 || body.compare(0, 4, "FDHM") != 0) return bad(400, "Das ist keine Heatmap-Datei von FragDesk.");
    } else if (mp == MatchPart::NoReplay) {   // {"reason": "demo"|"failed"} – nur der Grund wird gespeichert
        json::Value v;
        std::string reason = json::parse(req.body, v) && v.isObject() ? v.getString("reason") : std::string();
        if (reason != "demo" && reason != "failed") return bad(400, "reason muss \"demo\" oder \"failed\" sein.");
        marker = "{\"reason\":\"" + reason + "\"}\n";
    } else {
        fd::Match match;
        std::string err;
        if (!fd::parseMatch(body, match, err, true)) return bad(400, "Das Match lässt sich nicht lesen: " + err);
        if (match.checksum != checksum) return bad(400, "Die Prüfsumme passt nicht zum Match.");
    }
    Fail f = store_.putMatchFile(u, checksum, mp, body, isNew);
    if (!f.ok()) {
        fail(res, f);
        return false;
    }
    if (mp == MatchPart::Json) views_.matchChanged(u, checksum);   // neue Generation: gleich in der Handy-App
    JsonW j;
    j.obj().kb("ok", true).ki("size", int64_t(body.size())).end();
    res.body = j.take();
    return true;
}

// ---- Admin ------------------------------------------------------------------------------------------------------------
// Einladung (neues Konto) oder mit "user" ein Link „PC neu verbinden“ für dieses Konto (gilt höchstens 1 Tag)
void App::createInvite(const Request& req, Response& res, const std::string& by) {
    if (req.method != "POST") return res.error(405, "Nur POST");
    json::Value v;
    if (!parseBody(req, res, v)) return;
    std::string forUser = v.getString("user");
    if (!forUser.empty() && !validUserId(forUser)) return res.error(404, "Dieses Konto gibt es nicht.");
    int days = int(std::max(1.0, std::min(forUser.empty() ? 30.0 : 1.0, v.getNumber("days", 7))));
    sendInvite(res, by, v.getBool("admin"), days, forUser);
}

void App::sendInvite(Response& res, const std::string& by, bool admin, int days, const std::string& forUser) {
    Invite inv;
    std::string code;
    Fail f = store_.createInvite(admin, days, by, inv, code, forUser);
    if (!f.ok()) return fail(res, f);
    UserPtr target = forUser.empty() ? nullptr : store_.byId(forUser);
    JsonW j;
    j.obj()
        .kv("id", inv.id)
        .kv("code", code)
        .kv("link", store_.inviteLink(code))
        .ki("expires", inv.expires)
        .kb("admin", inv.admin)
        .ki("days", days)
        .kv("user", inv.user)
        .kv("userName", target ? store_.profileName(target) : std::string())
        .end();
    res.body = j.take();
}

void App::listUsers(Response& res) {
    JsonW j;
    j.arr();
    for (const UserInfo& i : store_.users()) writeEntry(j, i);
    j.end();
    res.body = j.take();
}

void App::admin(const Request& req, Response& res, const UserPtr& who, const std::string& sub) {
    const std::string& m = req.method;
    bool get = m == "GET" || m == "HEAD";
    if (sub == "steam" || startsWith(sub, "steam/")) return adminSteam(req, res, sub);
    if (sub == "invite") return createInvite(req, res, who->id);
    if (sub == "invites") {
        if (!get) return res.error(405, "Nur GET");
        JsonW j;
        j.arr();
        for (const Invite& i : store_.invites()) {
            UserPtr by = i.by.empty() ? nullptr : store_.byId(i.by);
            j.obj()
                .kv("id", i.id)
                .kb("admin", i.admin)
                .ki("created", i.created)
                .ki("expires", i.expires)
                .kv("by", i.by)
                .kv("byName", by ? store_.profileName(by) : i.by.empty() ? "Befehlszeile" : "")
                .kv("user", i.user)
                .kv("userName", i.user.empty() ? std::string() : [&] { UserPtr t = store_.byId(i.user); return t ? store_.profileName(t) : std::string(); }())
                .end();
        }
        j.end();
        res.body = j.take();
        return;
    }
    if (startsWith(sub, "invites/")) {
        if (m != "DELETE") return res.error(405, "Nur DELETE");
        if (!store_.removeInvite(sub.substr(8))) return res.error(404, "Diese Einladung gibt es nicht (mehr).");
        return ok(res);
    }
    if (sub == "users") {
        if (!get) return res.error(405, "Nur GET");
        return listUsers(res);
    }
    if (startsWith(sub, "users/")) {
        std::string id = sub.substr(6);
        if (!validUserId(id)) return res.error(404, "Dieses Konto gibt es nicht.");
        if (m == "DELETE") {
            Fail f = store_.removeUser(id);
            if (!f.ok()) return fail(res, f);
            views_.drop(id);
            return ok(res);
        }
        if (m != "PUT") return res.error(405, "Nur PUT oder DELETE");
        json::Value v;
        if (!parseBody(req, res, v)) return;
        const json::Value* lim = v.find("limit");
        if (!lim || !lim->isNumber() || !std::isfinite(lim->num)) return res.error(400, "limit fehlt (in Bytes, 0 = unbegrenzt).");
        Fail f = store_.setLimit(id, int64_t(lim->num));
        if (!f.ok()) return fail(res, f);
        UserPtr u = store_.byId(id);
        if (!u) return res.error(404, "Dieses Konto gibt es nicht.");
        JsonW j;
        writeEntry(j, store_.info(u));
        res.body = j.take();
        return;
    }
    res.error(404, "Unbekannte Anfrage");
}

// ---- Befehlszeile (fragdesk-server invite/users über 127.0.0.1 mit local.key) -------------------------------------------
void App::local(const Request& req, Response& res) {
    // Caddy kommt auch von 127.0.0.1, setzt aber X-Forwarded-For: so kommt von außen niemand hierher
    if (!loopback(req.remote) || !req.header("x-forwarded-for").empty()) return res.error(404, "Gibt es nicht");
    if (localLimit_.blocked(req.remote)) return res.error(429, "Zu viele falsche Schlüssel. Warte 15 Minuten.");
    if (!sameSecret(req.header("x-fragdesk-local"), store_.localKey())) {
        localLimit_.fail(req.remote);
        return res.error(403, "Falscher Schlüssel (local.key).");
    }
    std::string sub = req.path.substr(14);
    if (startsWith(sub, "fetch/")) return localFetch(req, res, sub.substr(6));
    if (sub == "invite") return createInvite(req, res, std::string());
    if (sub == "users") {
        if (req.method != "GET") return res.error(405, "Nur GET");
        return listUsers(res);
    }
    res.error(404, "Unbekannte Anfrage");
}

// ---- Demos automatisch laden: Handy und FragDesk ------------------------------------------------------------------------
void App::meAutodl(const Request& req, Response& res) {
    UserPtr u = store_.byDeviceToken(deviceToken(req));
    if (!u) return res.error(401, kNoDevice);
    if (req.path == "/api/v1/me/autodl/check") return autodl(req, res, u, true);
    if (req.path != "/api/v1/me/autodl") return res.error(404, "Unbekannte Anfrage");
    autodl(req, res, u, false);
}

// GET/PUT/DELETE …/autodl und POST …/autodl/check – für das Handy (/api/v1/me/autodl, Geräte-Cookie) und FragDesk am PC
// (/api/v1/account/autodl, Konto-Token) gleich
void App::autodl(const Request& req, Response& res, const UserPtr& u, bool check) {
    const std::string& m = req.method;
    int64_t now = nowEpoch();
    if (check) {   // „Jetzt prüfen“
        if (m != "POST") return res.error(405, "Nur POST");
        if (!store_.autodl(u).enabled) return res.error(409, "„Demos automatisch laden“ ist aus – erst einschalten.");
        std::string problem = helperProblem(helperEnv);
        if (!problem.empty()) return res.error(409, problem);
        if (!store_.steam().ready(now)) return res.error(409, "Der Steam-Bot ist noch nicht eingerichtet.");
        if (store_.requestCheck(u, now)) store_.wakeHelper();   // höchstens alle 30 s
        return writeAutodl(res, u);
    }
    if (m == "GET" || m == "HEAD") return writeAutodl(res, u);
    if (m == "DELETE") {   // aus, Codes löschen
        Fail f = store_.clearAutodl(u);
        if (!f.ok()) return fail(res, f);
        std::cerr << "Konto " << u->id << ": Demos automatisch laden aus, Codes gelöscht\n";
        return writeAutodl(res, u);
    }
    if (m != "PUT" && m != "POST") return res.error(405, "Nur GET, PUT oder DELETE");
    json::Value v;
    if (!parseBody(req, res, v)) return;
    if (store_.profileId(u).empty()) return res.error(400, "Erst in FragDesk am PC deinen Spieler wählen.");
    std::string auth = normAuthCode(v.getString("authCode")), known = normShareCode(v.getString("knownCode"));
    if (!auth.empty() && !validAuthCode(auth))
        return res.error(400, "Der Authentifizierungscode sieht so aus: AAAA-AAAAA-AAAA (Buchstaben und Ziffern). Bitte noch einmal kopieren.");
    if (!known.empty() && !validShareCode(known))
        return res.error(400, "Der Freigabecode sieht so aus: CSGO-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx (ohne 0, 1, I, l und g). Bitte noch einmal kopieren.");
    const json::Value* en = v.find("enabled");
    int enabled = en && en->type == json::Value::Bool ? (en->b ? 1 : 0) : -1;
    bool before = store_.autodl(u).enabled;
    Fail f = store_.setAutodl(u, auth, known, enabled);
    if (!f.ok()) return fail(res, f);
    AutoDl a = store_.autodl(u);
    if (a.enabled != before) std::cerr << "Konto " << u->id << ": Demos automatisch laden " << (a.enabled ? "an" : "aus") << "\n";
    if (a.enabled && helperProblem(helperEnv).empty() && store_.steam().ready(now)) store_.wakeHelper();   // gleich prüfen
    writeAutodl(res, u);
}

void App::writeAutodl(Response& res, const UserPtr& u) {
    AutoDl a = store_.autodl(u);
    UserInfo i = store_.info(u);
    SteamBot bot = store_.steam();
    int64_t now = nowEpoch();
    AutodlFacts f;
    f.enabled = a.enabled;
    f.hasAuth = !a.authCode.empty();
    f.hasKnown = !a.knownCode.empty();
    f.hasSteamId = !i.steamId.empty();
    f.botReady = bot.ready(now);
    f.botNeedsLogin = bot.needsLogin(now);
    f.admin = i.admin;
    f.helperProblem = helperProblem(helperEnv);
    f.lastError = a.lastError;
    f.lastCheck = a.lastCheck;
    f.fetched = a.fetched;
    f.now = now;
    bool running = bot.running && now - bot.runStart < 2 * 3600;   // ohne Ende: nach 2 h nicht mehr „läuft“
    f.checking = running && a.jobAt > 0 && a.jobAt + 60 >= bot.runStart;
    f.soon = !f.checking && a.checkAt > 0 && now - a.checkAt < 90 && a.lastCheck < a.checkAt;
    f.full = a.enabled && store_.nearlyFull(u);
    std::string tone, status = autodlStatus(f, tone);
    JsonW j;
    j.obj()
        .kb("enabled", a.enabled)
        .kv("authCodeMasked", maskAuthCode(a.authCode))
        .kv("knownCode", a.knownCode)
        .kv("steamId", i.steamId)
        .ki("lastCheck", a.lastCheck)
        .ki("lastNew", a.lastNew)
        .kv("lastError", a.lastError)
        .ki("fetched", a.fetched)
        .kb("botReady", f.botReady)
        .kv("status", status)
        // Erweiterungen für die Anzeige
        .kv("tone", tone)
        .kb("botNeedsLogin", f.botNeedsLogin)   // Bot war angemeldet, muss neu angemeldet werden
        .kb("ready", f.botReady && f.helperProblem.empty() && f.hasSteamId)   // alles da, es kann losgehen
        .kb("checking", f.checking || f.soon)
        .kb("admin", i.admin)
        .ki("every", kCheckMinutes)
        .end();
    res.body = j.take();
}

// ---- Demos automatisch laden: Steam-Bot (Admin) ---------------------------------------------------------------------------
void App::adminSteam(const Request& req, Response& res, const std::string& sub) {
    const std::string& m = req.method;
    int64_t now = nowEpoch();
    if (sub == "steam") {
        if (m == "GET" || m == "HEAD") return writeSteam(res);
        if (m != "DELETE") return res.error(405, "Nur GET oder DELETE");
        login_.cancel();
        Fail f = store_.forgetSteamBot();
        if (!f.ok()) return fail(res, f);
        std::cerr << "Steam-Bot abgemeldet (Token gelöscht)\n";
        return writeSteam(res);
    }
    json::Value v;
    if (sub == "steam/login") {   // Benutzername und Passwort: nur im Speicher, der Helfer holt sie genau einmal ab
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string user = trim(v.getString("user")), password = v.getString("password");
        if (!validSteamName(user)) return res.error(400, "Der Benutzername des Bot-Kontos fehlt (so, wie du dich bei Steam anmeldest).");
        if (password.empty() || password.size() > 512) return res.error(400, "Das Passwort des Bot-Kontos fehlt.");
        std::string problem = helperProblem(helperEnv);
        if (!problem.empty()) return res.error(409, problem);
        login_.start(user, password, now);
        std::cerr << "Steam-Bot: Anmeldung angefragt – Helfer wird geweckt\n";
        store_.wakeHelper();
        return writeSteam(res);
    }
    if (sub == "steam/code") {   // Steam-Guard-Code für den Helfer bereithalten
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string code = normGuardCode(v.getString("code")), err;
        if (!validGuardCode(code)) return res.error(400, "Der Code hat 5 Zeichen (Buchstaben und Ziffern).");
        if (!login_.code(code, now, err)) return res.error(409, err);
        return writeSteam(res);
    }
    if (sub == "steam/apikey") {   // Web-API-Schlüssel des Bots (leer = entfernen)
        if (m != "PUT" && m != "POST") return res.error(405, "Nur PUT");
        if (!parseBody(req, res, v)) return;
        std::string key = normApiKey(v.getString("key"));
        if (!key.empty() && !validApiKey(key)) return res.error(400, "Der Web-API-Schlüssel hat 32 Zeichen (0–9 und A–F). Bitte noch einmal kopieren.");
        Fail f = store_.setSteamKey(key);
        if (!f.ok()) return fail(res, f);
        std::cerr << "Steam-Bot: Web-API-Schlüssel " << (key.empty() ? "entfernt" : "gespeichert") << "\n";
        if (!key.empty() && store_.steam().ready(now) && store_.autodlCount() > 0 && helperProblem(helperEnv).empty()) store_.wakeHelper();
        return writeSteam(res);
    }
    res.error(404, "Unbekannte Anfrage");
}

void App::writeSteam(Response& res) {
    SteamBot bot = store_.steam();
    int64_t now = nowEpoch();
    SteamLogin::View lv = login_.view(now);
    std::string hint = lv.hint, problem = helperProblem(helperEnv);
    if (lv.active && lv.state == "working" && !lv.pickedUp && now - lv.started >= 45)
        hint = "Der Helfer meldet sich noch nicht – vielleicht lädt er gerade Demos. Dauert es länger: auf dem Server "
               "„systemctl status fragdesk-fetch.path“ und „journalctl -u fragdesk-fetch“ ansehen.";
    bool running = bot.running && now - bot.runStart < 2 * 3600;
    JsonW j;
    j.obj();
    j.key("bot").obj().kv("name", bot.name).kv("steamId", bot.steamId).kb("loggedIn", bot.loggedIn(now)).ki("tokenUntil", bot.tokenUntil);
    // Erweiterung: war angemeldet, gilt aber nicht mehr (Token abgelehnt oder abgelaufen) – mit Grund
    j.kb("needsLogin", bot.needsLogin(now))
        .kv("problem", bot.tokenBad ? bot.tokenHint : bot.needsLogin(now) ? std::string("Das Anmelde-Token ist abgelaufen.") : std::string())
        .end();
    j.kb("apiKeySet", bot.hasKey);
    j.key("login").obj().kv("state", lv.state).kv("hint", hint).kv("codeType", lv.codeType).ki("started", lv.started).end();
    j.key("lastRun").obj().ki("at", bot.lastRunAt).kv("text", bot.lastRunText).kb("running", running).ki("started", running ? bot.runStart : 0).end();
    j.ki("accounts", store_.autodlCount());
    j.kb("ready", bot.ready(now) && problem.empty());   // Erweiterungen: alles bereit / was auf dem Server fehlt
    j.kv("helper", problem);
    j.end();
    res.body = j.take();
}

// ---- Demos automatisch laden: Helfer (über 127.0.0.1 mit local.key) ---------------------------------------------------------
void App::localFetch(const Request& req, Response& res, const std::string& sub) {
    const std::string& m = req.method;
    int64_t now = nowEpoch();
    json::Value v;
    if (sub == "job") {   // Auftrag: Bot, Anmeldung (Passwort und Code je genau einmal), eingeschaltete Konten
        if (m != "GET") return res.error(405, "Nur GET");
        std::string token, key;
        store_.steamSecrets(token, key);
        SteamLogin::Handout h = login_.take(now);
        std::vector<FetchAccount> accounts = store_.fetchAccounts(now);
        JsonW j;
        j.obj();
        j.key("bot").obj().kv("refreshToken", token).kv("apiKey", key).end();
        j.key("login");
        if (!h.active) {
            j.null();
        } else {
            j.obj().kv("user", h.user);
            if (h.hasPassword) j.kv("password", h.password);
            else j.key("password").null();
            if (h.hasCode) j.kv("code", h.code);
            else j.key("code").null();
            j.end();
        }
        j.key("accounts").arr();
        for (const FetchAccount& a : accounts)
            j.obj().kv("id", a.id).kv("steamId", a.steamId).kv("authCode", a.authCode).kv("knownCode", a.knownCode).end();
        j.end().end();
        res.body = j.take();
        return;
    }
    if (sub == "login") {   // Zwischenstände und Ergebnis der Anmeldung
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string state = v.getString("state"), hint = cleanText(v.getString("hint"), 300), codeType = v.getString("codeType"), err;
        if (state != "working" && state != "need-code" && state != "need-confirm" && state != "done" && state != "error")
            return res.error(400, "state muss working, need-code, need-confirm, done oder error sein.");
        if (state == "done") {
            std::string token = trim(v.getString("refreshToken"));
            if (!validRefreshToken(token)) return res.error(400, "refreshToken fehlt oder ist ungültig.");
            const json::Value* sid = v.find("botSteamId");
            std::string steamId = sid && sid->isNumber() ? std::to_string(int64_t(sid->num)) : cleanSteamId64(v.getString("botSteamId"));
            std::string name = cleanText(v.getString("botName"), 64);
            Fail f = store_.setSteamLogin(token, name, steamId, epochFrom(v.getNumber("tokenUntil")));
            if (!f.ok()) return fail(res, f);
            if (hint.empty()) hint = name.empty() ? "Angemeldet." : "Angemeldet als " + name + ".";
            std::cerr << "Steam-Bot: angemeldet" << (steamId.empty() ? "" : " (SteamID " + steamId + ")") << "\n";
        }
        if (state == "error" && !login_.loginError(now)) {
            // keine Anmeldung offen (auch keine eben abgelaufene): Steam hat im normalen Lauf das gespeicherte Token
            // abgelehnt – neu anmelden nötig
            if (store_.steam().hasToken) {
                store_.steamTokenRejected(hint.empty() ? "Steam hat die Anmeldung des Bots abgelehnt." : hint);
                std::cerr << "Steam-Bot: Steam lehnt das gespeicherte Token ab – bitte neu anmelden (" << hint << ")\n";
            }
            return ok(res);
        }
        if (!login_.report(state, hint, codeType, now, err)) return res.error(409, err);
        if (state == "error") std::cerr << "Steam-Bot: Anmeldung hat nicht geklappt – " << hint << "\n";
        return ok(res);
    }
    if (sub == "token") {   // von Steam erneuertes Token
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string token = trim(v.getString("refreshToken"));
        if (!validRefreshToken(token)) return res.error(400, "refreshToken fehlt oder ist ungültig.");
        Fail f = store_.setSteamToken(token, epochFrom(v.getNumber("tokenUntil")));
        if (!f.ok()) return fail(res, f);
        return ok(res);
    }
    if (sub == "run") {   // Lauf beginnt / ist zu Ende (für die Admin-Anzeige)
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string phase = v.getString("phase");
        if (phase != "start" && phase != "end") return res.error(400, "phase muss \"start\" oder \"end\" sein.");
        store_.runPhase(phase == "start", cleanText(v.getString("text"), 300), now);
        return ok(res);
    }
    // <konto>/state, <konto>/match/<prüfsumme>/<teil>
    std::vector<std::string> part = split(sub);
    UserPtr u = validUserId(part[0]) ? store_.byId(part[0]) : nullptr;
    if (!u || u->deleted) return res.error(404, "Dieses Konto gibt es nicht (mehr).");
    if (part.size() == 2 && part[1] == "state") {
        if (m != "POST") return res.error(405, "Nur POST");
        if (!parseBody(req, res, v)) return;
        std::string known = normShareCode(v.getString("knownCode"));
        if (!validShareCode(known)) return res.error(400, "knownCode fehlt oder ist kein Freigabecode (CSGO-…).");
        int64_t lastCheck = epochFrom(v.getNumber("lastCheck"));
        const json::Value* le = v.find("lastError");
        std::string lastError = le && le->isString() ? cleanText(le->str, 300) : std::string();
        bool ignored = false;
        // fetched zählt der Server selbst (je neu importiertem Match) – so zählt nichts doppelt; from (optional): der
        // Freigabecode, von dem der Helfer ausgeht
        Fail f = store_.fetchState(u, known, normShareCode(v.getString("from")), lastCheck > 0 ? lastCheck : now, epochFrom(v.getNumber("lastNew")),
                                   le && le->isString() ? &lastError : nullptr, ignored);
        if (!f.ok()) return fail(res, f);
        JsonW j;
        j.obj().kb("ok", true).kb("ignored", ignored).end();
        res.body = j.take();
        return;
    }
    if (part.size() == 4 && part[1] == "match") {   // Ergebnisse: wie /api/v1/sync/match
        const std::string& checksum = part[2];
        if (!validChecksum(checksum)) return res.error(400, "Ungültige Prüfsumme.");
        if (!store_.autodl(u).enabled) return res.error(403, "Für dieses Konto ist „Demos automatisch laden“ aus.");   // Helfer: Konto überspringen
        if (part[3] == "source") {   // nach dem Import: woher die Demo kam (für „Demo von Valve laden“ in FragDesk)
            if (m != "PUT" && m != "POST") return res.error(405, "Nur PUT");
            if (!parseBody(req, res, v)) return;
            MatchSource src;
            src.url = trim(v.getString("url"));
            if (!validDemoUrl(src.url)) return res.error(400, "url fehlt oder ist keine Adresse (http://…).");
            src.shareCode = normShareCode(v.getString("shareCode"));
            if (!src.shareCode.empty() && !validShareCode(src.shareCode)) return res.error(400, "shareCode ist kein Freigabecode (CSGO-…).");
            src.matchtime = epochFrom(v.getNumber("matchtime"));
            src.until = src.matchtime > 0 ? src.matchtime + kDemoKeepDays * 86400 : 0;   // so lange hebt Valve sie etwa auf
            src.at = now;
            Fail f = store_.putMatchSource(u, checksum, src);
            if (!f.ok()) return fail(res, f);
            JsonW j;
            j.obj().kb("ok", true).ki("until", src.until).end();
            res.body = j.take();
            return;
        }
        bool isNew = false;
        if (!putMatch(req, res, u, checksum, part[3], &isNew)) return;
        if (isNew) {
            store_.fetchedMatch(u, now);
            std::cerr << "Konto " << u->id << ": neues Match " << checksum << " automatisch geladen\n";
        }
        return;
    }
    res.error(404, "Unbekannte Anfrage");
}

// ---- Handy ----------------------------------------------------------------------------------------------------------
void App::pair(const Request& req, Response& res) {
    if (req.method != "POST") return res.error(405, "Nur POST");
    std::string ip = clientIp(req);
    const char* tooMany = "Zu viele falsche Versuche. Warte 15 Minuten oder starte in FragDesk „Handy verbinden“ neu.";
    if (pairLimit_.blocked(ip)) return res.error(429, tooMany);
    json::Value v;
    if (!parseBody(req, res, v)) return;
    std::string token, err;
    UserPtr u = store_.pair(v.getString("code"), deviceName(v.getString("name"), req.header("user-agent")), token, err);
    if (!u) {
        pairLimit_.fail(ip);
        return pairLimit_.blocked(ip) ? res.error(429, tooMany) : res.error(403, err);
    }
    res.set("Set-Cookie", std::string(kCookie) + "=" + token + "; Path=/; Max-Age=315360000; HttpOnly; SameSite=Strict" + (secure(req) ? "; Secure" : ""));
    JsonW j;
    j.obj().kb("ok", true).ki("api", fdapi::kApiVersion).kv("version", version_).kv("kind", "server").end();
    res.body = j.take();
}

void App::me(const Request& req, Response& res) {
    if (req.method != "GET" && req.method != "HEAD") return res.error(405, "Nur GET");
    UserPtr u = store_.byDeviceToken(deviceToken(req));
    if (!u) return res.error(401, kNoDevice);
    UserInfo i = store_.info(u);
    JsonW j;
    j.obj().key("user");
    writeUser(j, i);
    j.key("quota");
    writeQuota(j, i);
    j.key("counts").obj().ki("matches", i.matches).ki("clips", i.clips).ki("devices", int64_t(i.devices.size())).end();
    j.key("server").raw(serverJson());
    j.end();
    res.body = j.take();
}

// „PC neu verbinden“ vom eigenen Handy: Link für das eigene Konto (neuer PC, nach „Trennen“), gilt 1 Tag
void App::relink(const Request& req, Response& res) {
    if (req.method != "POST") return res.error(405, "Nur POST");
    UserPtr u = store_.byDeviceToken(deviceToken(req));
    if (!u) return res.error(401, kNoDevice);
    sendInvite(res, u->id, false, 1, u->id);
}

void App::phoneLogout(const Request& req, Response& res) {
    if (req.method != "POST") return res.error(405, "Nur POST");
    std::string token = deviceToken(req);
    UserPtr u = store_.byDeviceToken(token);
    if (!u) return res.error(401, kNoDevice);
    store_.removeDevice(u, token);
    res.set("Set-Cookie", std::string(kCookie) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    ok(res);
}

void App::phone(const Request& req, Response& res) {
    std::string token = deviceToken(req);
    UserPtr u = store_.byDeviceToken(token);
    if (!u) return res.error(401, startsWith(req.path, "/media/") ? "Nicht gekoppelt" : kNoDevice);
    // fdapi::handle prüft das Token selbst: ein Auth nur für diese Anfrage mit genau diesem (schon geprüften) Gerät
    fdapi::Auth one;
    fdapi::Device d;
    d.token = token;
    d.created = d.lastSeen = nowEpoch();
    one.setDevices({d});
    fdapi::Host h = host(u);
    h.auth = &one;
    fdapi::handle(h, req, res);
}

}  // namespace fdsrv
