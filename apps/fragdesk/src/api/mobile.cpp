#include "mobile.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

#include "../core/json.h"
#include "../core/radar.h"
#include "jsonw.h"
#include "views.h"

namespace fdapi {

// ---- Hilfen -----------------------------------------------------------------------------------------------------
int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string randomHex(int bytes) {
    std::vector<unsigned char> b(static_cast<size_t>(bytes));
    bool ok = false;
#ifdef _WIN32
    ok = BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    ok = getrandom(b.data(), b.size(), 0) == ssize_t(b.size());
    if (!ok) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            ok = read(fd, b.data(), b.size()) == ssize_t(b.size());
            close(fd);
        }
    }
#endif
    if (!ok) return std::string();   // ohne Zufall keine Anmeldung
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char ch : b) {
        out += hex[ch >> 4];
        out += hex[ch & 15];
    }
    return out;
}

namespace {

uint64_t fnv1a(const std::string& s, bool fold) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : s) {
        if (fold && ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch + 32);
        h ^= ch;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex64(uint64_t v) {
    char b[24];
    snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool sameSecret(const std::string& a, const std::string& b) {   // Vergleich ohne frühes Abbrechen
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

std::string deviceName(const std::string& wanted, const std::string& agent) {
    std::string n;
    for (char ch : wanted)
        if (static_cast<unsigned char>(ch) >= 0x20 && n.size() < 40) n += ch;
    if (!n.empty()) return n;
    if (agent.find("iPhone") != std::string::npos) return "iPhone";
    if (agent.find("iPad") != std::string::npos) return "iPad";
    if (agent.find("Android") != std::string::npos) return "Android";
    return "Gerät";
}

const char* kCookie = "fdk";

}  // namespace

std::string clipId(const std::string& utf8Path) {
#ifdef _WIN32
    return hex64(fnv1a(utf8Path, true));   // Windows-Pfade ohne Rücksicht auf Groß-/Kleinschreibung
#else
    return hex64(fnv1a(utf8Path, false));
#endif
}

std::string contentType(const std::string& path) {
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "js") return "text/javascript; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "webmanifest") return "application/manifest+json";
    if (ext == "png") return "image/png";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "ico") return "image/x-icon";
    if (ext == "mp4") return "video/mp4";
    if (ext == "woff2") return "font/woff2";
    return "application/octet-stream";
}

// ---- Auth -------------------------------------------------------------------------------------------------------
void Auth::setDevices(std::vector<Device> devices) {
    std::lock_guard<std::mutex> lk(mu_);
    devices_ = std::move(devices);
}

std::vector<Device> Auth::devices() const {
    std::lock_guard<std::mutex> lk(mu_);
    return devices_;
}

bool Auth::remove(const std::string& token) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = std::remove_if(devices_.begin(), devices_.end(), [&](const Device& d) { return d.token == token; });
        found = it != devices_.end();
        devices_.erase(it, devices_.end());
    }
    if (found && onChange) onChange();
    return found;
}

void Auth::removeAll() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        devices_.clear();
    }
    if (onChange) onChange();
}

std::string Auth::startPairing(int minutes, const std::string& fixed) {
    std::string r = randomHex(4);
    if (r.empty()) return std::string();
    unsigned long v = std::stoul(r, nullptr, 16) % 1000000ul;
    char b[8];
    snprintf(b, sizeof b, "%06lu", v);
    std::lock_guard<std::mutex> lk(mu_);
    code_ = fixed.empty() ? std::string(b) : fixed;
    codeUntil_ = nowEpoch() + minutes * 60;
    attempts_ = 0;
    return code_;
}

void Auth::stopPairing() {
    std::lock_guard<std::mutex> lk(mu_);
    code_.clear();
    codeUntil_ = 0;
}

std::string Auth::pairingCode() const {
    std::lock_guard<std::mutex> lk(mu_);
    return nowEpoch() <= codeUntil_ ? code_ : std::string();
}

int64_t Auth::pairingUntil() const {
    std::lock_guard<std::mutex> lk(mu_);
    return code_.empty() ? 0 : codeUntil_;
}

bool Auth::check(const std::string& token) {
    if (token.size() < 32) return false;
    bool ok = false, save = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& d : devices_)
            if (sameSecret(d.token, token)) {
                ok = true;
                int64_t now = nowEpoch();
                if (now - d.lastSeen > 3600) {   // höchstens stündlich speichern
                    d.lastSeen = now;
                    save = true;
                }
            }
    }
    if (save && onChange) onChange();
    return ok;
}

std::string Auth::pair(const std::string& code, const std::string& name, std::string& err, bool& tooMany) {
    tooMany = false;
    std::string token;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (code_.empty() || nowEpoch() > codeUntil_) {
            err = "Gerade ist kein Code aktiv. Tippe in FragDesk unter Einstellungen › Handy-App auf „Handy verbinden“.";
            return std::string();
        }
        if (!sameSecret(code, code_)) {
            if (++attempts_ >= 5) {
                code_.clear();
                tooMany = true;
                err = "Zu viele falsche Versuche. Starte in FragDesk „Handy verbinden“ neu.";
            } else err = "Der Code stimmt nicht.";
            return std::string();
        }
        token = randomHex(32);
        if (token.empty()) {
            err = "Kein Zufallsgenerator verfügbar.";
            return std::string();
        }
        Device d;
        d.token = token;
        d.name = name;
        d.created = d.lastSeen = nowEpoch();
        devices_.push_back(d);
        if (devices_.size() > 20) devices_.erase(devices_.begin());   // älteste zuerst raus
    }
    if (onChange) onChange();
    return token;
}

// ---- Routen -----------------------------------------------------------------------------------------------------
namespace {

std::string tokenOf(const Request& req) {
    std::string t = req.cookie(kCookie);
    if (t.empty()) {
        std::string a = req.header("authorization");
        if (a.compare(0, 7, "Bearer ") == 0) t = a.substr(7);
    }
    return t;
}

bool validAssetPath(const std::string& p) {
    if (p.empty() || p.size() > 120 || p[0] == '/' || p.find("..") != std::string::npos) return false;
    for (char ch : p)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == '_' || ch == '/')) return false;
    return true;
}

void serveAsset(Host& host, const Request& req, Response& res, std::string path) {
    if (path.empty()) path = "index.html";
    std::string body;
    if (!validAssetPath(path) || !host.asset || !host.asset(path, body)) {
        res.error(404, "Gibt es nicht");
        return;
    }
    std::string etag = "\"" + hex64(fnv1a(body, false)) + "\"";
    res.set("ETag", etag);
    res.set("Cache-Control", "no-cache");
    res.type = contentType(path);
    if (path == "index.html") {
        res.set("Content-Security-Policy",
                "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; style-src 'self' 'unsafe-inline'; "
                "script-src 'self'; connect-src 'self'; manifest-src 'self'; worker-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
                "form-action 'none'");
        res.set("X-Frame-Options", "DENY");
    }
    if (req.header("if-none-match") == etag) {
        res.status = 304;
        return;
    }
    res.body = std::move(body);
}

bool findClip(Host& host, const std::string& id, ClipInfo& out) {
    bool found = false;
    bool ok = host.withSource([&](Source& src) {
        for (auto& c : src.clips())
            if (c.id == id) {
                out = c;
                found = true;
                break;
            }
    });
    return ok && found;
}

std::string encodeFileName(const std::string& s) {   // RFC 5987
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') out += char(ch);
        else {
            out += '%';
            out += hex[ch >> 4];
            out += hex[ch & 15];
        }
    }
    return out;
}

// /media/radar/<karte>.png, /media/radar/<karte>_lower.png – Radarbilder für Heatmap und 2D-Ansicht. Ändern sich
// nur mit einer neuen Version (die Adressen tragen ?v=<Version>), dürfen also lange im Speicher des Browsers bleiben.
void serveRadar(Host& host, const Request& req, Response& res) {
    std::string name = req.path.substr(13);
    bool lower = false;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0) name.resize(name.size() - 4);
    else return res.error(404, "Gibt es nicht");
    if (name.size() > 6 && name.compare(name.size() - 6, 6, "_lower") == 0) {
        name.resize(name.size() - 6);
        lower = true;
    }
    const fd::RadarMap* m = name.size() <= 40 ? fd::radarMap(name) : nullptr;
    std::string png;
    if (!m || name != m->name || (lower && !m->lower) || !host.radar || !host.radar(m->name, lower, png)) return res.error(404, "Kein Radarbild");
    std::string etag = "\"" + hex64(fnv1a(png, false)) + "\"";
    res.set("ETag", etag);
    res.set("Cache-Control", "private, max-age=2592000");
    if (req.header("if-none-match") == etag) {
        res.status = 304;
        return;
    }
    res.type = "image/png";
    res.body = std::move(png);
}

void serveMedia(Host& host, const Request& req, Response& res) {
    const std::string& p = req.path;
    if (p.compare(0, 13, "/media/radar/") == 0) return serveRadar(host, req, res);
    bool clip = p.compare(0, 12, "/media/clip/") == 0, thumb = p.compare(0, 13, "/media/thumb/") == 0;
    if (!clip && !thumb) {
        res.error(404, "Gibt es nicht");
        return;
    }
    std::string id = p.substr(clip ? 12 : 13);
    if (id.size() > 4 && id.compare(id.size() - 4, 4, ".jpg") == 0) id.resize(id.size() - 4);
    if (id.size() > 4 && id.compare(id.size() - 4, 4, ".mp4") == 0) id.resize(id.size() - 4);
    std::string blocked = host.mediaBlocked ? host.mediaBlocked() : std::string();
    if (!blocked.empty()) {
        res.error(503, blocked);
        res.set("Retry-After", "60");
        return;
    }
    ClipInfo ci;
    if (!findClip(host, id, ci)) {
        res.error(404, "Das Video gibt es nicht mehr.");
        return;
    }
    if (thumb) {
        std::string jpeg;
        if (!host.thumbnail || !host.thumbnail(ci, jpeg)) {
            res.error(404, "Kein Vorschaubild");
            return;
        }
        res.type = "image/jpeg";
        res.body = std::move(jpeg);
        res.set("Cache-Control", "private, max-age=86400");
        return;
    }
    int64_t size = 0;
    FileReader reader = host.openFile ? host.openFile(ci.path, size) : nullptr;
    if (!reader) {
        res.error(404, "Das Video lässt sich nicht öffnen.");
        return;
    }
    res.type = "video/mp4";
    res.file = std::move(reader);
    res.fileSize = size;
    res.set("Cache-Control", "private, max-age=3600");
    if (req.param("dl") == "1") {
        std::string name = ci.title.empty() ? "FragDesk.mp4" : ci.title + ".mp4";
        res.set("Content-Disposition", "attachment; filename=\"FragDesk.mp4\"; filename*=UTF-8''" + encodeFileName(name));
    }
}

}  // namespace

bool readAll(Host& host, const std::string& path, std::string& out, int64_t maxBytes) {
    out.clear();
    int64_t size = 0;
    FileReader rd = path.empty() || !host.openFile ? nullptr : host.openFile(path, size);
    if (!rd || size < 0 || size > maxBytes) return false;
    out.resize(static_cast<size_t>(size));
    int64_t got = 0;
    while (got < size) {
        int64_t n = rd(got, &out[static_cast<size_t>(got)], size - got);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

namespace {

void sessionJson(Host& host, Response& res) {
    JsonW j;
    j.obj().kb("ok", true).ki("api", kApiVersion).kv("version", host.version).kv("kind", host.kind).end();
    res.body = j.take();
}

}  // namespace

void handle(Host& host, const Request& req, Response& res) {
    const std::string& p = req.path;
    bool get = req.method == "GET" || req.method == "HEAD";

    if (p.compare(0, 5, "/api/") == 0) {
        res.set("Cache-Control", "no-store");
        bool post = req.method == "POST";
        // Schreibende Anfragen nur mit eigenem Kopf (Schutz gegen fremde Seiten im selben Browser)
        if (post && req.header("x-fragdesk") != "1") {
            res.error(403, "Nicht erlaubt");
            return;
        }
        if (p == "/api/v1/pair") {
            if (!post) return res.error(405, "Nur POST");
            if (!host.auth) return res.error(503, "Kopplung nicht verfügbar");
            json::Value v;
            json::parse(req.body, v);
            std::string err;
            bool tooMany = false;
            std::string token = host.auth->pair(v.getString("code"), deviceName(v.getString("name"), req.header("user-agent")), err, tooMany);
            if (token.empty()) return res.error(tooMany ? 429 : 403, err);
            res.set("Set-Cookie", std::string(kCookie) + "=" + token + "; Path=/; Max-Age=315360000; HttpOnly; SameSite=Strict");
            sessionJson(host, res);
            return;
        }
        std::string token = tokenOf(req);
        bool authed = host.auth && host.auth->check(token);
        if (!authed) {
            res.error(401, "Dieses Handy ist nicht (mehr) mit FragDesk gekoppelt.");
            return;
        }
        if (p == "/api/v1/session") {
            sessionJson(host, res);
            return;
        }
        if (p == "/api/v1/logout") {
            if (!post) return res.error(405, "Nur POST");
            host.auth->remove(token);
            res.set("Set-Cookie", std::string(kCookie) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
            res.body = "{\"ok\":true}";
            return;
        }
        if (!get) return res.error(405, "Nur GET");
        bool ok = host.withSource([&](Source& src) {
            ViewCtx c{src, req, host, src.me()};
            if (p == "/api/v1/info") viewInfo(c, res);
            else if (p == "/api/v1/overview") viewOverview(c, res);
            else if (p == "/api/v1/matches") viewMatches(c, res);
            else if (p.compare(0, 14, "/api/v1/match/") == 0) viewMatch(c, res, p.substr(14));
            else if (p == "/api/v1/stats") viewStats(c, res);
            else if (p == "/api/v1/moments") viewMoments(c, res);
            else res.error(404, "Unbekannte Anfrage");
        });
        if (!ok) res.error(503, "FragDesk ist gerade nicht bereit.");
        return;
    }

    if (!get) {
        res.error(405, "Nur GET");
        return;
    }
    if (p.compare(0, 7, "/media/") == 0) {
        if (!host.auth || !host.auth->check(tokenOf(req))) {
            res.error(401, "Nicht gekoppelt");
            return;
        }
        serveMedia(host, req, res);
        return;
    }
    serveAsset(host, req, res, p.substr(1));
}

}  // namespace fdapi
