// fragdesk-server – Konten, Einladungen, Schlüssel und Dateien (siehe store.h)
#include "store.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

#include "../src/api/jsonw.h"
#include "../src/core/format.h"
#include "../src/core/json.h"
#include "files.h"
#include "sha256.h"

namespace fdsrv {

using fdapi::JsonW;
using fdapi::nowEpoch;
using fdapi::randomHex;

namespace {

bool hexOnly(const std::string& s) {
    for (char ch : s)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool sameSecret(const std::string& a, const std::string& b) {   // Vergleich ohne frühes Abbrechen
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

std::string cleanSteamId(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (ch >= '0' && ch <= '9' && out.size() < 20) out += ch;
    return out;
}

const char* kGone = "Dieses Konto gibt es nicht mehr.";

}  // namespace

bool validChecksum(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) return false;
    return true;
}

bool validClipId(const std::string& s) { return s.size() == 16 && hexOnly(s); }
bool validUserId(const std::string& s) { return s.size() == 8 && hexOnly(s); }

std::string cleanText(const std::string& s, size_t maxBytes) {
    std::string out;
    for (unsigned char ch : s)
        if (ch >= 0x20 && ch != 0x7f) out += char(ch);
    out = trim(out);
    if (out.size() > maxBytes) {
        size_t n = maxBytes;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;   // nicht mitten im Zeichen
        out = trim(out.substr(0, n));
    }
    return out;
}

std::string quotaText(int64_t used, int64_t limit) { return fd::fmtBytes(double(used)) + " von " + fd::fmtBytes(double(limit)); }

// ---- Laden ------------------------------------------------------------------------------------------------------
bool Store::open(const std::string& dataDir, std::string& err) {
    dir_ = dataDir;
    while (dir_.size() > 1 && dir_.back() == '/') dir_.pop_back();
    if (!makeDirs(dir_, 0700) || !makeDirs(joinPath(dir_, "users"), 0700)) {
        err = "Der Datenordner " + dir_ + " lässt sich nicht anlegen: " + lastError();
        return false;
    }
    std::string keyPath = joinPath(dir_, "local.key"), key;
    if (readFile(keyPath, key, 4096)) key = trim(key);
    if (key.size() != 64 || !hexOnly(key)) {
        key = randomHex(32);
        if (key.empty() || !writeAtomic(keyPath, key + "\n", 0600)) {
            err = "local.key lässt sich nicht schreiben: " + lastError();
            return false;
        }
    }
    localKey_ = key;
    std::lock_guard<std::mutex> lk(mu_);
    if (!loadServer(err)) return false;
    for (const std::string& n : listDir(dir_))
        if (n.find(".tmp-") != std::string::npos) removeFile(joinPath(dir_, n));   // Reste eines Absturzes
    for (auto& kv : users_) {
        if (!loadAccount(kv.second, err)) return false;
        scanFiles(kv.second);
    }
    for (const std::string& n : listDir(joinPath(dir_, "users")))
        if (!users_.count(n)) std::cerr << "Hinweis: users/" << n << " gehört zu keinem Konto (wird nicht benutzt).\n";
    return true;
}

bool Store::loadServer(std::string& err) {
    std::string path = joinPath(dir_, "server.json"), text;
    if (fileSize(path) < 0) {   // erster Start
        if (!saveServer()) {
            err = "server.json lässt sich nicht schreiben: " + lastError();
            return false;
        }
        return true;
    }
    json::Value v;
    std::string perr;
    if (!readFile(path, text, 64 << 20)) {
        err = "server.json lässt sich nicht lesen: " + lastError();
        return false;
    }
    if (!json::parse(text, v, &perr) || !v.isObject()) {
        err = "server.json ist beschädigt (" + perr + "). Der Server startet nicht, damit nichts verloren geht.";
        return false;
    }
    const json::Value& s = v["settings"];
    if (s.isObject()) {
        settings_.url = s.getString("url", settings_.url);
        settings_.listen = s.getString("listen", settings_.listen);
        settings_.defaultLimit = int64_t(s.getNumber("defaultLimit", double(kDefaultLimit)));
    }
    for (const auto& e : v["users"].arr) {
        std::string id = e.getString("id");
        if (!validUserId(id) || users_.count(id)) continue;
        auto u = std::make_shared<User>();
        u->id = id;
        u->admin = e.getBool("admin");
        u->limit = std::max<int64_t>(0, int64_t(e.getNumber("limit")));
        u->created = int64_t(e.getNumber("created"));
        u->lastSeen = int64_t(e.getNumber("lastSeen"));
        users_[id] = u;
    }
    for (const auto& e : v["invites"].arr) {
        Invite inv;
        inv.id = e.getString("id");
        inv.hash = e.getString("hash");
        inv.by = e.getString("by");
        inv.admin = e.getBool("admin");
        inv.created = int64_t(e.getNumber("created"));
        inv.expires = int64_t(e.getNumber("expires"));
        if (inv.hash.size() == 64 && !inv.id.empty()) invites_.push_back(inv);
    }
    return true;
}

bool Store::loadAccount(const UserPtr& u, std::string& err) {
    std::string base = userDir(u->id);
    if (!makeDirs(joinPath(base, "matches")) || !makeDirs(joinPath(base, "clips"))) {
        err = "Der Ordner " + base + " lässt sich nicht anlegen: " + lastError();
        return false;
    }
    std::string path = joinPath(base, "account.json"), text;
    json::Value v;
    if (fileSize(path) < 0) {   // sollte es nicht geben – Konto ohne Schlüssel, nur noch für den Admin sichtbar
        u->name = "Spieler";
        if (saveAccount(u)) return true;
        err = "account.json lässt sich nicht schreiben: " + lastError();
        return false;
    }
    std::string perr;
    if (!readFile(path, text, 16 << 20) || !json::parse(text, v, &perr) || !v.isObject()) {
        err = path + " ist beschädigt (" + perr + "). Der Server startet nicht, damit nichts verloren geht.";
        return false;
    }
    const json::Value& p = v["profile"];
    u->name = p.getString("name", "Spieler");
    u->steamId = p.getString("steamId");
    for (const auto& t : v["tokens"].arr) {
        TokenRec r;
        r.hash = t.getString("hash");
        r.client = t.getString("client");
        r.created = int64_t(t.getNumber("created"));
        r.lastSeen = int64_t(t.getNumber("lastSeen"));
        if (r.hash.size() != 64) continue;
        accountTokens_[r.hash] = u->id;
        u->tokens.push_back(r);
    }
    for (const auto& d : v["devices"].arr) {
        DeviceRec r;
        r.id = d.getString("id");
        r.hash = d.getString("hash");
        r.name = d.getString("name");
        r.created = int64_t(d.getNumber("created"));
        r.lastSeen = int64_t(d.getNumber("lastSeen"));
        if (r.hash.size() != 64 || r.id.empty()) continue;
        deviceTokens_[r.hash] = u->id;
        u->devices.push_back(r);
    }
    return true;
}

void Store::scanFiles(const UserPtr& u) {
    u->matches.clear();
    u->clips.clear();
    std::string mdir = joinPath(userDir(u->id), "matches"), cdir = joinPath(userDir(u->id), "clips");
    for (const std::string& n : listDir(mdir)) {
        std::string path = joinPath(mdir, n);
        if (n.find(".tmp-") != std::string::npos) {
            removeFile(path);
            continue;
        }
        bool json = endsWith(n, ".json"), heat = endsWith(n, ".heat");
        std::string c = n.substr(0, n.size() - 5);
        if ((!json && !heat) || !validChecksum(c)) continue;
        (json ? u->matches[c].json : u->matches[c].heat) = fileSize(path);
    }
    for (const std::string& n : listDir(cdir)) {
        std::string path = joinPath(cdir, n);
        if (n.find(".tmp-") != std::string::npos) {
            removeFile(path);
            continue;
        }
        size_t dot = n.find('.');
        if (dot == std::string::npos || !validClipId(n.substr(0, dot))) continue;
        std::string id = n.substr(0, dot), ext = n.substr(dot);
        if (ext == ".mp4") u->clips[id].mp4 = fileSize(path);
        else if (ext == ".part") u->clips[id].part = fileSize(path);
        else if (ext == ".jpg") u->clips[id].thumb = fileSize(path);
    }
    std::string text;
    json::Value v;
    if (readFile(joinPath(cdir, "clips.json"), text, 64 << 20) && json::parse(text, v) && v.isArray()) {
        for (const auto& e : v.arr) {
            auto it = u->clips.find(e.getString("id"));
            if (it == u->clips.end() || it->second.mp4 < 0) continue;   // Video weg: Eintrag verwerfen
            ClipMeta& m = it->second.meta;
            m.title = e.getString("title");
            m.match = e.getString("match");
            m.highlight = e.getString("highlight");
            m.map = e.getString("map");
            m.created = int64_t(e.getNumber("created"));
            m.seconds = e.getNumber("seconds");
            it->second.hasMeta = true;
        }
    }
    recount(u);
}

void Store::recount(const UserPtr& u) {
    int64_t sum = 0;
    for (const auto& kv : u->matches) sum += std::max<int64_t>(0, kv.second.json) + std::max<int64_t>(0, kv.second.heat);
    for (const auto& kv : u->clips)
        sum += std::max<int64_t>(0, kv.second.mp4) + std::max<int64_t>(0, kv.second.part) + std::max<int64_t>(0, kv.second.thumb);
    u->used = sum;
}

// ---- Speichern (immer mit mu_) ----------------------------------------------------------------------------------
bool Store::saveServer() {
    JsonW j;
    j.obj().ki("format", 1);
    j.key("settings").obj().kv("url", settings_.url).kv("listen", settings_.listen).ki("defaultLimit", settings_.defaultLimit).end();
    j.key("users").arr();
    for (const auto& kv : users_) {
        const User& u = *kv.second;
        j.obj().kv("id", u.id).kb("admin", u.admin).ki("limit", u.limit).ki("created", u.created).ki("lastSeen", u.lastSeen).end();
    }
    j.end();
    j.key("invites").arr();
    for (const Invite& i : invites_)
        j.obj().kv("id", i.id).kv("hash", i.hash).kb("admin", i.admin).kv("by", i.by).ki("created", i.created).ki("expires", i.expires).end();
    j.end().end();
    bool ok = writeAtomic(joinPath(dir_, "server.json"), j.take() + "\n");
    if (ok) serverDirty_ = false;
    else std::cerr << "Fehler: server.json lässt sich nicht speichern: " << lastError() << "\n";
    return ok;
}

bool Store::saveAccount(const UserPtr& u) {
    JsonW j;
    j.obj().ki("format", 1).kv("id", u->id);
    j.key("profile").obj().kv("name", u->name).kv("steamId", u->steamId).end();
    j.key("tokens").arr();
    for (const TokenRec& t : u->tokens)
        j.obj().kv("hash", t.hash).kv("client", t.client).ki("created", t.created).ki("lastSeen", t.lastSeen).end();
    j.end();
    j.key("devices").arr();
    for (const DeviceRec& d : u->devices)
        j.obj().kv("id", d.id).kv("hash", d.hash).kv("name", d.name).ki("created", d.created).ki("lastSeen", d.lastSeen).end();
    j.end().end();
    bool ok = writeAtomic(joinPath(userDir(u->id), "account.json"), j.take() + "\n");
    if (ok) u->dirty = false;
    else std::cerr << "Fehler: account.json von " << u->id << " lässt sich nicht speichern: " << lastError() << "\n";
    return ok;
}

bool Store::saveClips(const UserPtr& u) {
    JsonW j;
    j.arr();
    for (const auto& kv : u->clips) {
        const ClipRec& c = kv.second;
        if (!c.complete()) continue;
        j.obj()
            .kv("id", kv.first)
            .kv("title", c.meta.title)
            .kv("match", c.meta.match)
            .kv("highlight", c.meta.highlight)
            .kv("map", c.meta.map)
            .ki("created", c.meta.created)
            .kn("seconds", c.meta.seconds)
            .ki("size", c.mp4)
            .end();
    }
    j.end();
    return writeAtomic(joinPath(joinPath(userDir(u->id), "clips"), "clips.json"), j.take() + "\n");
}

void Store::touch(User& u, int64_t* tokenSeen) {
    int64_t now = nowEpoch();
    if (now - u.lastSeen >= 60) {
        u.lastSeen = now;
        serverDirty_ = true;
    }
    if (tokenSeen && now - *tokenSeen >= 3600) {   // höchstens stündlich
        *tokenSeen = now;
        u.dirty = true;
    }
}

bool Store::alive(const UserPtr& u) const { return u && !u->deleted; }

// ---- Einstellungen ------------------------------------------------------------------------------------------------
Settings Store::settings() const {
    std::lock_guard<std::mutex> lk(mu_);
    return settings_;
}

void Store::setUrl(const std::string& url) {
    std::string u = url;
    if (!u.empty() && u.back() != '/') u += '/';
    std::lock_guard<std::mutex> lk(mu_);
    if (u.empty() || u == settings_.url) return;
    settings_.url = u;
    saveServer();
}

void Store::setListen(const std::string& listen) {
    std::lock_guard<std::mutex> lk(mu_);
    if (listen == settings_.listen) return;
    settings_.listen = listen;
    saveServer();
}

// ---- Anmeldung --------------------------------------------------------------------------------------------------
UserPtr Store::byAccountToken(const std::string& token) {
    if (token.size() != 64) return nullptr;   // eigene Tokens: immer 32 Bytes als Hex
    std::string h = sha256Hex(token);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = accountTokens_.find(h);
    if (it == accountTokens_.end()) return nullptr;
    auto u = users_.find(it->second);
    if (u == users_.end() || u->second->deleted) return nullptr;
    for (TokenRec& t : u->second->tokens)
        if (t.hash == h) touch(*u->second, &t.lastSeen);
    return u->second;
}

bool Store::validAccountToken(const std::string& token) const {
    if (token.size() != 64) return false;
    std::string h = sha256Hex(token);
    std::lock_guard<std::mutex> lk(mu_);
    return accountTokens_.count(h) > 0;
}

UserPtr Store::byDeviceToken(const std::string& token) {
    if (token.size() != 64) return nullptr;
    std::string h = sha256Hex(token);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = deviceTokens_.find(h);
    if (it == deviceTokens_.end()) return nullptr;
    auto u = users_.find(it->second);
    if (u == users_.end() || u->second->deleted) return nullptr;
    for (DeviceRec& d : u->second->devices)
        if (d.hash == h) touch(*u->second, &d.lastSeen);
    return u->second;
}

UserPtr Store::byId(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = users_.find(id);
    return it == users_.end() ? nullptr : it->second;
}

UserInfo Store::info(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    UserInfo i;
    i.id = u->id;
    i.name = u->name;
    i.steamId = u->steamId;
    i.admin = u->admin;
    i.limit = u->limit;
    i.used = u->used;
    i.created = u->created;
    i.lastSeen = u->lastSeen;
    for (const auto& kv : u->matches)
        if (kv.second.json >= 0) ++i.matches;
    for (const auto& kv : u->clips)
        if (kv.second.complete()) ++i.clips;
    i.devices = u->devices;
    for (DeviceRec& d : i.devices) d.hash.clear();
    return i;
}

std::vector<UserInfo> Store::users() const {
    std::vector<UserPtr> list;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& kv : users_) list.push_back(kv.second);
    }
    std::vector<UserInfo> out;
    for (const UserPtr& u : list) out.push_back(info(u));
    std::stable_sort(out.begin(), out.end(), [](const UserInfo& a, const UserInfo& b) {
        return a.created != b.created ? a.created < b.created : a.name != b.name ? a.name < b.name : a.id < b.id;
    });
    return out;
}

int Store::userCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return int(users_.size());
}

std::string Store::profileId(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->steamId;
}

std::string Store::profileName(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->name;
}

// ---- Konten -----------------------------------------------------------------------------------------------------
Fail Store::registerUser(const std::string& code, const std::string& name, const std::string& steamId, const std::string& client,
                         UserPtr& user, std::string& token) {
    const char* invalid = "Die Einladung gilt nicht (mehr). Lass dir einen neuen Link schicken – jeder Link gilt nur einmal.";
    if (code.size() < 16 || code.size() > 128) return Fail::make(403, invalid);
    std::string h = sha256Hex(code);
    std::lock_guard<std::mutex> lk(mu_);
    auto inv = std::find_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return sameSecret(i.hash, h); });
    if (inv == invites_.end()) return Fail::make(403, invalid);
    int64_t now = nowEpoch();
    if (inv->expires < now) {
        invites_.erase(inv);
        saveServer();
        return Fail::make(403, "Die Einladung ist abgelaufen. Lass dir einen neuen Link schicken.");
    }
    token = randomHex(32);
    std::string id;
    for (int i = 0; i < 20 && (id.empty() || users_.count(id)); ++i) id = randomHex(4);
    if (token.empty() || id.empty() || users_.count(id)) return Fail::make(500, "Kein Zufallsgenerator verfügbar.");
    auto u = std::make_shared<User>();
    u->id = id;
    u->name = cleanText(name, 40);
    if (u->name.empty()) u->name = "Spieler";
    u->steamId = cleanSteamId(steamId);
    u->admin = inv->admin;
    u->limit = inv->admin ? 0 : settings_.defaultLimit;
    u->created = u->lastSeen = now;
    TokenRec t;
    t.hash = sha256Hex(token);
    t.client = cleanText(client, 60);
    t.created = t.lastSeen = now;
    u->tokens.push_back(t);
    std::string base = userDir(id);
    if (!makeDirs(joinPath(base, "matches")) || !makeDirs(joinPath(base, "clips")) || !saveAccount(u)) {
        removeTree(base);
        return Fail::make(500, "Das Konto lässt sich auf dem Server nicht anlegen: " + lastError());
    }
    Invite used = *inv;
    invites_.erase(inv);   // ein Code gilt einmal
    users_[id] = u;
    if (!saveServer()) {   // zurück auf den alten Stand
        users_.erase(id);
        invites_.push_back(used);
        removeTree(base);
        return Fail::make(500, "Das Konto lässt sich auf dem Server nicht speichern.");
    }
    accountTokens_[t.hash] = id;
    user = u;
    std::cerr << "Neues Konto " << id << (u->admin ? " (Admin)" : "") << "\n";
    return Fail();
}

Fail Store::setProfile(const UserPtr& u, const std::string& name, const std::string& steamId) {
    std::string n = cleanText(name, 40);
    if (n.empty()) return Fail::make(400, "Der Name fehlt.");
    std::lock_guard<std::mutex> lk(mu_);
    if (!alive(u)) return Fail::make(401, kGone);
    u->name = n;
    u->steamId = cleanSteamId(steamId);
    if (!saveAccount(u)) return Fail::make(500, "Das Profil lässt sich nicht speichern.");
    return Fail();
}

bool Store::logoutToken(const UserPtr& u, const std::string& token) {
    std::string h = sha256Hex(token);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::remove_if(u->tokens.begin(), u->tokens.end(), [&](const TokenRec& t) { return t.hash == h; });
    if (it == u->tokens.end()) return false;
    u->tokens.erase(it, u->tokens.end());
    accountTokens_.erase(h);
    saveAccount(u);
    return true;
}

Fail Store::setLimit(const std::string& id, int64_t limit) {
    if (limit < 0 || limit > (int64_t(1) << 50)) return Fail::make(400, "Ungültiges Limit.");
    std::lock_guard<std::mutex> lk(mu_);
    auto it = users_.find(id);
    if (it == users_.end()) return Fail::make(404, kGone);
    it->second->limit = limit;
    if (!saveServer()) return Fail::make(500, "Das Limit lässt sich nicht speichern.");
    return Fail();
}

Fail Store::removeUser(const std::string& id) {
    UserPtr u;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = users_.find(id);
        if (it == users_.end()) return Fail::make(404, kGone);
        u = it->second;
        int admins = 0;
        for (const auto& kv : users_) admins += kv.second->admin ? 1 : 0;
        if (u->admin && admins <= 1) return Fail::make(409, "Das ist der letzte Admin. Lade erst einen zweiten Admin ein.");
        u->deleted = true;
        users_.erase(it);
        for (const TokenRec& t : u->tokens) accountTokens_.erase(t.hash);
        for (const DeviceRec& d : u->devices) deviceTokens_.erase(d.hash);
        saveServer();
    }
    std::lock_guard<std::mutex> fl(u->fileMu);   // laufendes Hochladen erst fertig werden lassen
    if (!removeTree(userDir(id))) std::cerr << "Fehler: users/" << id << " lässt sich nicht löschen: " << lastError() << "\n";
    std::cerr << "Konto " << id << " entfernt\n";
    return Fail();
}

// ---- Einladungen --------------------------------------------------------------------------------------------------
Fail Store::createInvite(bool admin, int days, const std::string& by, Invite& out, std::string& code) {
    days = std::max(1, std::min(30, days));
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = nowEpoch();
    invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.expires < now; }), invites_.end());
    if (int(invites_.size()) >= kMaxInvites) return Fail::make(429, "Zu viele offene Einladungen. Zieh erst ein paar alte zurück.");
    code = randomHex(16);
    Invite inv;
    for (int i = 0; i < 20 && (inv.id.empty() || std::any_of(invites_.begin(), invites_.end(), [&](const Invite& x) { return x.id == inv.id; })); ++i)
        inv.id = randomHex(4);
    if (code.empty() || inv.id.empty()) return Fail::make(500, "Kein Zufallsgenerator verfügbar.");
    inv.hash = sha256Hex(code);
    inv.by = by;
    inv.admin = admin;
    inv.created = now;
    inv.expires = now + int64_t(days) * 86400;
    invites_.push_back(inv);
    if (!saveServer()) {
        invites_.pop_back();
        return Fail::make(500, "Die Einladung lässt sich nicht speichern.");
    }
    out = inv;
    return Fail();
}

std::vector<Invite> Store::invites() {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = nowEpoch();
    size_t before = invites_.size();
    invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.expires < now; }), invites_.end());
    if (invites_.size() != before) saveServer();
    std::vector<Invite> out = invites_;
    std::stable_sort(out.begin(), out.end(), [](const Invite& a, const Invite& b) { return a.created > b.created; });
    for (Invite& i : out) i.hash.clear();
    return out;
}

bool Store::removeInvite(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::find_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.id == id; });
    if (it == invites_.end()) return false;
    invites_.erase(it);
    saveServer();
    return true;
}

std::string Store::inviteLink(const std::string& code) const {
    std::lock_guard<std::mutex> lk(mu_);
    return settings_.url + "#invite=" + code;
}

// ---- Handys -------------------------------------------------------------------------------------------------------
std::string Store::startPairing(const UserPtr& u, int64_t& until) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!alive(u)) return std::string();
    int64_t now = nowEpoch();
    std::string code;
    for (int tries = 0; tries < 20; ++tries) {   // Codes verschiedener Konten nie gleich
        std::string r = randomHex(4);
        if (r.empty()) return std::string();
        char b[8];
        snprintf(b, sizeof b, "%06lu", std::stoul(r, nullptr, 16) % 1000000ul);
        bool taken = false;
        for (const auto& kv : users_)
            if (kv.second != u && kv.second->pairCode == b && kv.second->pairUntil >= now) taken = true;
        if (!taken) {
            code = b;
            break;
        }
    }
    if (code.empty()) return std::string();
    u->pairCode = code;
    u->pairUntil = now + kPairMinutes * 60;
    u->pairFails = 0;
    until = u->pairUntil;
    return code;
}

void Store::stopPairing(const UserPtr& u) {
    std::lock_guard<std::mutex> lk(mu_);
    u->pairCode.clear();
    u->pairUntil = 0;
}

UserPtr Store::pair(const std::string& code, const std::string& deviceName, std::string& token, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = nowEpoch();
    UserPtr hit;
    bool anyActive = false;
    for (const auto& kv : users_) {
        User& u = *kv.second;
        if (u.pairCode.empty() || u.pairUntil < now) continue;
        anyActive = true;
        if (code.size() == 6 && sameSecret(code, u.pairCode)) hit = kv.second;
    }
    if (!hit) {
        // Wer rät, trifft keinen bestimmten Code: jeder Fehlversuch zählt für alle offenen Codes (wie in FragDesk:
        // nach 5 Fehlversuchen verfällt der Code)
        for (const auto& kv : users_) {
            User& u = *kv.second;
            if (u.pairCode.empty() || u.pairUntil < now) continue;
            if (++u.pairFails >= 5) {
                u.pairCode.clear();
                u.pairUntil = 0;
            }
        }
        err = anyActive ? "Der Code stimmt nicht." : "Gerade ist kein Code aktiv. Tippe in FragDesk unter Einstellungen › Handy-App auf „Handy verbinden“.";
        return nullptr;
    }
    token = randomHex(32);
    DeviceRec d;
    d.id = randomHex(4);
    if (token.empty() || d.id.empty()) {
        err = "Kein Zufallsgenerator verfügbar.";
        return nullptr;
    }
    d.hash = sha256Hex(token);
    d.name = cleanText(deviceName, 40);
    d.created = d.lastSeen = now;
    hit->devices.push_back(d);
    while (int(hit->devices.size()) > kMaxDevices) {   // älteste zuerst raus
        deviceTokens_.erase(hit->devices.front().hash);
        hit->devices.erase(hit->devices.begin());
    }
    deviceTokens_[d.hash] = hit->id;
    touch(*hit, nullptr);
    if (!saveAccount(hit)) {
        deviceTokens_.erase(d.hash);
        hit->devices.pop_back();
        err = "Das Handy lässt sich auf dem Server nicht speichern.";
        return nullptr;
    }
    return hit;
}

bool Store::removeDevice(const UserPtr& u, const std::string& token) {
    std::string h = sha256Hex(token);
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::remove_if(u->devices.begin(), u->devices.end(), [&](const DeviceRec& d) { return d.hash == h; });
    if (it == u->devices.end()) return false;
    u->devices.erase(it, u->devices.end());
    deviceTokens_.erase(h);
    saveAccount(u);
    return true;
}

bool Store::removeDeviceById(const UserPtr& u, const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::find_if(u->devices.begin(), u->devices.end(), [&](const DeviceRec& d) { return d.id == id; });
    if (it == u->devices.end()) return false;
    deviceTokens_.erase(it->hash);
    u->devices.erase(it);
    saveAccount(u);
    return true;
}

// ---- Dateien ------------------------------------------------------------------------------------------------------
std::string Store::userDir(const std::string& id) const { return joinPath(joinPath(dir_, "users"), id); }

std::string Store::matchPath(const std::string& id, const std::string& checksum, const char* ext) const {
    return joinPath(joinPath(userDir(id), "matches"), checksum + ext);
}

std::string Store::clipPath(const std::string& id, const std::string& clipId, const char* ext) const {
    return joinPath(joinPath(userDir(id), "clips"), clipId + ext);
}

Fail Store::checkSpace(const User& u, int64_t grow) const {
    if (grow <= 0) return Fail();
    if (u.limit > 0 && u.used + grow > u.limit)
        return Fail::make(507, "Dein Speicher auf dem Server ist voll (" + quotaText(u.used, u.limit) + " belegt).");
    int64_t free = freeBytes(dir_);
    if (free >= 0 && free - grow < minFree) return Fail::make(507, "Auf dem Server ist kein Platz mehr frei. Sag dem Admin Bescheid.");
    return Fail();
}

Fail Store::putMatchFile(const UserPtr& u, const std::string& checksum, bool heat, const std::string& body) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
        auto it = u->matches.find(checksum);
        if (it == u->matches.end() && u->matches.size() >= kMaxMatches)
            return Fail::make(507, "Auf dem Server liegen schon " + std::to_string(kMaxMatches) + " Matches – mehr geht pro Konto nicht.");
        int64_t old = it == u->matches.end() ? -1 : heat ? it->second.heat : it->second.json;
        Fail f = checkSpace(*u, int64_t(body.size()) - std::max<int64_t>(0, old));
        if (!f.ok()) return f;
    }
    if (!writeAtomic(matchPath(u->id, checksum, heat ? ".heat" : ".json"), body))
        return Fail::make(500, "Speichern auf dem Server ging schief: " + lastError());
    std::lock_guard<std::mutex> lk(mu_);
    MatchRec& rec = u->matches[checksum];
    int64_t& slot = heat ? rec.heat : rec.json;
    u->used += int64_t(body.size()) - std::max<int64_t>(0, slot);
    slot = int64_t(body.size());
    return Fail();
}

Fail Store::deleteMatch(const UserPtr& u, const std::string& checksum) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
    }
    bool ok = removeFile(matchPath(u->id, checksum, ".json")) && removeFile(matchPath(u->id, checksum, ".heat"));
    removeFile(matchPath(u->id, checksum, ".replay"));   // gibt es auf dem Server (noch) nicht – sicher ist sicher
    std::lock_guard<std::mutex> lk(mu_);
    u->matches.erase(checksum);
    recount(u);
    if (!ok) return Fail::make(500, "Löschen auf dem Server ging schief: " + lastError());
    return Fail();
}

Fail Store::appendClip(const UserPtr& u, const std::string& clipId, int64_t offset, int64_t total, const std::string& body, int64_t& size) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
        auto it = u->clips.find(clipId);
        if (it == u->clips.end() && u->clips.size() >= kMaxClips)
            return Fail::make(507, "Auf dem Server liegen schon " + std::to_string(kMaxClips) + " Videos – mehr geht pro Konto nicht.");
        ClipRec rec = it == u->clips.end() ? ClipRec() : it->second;
        if (rec.mp4 >= 0) return Fail::make(409, "Das Video ist schon vollständig auf dem Server.", rec.mp4);
        int64_t cur = std::max<int64_t>(0, rec.part);
        if (offset != cur) return Fail::make(409, "Das Stück passt nicht zum bisher hochgeladenen Teil.", cur);
        if (total <= 0 || total > kMaxClip) return Fail::make(400, "Die Gesamtgröße des Videos fehlt oder ist ungültig.");
        if (offset + int64_t(body.size()) > total) return Fail::make(400, "Das Stück reicht über das Ende des Videos.");
        // fürs ganze restliche Video prüfen – sonst merkt FragDesk erst kurz vor Schluss, dass es nicht passt
        Fail f = checkSpace(*u, total - cur);
        if (!f.ok()) return f;
    }
    std::string path = clipPath(u->id, clipId, ".part");
    int64_t onDisk = std::max<int64_t>(0, fileSize(path));
    int64_t newSize = 0;
    if (onDisk != offset || !appendDurable(path, body, offset, newSize)) {
        int64_t real = fileSize(path);
        std::lock_guard<std::mutex> lk(mu_);
        ClipRec& rec = u->clips[clipId];
        rec.part = real;
        recount(u);
        if (onDisk != offset) return Fail::make(409, "Das Stück passt nicht zum bisher hochgeladenen Teil.", std::max<int64_t>(0, real));
        return Fail::make(500, "Speichern auf dem Server ging schief: " + lastError());
    }
    std::lock_guard<std::mutex> lk(mu_);
    ClipRec& rec = u->clips[clipId];
    u->used += newSize - std::max<int64_t>(0, rec.part);
    rec.part = newSize;
    size = newSize;
    return Fail();
}

Fail Store::putThumb(const UserPtr& u, const std::string& clipId, const std::string& jpeg) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
        auto it = u->clips.find(clipId);
        if (it == u->clips.end() && u->clips.size() >= kMaxClips)
            return Fail::make(507, "Auf dem Server liegen schon " + std::to_string(kMaxClips) + " Videos – mehr geht pro Konto nicht.");
        int64_t old = it == u->clips.end() ? -1 : it->second.thumb;
        Fail f = checkSpace(*u, int64_t(jpeg.size()) - std::max<int64_t>(0, old));
        if (!f.ok()) return f;
    }
    if (!writeAtomic(clipPath(u->id, clipId, ".jpg"), jpeg)) return Fail::make(500, "Speichern auf dem Server ging schief: " + lastError());
    std::lock_guard<std::mutex> lk(mu_);
    ClipRec& rec = u->clips[clipId];
    u->used += int64_t(jpeg.size()) - std::max<int64_t>(0, rec.thumb);
    rec.thumb = int64_t(jpeg.size());
    return Fail();
}

Fail Store::finishClip(const UserPtr& u, const std::string& clipId, int64_t total, const ClipMeta& meta) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
        auto it = u->clips.find(clipId);
        ClipRec rec = it == u->clips.end() ? ClipRec() : it->second;
        if (rec.mp4 >= 0) {   // schon fertig (Antwort ging verloren): nur die Angaben übernehmen
            if (rec.mp4 != total) return Fail::make(409, "Auf dem Server liegt dieses Video in einer anderen Größe.", rec.mp4);
            it->second.meta = meta;
            it->second.hasMeta = true;
            if (!saveClips(u)) return Fail::make(500, "clips.json lässt sich nicht speichern.");
            return Fail();
        }
        if (rec.part != total || total <= 0)
            return Fail::make(409, "Das Video ist noch nicht vollständig hochgeladen.", std::max<int64_t>(0, rec.part));
    }
    std::string part = clipPath(u->id, clipId, ".part");
    if (fileSize(part) != total) return Fail::make(409, "Das Video ist noch nicht vollständig hochgeladen.", std::max<int64_t>(0, fileSize(part)));
    std::string first;   // nur der Anfang: MP4 beginnt mit einem „ftyp“-Block
    if (FILE* f = fopen(part.c_str(), "rb")) {
        char b[12] = {};
        first.assign(b, fread(b, 1, sizeof b, f));
        fclose(f);
    }
    if (first.size() < 12 || first.compare(4, 4, "ftyp") != 0) {
        removeFile(part);
        std::lock_guard<std::mutex> lk(mu_);
        u->clips[clipId].part = -1;
        recount(u);
        return Fail::make(400, "Das ist kein MP4-Video.");
    }
    if (!renameFile(part, clipPath(u->id, clipId, ".mp4"))) return Fail::make(500, "Speichern auf dem Server ging schief: " + lastError());
    std::lock_guard<std::mutex> lk(mu_);
    ClipRec& rec = u->clips[clipId];
    rec.mp4 = total;
    rec.part = -1;
    rec.meta = meta;
    rec.hasMeta = true;
    recount(u);
    if (!saveClips(u)) return Fail::make(500, "clips.json lässt sich nicht speichern.");
    return Fail();
}

Fail Store::deleteClip(const UserPtr& u, const std::string& clipId) {
    std::lock_guard<std::mutex> fl(u->fileMu);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!alive(u)) return Fail::make(401, kGone);
    }
    bool ok = true;
    for (const char* ext : {".mp4", ".part", ".jpg"}) ok = removeFile(clipPath(u->id, clipId, ext)) && ok;
    std::lock_guard<std::mutex> lk(mu_);
    bool had = u->clips.count(clipId) && u->clips[clipId].hasMeta;
    u->clips.erase(clipId);
    recount(u);
    if (had && !saveClips(u)) ok = false;
    if (!ok) return Fail::make(500, "Löschen auf dem Server ging schief: " + lastError());
    return Fail();
}

std::map<std::string, MatchRec> Store::matchFiles(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->matches;
}

std::map<std::string, ClipRec> Store::clipFiles(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->clips;
}

std::vector<fdapi::ClipInfo> Store::clipInfos(const UserPtr& u) const {
    std::vector<fdapi::ClipInfo> out;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& kv : u->clips) {
            const ClipRec& c = kv.second;
            if (!c.complete()) continue;
            fdapi::ClipInfo ci;
            ci.id = kv.first;
            ci.path = clipPath(u->id, kv.first, ".mp4");
            ci.title = c.meta.title;
            ci.match = c.meta.match;
            ci.highlight = c.meta.highlight;
            ci.map = c.meta.map;
            ci.created = c.meta.created;
            ci.size = c.mp4;
            ci.seconds = c.meta.seconds;
            out.push_back(std::move(ci));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const fdapi::ClipInfo& a, const fdapi::ClipInfo& b) { return a.created > b.created; });
    return out;
}

int64_t Store::used(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->used;
}

int64_t Store::limit(const UserPtr& u) const {
    std::lock_guard<std::mutex> lk(mu_);
    return u->limit;
}

// ---- Aufräumen ----------------------------------------------------------------------------------------------------
void Store::maintain() {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t now = nowEpoch();
    size_t before = invites_.size();
    invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.expires < now; }), invites_.end());
    if (invites_.size() != before) serverDirty_ = true;
    for (auto& kv : users_) {
        User& u = *kv.second;
        if (!u.pairCode.empty() && u.pairUntil < now) u.pairCode.clear();
        if (u.dirty) saveAccount(kv.second);
    }
    if (serverDirty_) saveServer();
}

void Store::flush() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& kv : users_)
        if (kv.second->dirty) saveAccount(kv.second);
    if (serverDirty_) saveServer();
}

}  // namespace fdsrv
