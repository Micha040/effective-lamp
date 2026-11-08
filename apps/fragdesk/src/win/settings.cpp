// FragDesk – Einstellungen (siehe settings.h)
#include "settings.h"

#include <cctype>
#include <cstdio>

#include "../core/json.h"
#include "common.h"

namespace fdw {

std::string jsonQuote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else o += char(c);
        }
    }
    return o + "\"";
}

bool validToken(const std::string& token) {
    if (token.size() < 16 || token.size() > 256) return false;
    for (unsigned char c : token)
        if (!(std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')) return false;
    return true;
}

static std::wstring settingsPath() { return joinPath(dataDir(), L"settings.json"); }

bool Settings::load() {
    std::string text;
    if (!readFile(settingsPath(), text, 1u << 20)) return false;
    json::Value v;
    if (!json::parse(text, v) || !v.isObject()) return false;
    steamId = v.getString("steamId");
    cs2Dir = widen(v.getString("cs2Dir"));
    videoDir = widen(v.getString("videoDir"));
    watchReplays = v.getBool("watchReplays", true);
    watchDownloads = v.getBool("watchDownloads", true);
    autoAnalyze = v.getBool("autoAnalyze", false);
    theme = v.getString("theme");
    folders.clear();
    for (const auto& f : v["folders"].arr)
        if (f.isString() && !f.str.empty()) folders.push_back(widen(f.str));
    ignored.clear();
    for (const auto& f : v["ignored"].arr)
        if (f.isString()) ignored.push_back(f.str);
    const json::Value& r = v["record"];
    if (r.isObject()) {
        int h = int(r.getNumber("height", 1080));
        record.height = h == 720 || h == 1440 ? h : 1080;
        record.fps = int(r.getNumber("fps", 60)) == 30 ? 30 : 60;
        int q = int(r.getNumber("quality", 2));
        record.quality = q < 1 ? 1 : q > 3 ? 3 : q;
        record.onlyKillfeed = r.getBool("onlyKillfeed", true);
        record.voices = r.getBool("voices", false);
        record.sound = r.getBool("sound", true);
    }
    const json::Value& mo = v["mobile"];
    if (mo.isObject()) {
        mobile.enabled = mo.getBool("enabled", false);
        int port = int(mo.getNumber("port", 7420));
        mobile.port = port >= 1024 && port <= 65535 ? port : 7420;
        mobile.address = mo.getString("address");
        mobile.server = mo.getString("mode") == "server";
        const json::Value& sv = mo["server"];
        if (sv.isObject()) {
            ServerSettings& s = mobile.srv;
            s.url = sv.getString("url");
            s.token = sv.getString("token");
            if (!validToken(s.token)) s.token.clear();
            s.user = sv.getString("user");
            s.userId = sv.getString("userId");
            s.admin = sv.getBool("admin", false);
            s.videos = sv.getBool("videos", true);
            s.replays = sv.getBool("replays", true);
            s.profile = sv.getString("profile");
            s.lastSync = int64_t(sv.getNumber("lastSync"));
            s.used = int64_t(sv.getNumber("used", -1));
            s.limit = int64_t(sv.getNumber("limit", 0));
            for (const auto& g : sv["gone"].arr)
                if (g.isString() && g.str.size() > 2 && g.str.size() <= 80 && s.gone.size() < kMaxGone) s.gone.push_back(g.str);
        }
        mobile.devices.clear();
        for (const auto& d : mo["devices"].arr) {
            MobileDevice md;
            md.token = d.getString("token");
            md.name = d.getString("name");
            md.created = int64_t(d.getNumber("created"));
            md.lastSeen = int64_t(d.getNumber("lastSeen"));
            if (md.token.size() >= 32) mobile.devices.push_back(md);
        }
    }
    return true;
}

bool Settings::save() const {
    std::string o = "{\n";
    o += "  \"steamId\": " + jsonQuote(steamId) + ",\n";
    o += "  \"cs2Dir\": " + jsonQuote(narrow(cs2Dir)) + ",\n";
    o += "  \"videoDir\": " + jsonQuote(narrow(videoDir)) + ",\n";
    o += std::string("  \"watchReplays\": ") + (watchReplays ? "true" : "false") + ",\n";
    o += std::string("  \"watchDownloads\": ") + (watchDownloads ? "true" : "false") + ",\n";
    o += std::string("  \"autoAnalyze\": ") + (autoAnalyze ? "true" : "false") + ",\n";
    o += "  \"theme\": " + jsonQuote(theme) + ",\n";
    o += "  \"folders\": [";
    for (size_t i = 0; i < folders.size(); ++i) o += (i ? ", " : "") + jsonQuote(narrow(folders[i]));
    o += "],\n  \"ignored\": [";
    for (size_t i = 0; i < ignored.size(); ++i) o += (i ? ",\n    " : "\n    ") + jsonQuote(ignored[i]);
    o += ignored.empty() ? "],\n" : "\n  ],\n";
    char b[256];
    std::snprintf(b, sizeof b, "  \"record\": {\"height\": %d, \"fps\": %d, \"quality\": %d, \"onlyKillfeed\": %s, \"voices\": %s, \"sound\": %s},\n",
                  record.height, record.fps, record.quality, record.onlyKillfeed ? "true" : "false", record.voices ? "true" : "false",
                  record.sound ? "true" : "false");
    o += b;
    std::snprintf(b, sizeof b, "  \"mobile\": {\"enabled\": %s, \"mode\": \"%s\", \"port\": %d, \"address\": ", mobile.enabled ? "true" : "false",
                  mobile.server ? "server" : "pc", mobile.port);
    o += b + jsonQuote(mobile.address) + ",\n";
    const ServerSettings& sv = mobile.srv;
    o += "    \"server\": {\"url\": " + jsonQuote(sv.url) + ", \"token\": " + jsonQuote(sv.token) + ", \"user\": " + jsonQuote(sv.user) +
         ", \"userId\": " + jsonQuote(sv.userId) + ", \"admin\": " + (sv.admin ? "true" : "false") + ", \"videos\": " + (sv.videos ? "true" : "false") +
         ", \"replays\": " + (sv.replays ? "true" : "false") + ", \"profile\": " + jsonQuote(sv.profile);
    std::snprintf(b, sizeof b, ", \"lastSync\": %lld, \"used\": %lld, \"limit\": %lld, \"gone\": [", static_cast<long long>(sv.lastSync),
                  static_cast<long long>(sv.used), static_cast<long long>(sv.limit));
    o += b;
    for (size_t i = 0; i < sv.gone.size(); ++i) o += (i ? ", " : "") + jsonQuote(sv.gone[i]);
    o += "]},\n    \"devices\": [";
    for (size_t i = 0; i < mobile.devices.size(); ++i) {
        const MobileDevice& d = mobile.devices[i];
        std::snprintf(b, sizeof b, ", \"created\": %lld, \"lastSeen\": %lld}", static_cast<long long>(d.created), static_cast<long long>(d.lastSeen));
        o += (i ? ",\n    " : "\n    ") + std::string("{\"token\": ") + jsonQuote(d.token) + ", \"name\": " + jsonQuote(d.name) + b;
    }
    o += mobile.devices.empty() ? "]}\n" : "\n  ]}\n";
    o += "}\n";
    createDirs(dataDir());
    return writeFileAtomic(settingsPath(), o);
}

std::wstring Settings::effectiveVideoDir() const {
    if (!videoDir.empty()) return videoDir;
    return joinPath(videosDir(), L"FragDesk");
}

}  // namespace fdw
