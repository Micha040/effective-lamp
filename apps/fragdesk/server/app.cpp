// fragdesk-server – alle Anfragen (siehe app.h)
#include "app.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>

#include "../src/api/jsonw.h"
#include "../src/core/json.h"
#include "../src/core/model.h"
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

fdapi::FileReader openReader(const std::string& path, int64_t& size) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return nullptr;
    }
    size = int64_t(st.st_size);
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
        // Schreibende Anfragen nur mit eigenem Kopf (Schutz gegen fremde Seiten im selben Browser)
        if (!read && req.header("x-fragdesk") != "1") return res.error(403, "Nicht erlaubt");
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
        if (startsWith(p, "/api/v1/sync/match/")) return kMaxMatchFile;
    }
    return kMaxJson;
}

void App::maintain() {
    store_.maintain();
    views_.unloadIdle(idleUnload);
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
    h.openFile = openReader;
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
        for (const auto& kv : mf)
            if (kv.second.json >= 0 || kv.second.heat >= 0)
                j.key(kv.first.c_str()).obj().ki("json", std::max<int64_t>(0, kv.second.json)).ki("heat", std::max<int64_t>(0, kv.second.heat)).end();
        j.end().key("clips").obj();
        for (const auto& kv : cf) {
            const ClipRec& c = kv.second;
            if (c.mp4 < 0 && c.part < 0 && c.thumb < 0) continue;
            j.key(kv.first.c_str()).obj().ki("size", c.mp4 >= 0 ? c.mp4 : std::max<int64_t>(0, c.part)).kb("complete", c.complete()).kb("thumb", c.thumb >= 0).end();
        }
        j.end().key("quota").obj().ki("used", store_.used(u)).ki("limit", store_.limit(u)).end();
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
        bool heat = part[2] == "heat";
        if (!heat && part[2] != "json") return res.error(404, "Unbekannte Anfrage");
        if (m != "PUT") return res.error(405, "Nur PUT");
        if (int64_t(req.body.size()) > kMaxMatchFile) return res.error(413, "Die Datei ist zu groß (höchstens 4 MB).");
        if (heat) {
            if (req.body.size() < 8 || req.body.compare(0, 4, "FDHM") != 0) return res.error(400, "Das ist keine Heatmap-Datei von FragDesk.");
        } else {
            fd::Match match;
            std::string err;
            if (!fd::parseMatch(req.body, match, err, true)) return res.error(400, "Das Match lässt sich nicht lesen: " + err);
            if (match.checksum != checksum) return res.error(400, "Die Prüfsumme passt nicht zum Match.");
        }
        Fail f = store_.putMatchFile(u, checksum, heat, req.body);
        if (!f.ok()) return fail(res, f);
        if (!heat) views_.matchChanged(u, checksum);
        JsonW j;
        j.obj().kb("ok", true).ki("size", int64_t(req.body.size())).end();
        res.body = j.take();
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
    if (sub == "invite") return createInvite(req, res, std::string());
    if (sub == "users") {
        if (req.method != "GET") return res.error(405, "Nur GET");
        return listUsers(res);
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
