// efficient lamp – Katalog lesen und prüfen (siehe catalog.h)
#include "catalog.h"

#include <cctype>
#include <cstdlib>

#include "json.h"

namespace cat {

namespace {

bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

bool validId(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    for (char c : s)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '-')) return false;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool readCommand(const json::Value& v, Command& c, const std::string& what, std::string& err) {
    c = Command();
    if (v.type == json::Value::Null) return true;   // optional
    if (!v.isObject()) {
        err = what + ": Objekt erwartet";
        return false;
    }
    c.script = v.getString("script");
    c.exe = v.getString("exe");
    c.args = v.getString("args");
    c.label = v.getString("label");
    c.wait = v.getBool("wait", true);
    if (!c.script.empty() && !isSafeFileName(c.script)) {
        err = what + ": \"script\" darf nur ein Dateiname im Paket sein";
        return false;
    }
    if (!c.exe.empty() && !isUserPath(c.exe)) {
        err = what + ": \"exe\" muss unter %LOCALAPPDATA% oder %APPDATA% liegen";
        return false;
    }
    if (!c.script.empty() && !c.exe.empty()) {
        err = what + ": entweder \"script\" oder \"exe\"";
        return false;
    }
    // cmd.exe wertet & | < > ^ aus und setzt %VAR% / !VAR! ein – nichts davon darf in den Argumenten stehen
    for (char ch : c.args)
        if ((unsigned char)ch < 0x20 || ch == '&' || ch == '|' || ch == '<' || ch == '>' || ch == '^' || ch == '%' || ch == '!' || ch == '"') {
            err = what + ": unerlaubtes Zeichen in \"args\"";
            return false;
        }
    return true;
}

// Ganzzahl >= 0 (JSON-Zahl ohne Nachkommastellen, höchstens 2^53)
bool readSize(const json::Value& v, const std::string& key, uint64_t& out) {
    out = 0;
    const json::Value* n = v.find(key);
    if (!n) return true;
    if (!n->isNumber() || n->num < 0 || n->num > 9007199254740992.0 || n->num != (double)(uint64_t)n->num) return false;
    out = uint64_t(n->num);
    return true;
}

bool startsWithDir(const std::string& path, const std::string& dir) {
    std::string p = lower(path), d = lower(dir);
    if (!d.empty() && d.back() != '\\') d += '\\';
    return p.compare(0, d.size(), d) == 0;
}

}  // namespace

bool isSafeFileName(const std::string& s) {
    if (s.empty() || s.size() > 80 || s == "." || s == "..") return false;
    for (char c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '"' || (unsigned char)c < 0x20 || c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
            return false;
    return true;
}

bool isValidVersion(const std::string& s) {
    if (s.empty() || s.size() > 32 || !std::isdigit((unsigned char)s[0]) || s.back() == '.' || s.find("..") != std::string::npos) return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-')) return false;
    return true;
}

bool isUserPath(const std::string& s) {
    std::string l = lower(s);
    bool root = l.rfind("%localappdata%\\", 0) == 0 || l.rfind("%appdata%\\", 0) == 0;
    if (!root || l.find("..") != std::string::npos || l.find('/') != std::string::npos) return false;
    for (char c : s)
        if ((unsigned char)c < 0x20 || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|') return false;
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v, std::vector<long>& nums, std::string& pre) {
        size_t i = 0;
        if (i < v.size() && (v[i] == 'v' || v[i] == 'V')) ++i;
        nums.clear();
        pre.clear();
        while (i < v.size()) {
            if (std::isdigit((unsigned char)v[i])) {
                long n = 0;
                while (i < v.size() && std::isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
                nums.push_back(n);
            } else if (v[i] == '.') {
                ++i;
            } else {
                pre = v.substr(v[i] == '-' ? i + 1 : i);   // "-beta.1" usw.
                break;
            }
        }
        while (!nums.empty() && nums.back() == 0) nums.pop_back();
    };
    std::vector<long> na, nb;
    std::string pa, pb;
    parts(a, na, pa);
    parts(b, nb, pb);
    for (size_t i = 0; i < na.size() || i < nb.size(); ++i) {
        long x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (pa == pb) return 0;
    if (pa.empty()) return 1;   // 1.0.0 > 1.0.0-beta
    if (pb.empty()) return -1;
    return pa < pb ? -1 : 1;
}

std::string findVersionString(const std::string& text) {
    // sucht  Version = "1.2.3"  (C#: public const string Version = "1.1.0";)
    size_t pos = 0;
    while ((pos = text.find("Version", pos)) != std::string::npos) {
        size_t i = pos + 7;
        pos = i;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= text.size() || text[i] != '=') continue;
        ++i;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= text.size() || (text[i] != '"' && text[i] != '\'')) continue;
        char q = text[i++];
        size_t end = text.find(q, i);
        if (end == std::string::npos || end - i > 32 || end == i) continue;
        std::string v = text.substr(i, end - i);
        if (!std::isdigit((unsigned char)v[0])) continue;
        bool ok = true;
        for (char c : v)
            if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-')) ok = false;
        if (ok) return v;
    }
    return std::string();
}

namespace {

// Ein Programm lesen; false + Grund, wenn der Eintrag unbrauchbar ist (er wird dann übersprungen)
bool readApp(const json::Value& a, App& app, std::string& err) {
    app.id = a.getString("id");
    if (!validId(app.id)) {
        err = "ungültige App-ID \"" + app.id + "\"";
        return false;
    }
    app.name = a.getString("name");
    app.version = a.getString("version");
    app.tagline = a.getString("tagline");
    app.description = a.getString("description");
    app.requirements = a.getString("requires");
    app.color = a.getString("color");
    app.page = a.getString("page");
    const json::Value& d = a["download"];
    app.file = d.getString("file");
    app.url = d.getString("url");
    app.sha256 = lower(d.getString("sha256"));
    app.folder = d.getString("folder");
    app.process = a.getString("process");
    app.installDir = a.getString("installDir");
    const json::Value& det = a["detect"];
    app.uninstallKey = det.getString("uninstallKey");
    app.exe = det.getString("exe");
    app.versionFile = det.getString("versionFile");
    const std::string where = app.id + ": ";
    if (app.name.empty() || !isValidVersion(app.version)) {
        err = where + "Name fehlt oder Version ungültig";
        return false;
    }
    if (app.url.empty() || !hex64(app.sha256) || !readSize(d, "size", app.size)) {
        err = where + "Download-Adresse, Größe oder SHA-256 ungültig";
        return false;
    }
    if (!app.folder.empty() && !isSafeFileName(app.folder)) {
        err = where + "\"folder\" ungültig";
        return false;
    }
    if (!app.process.empty() && !isSafeFileName(app.process)) {
        err = where + "\"process\" ungültig";
        return false;
    }
    for (const std::string* p : {&app.exe, &app.versionFile, &app.installDir})
        if (!p->empty() && !isUserPath(*p)) {
            err = where + "Pfade müssen unter %LOCALAPPDATA% oder %APPDATA% liegen";
            return false;
        }
    if (!app.uninstallKey.empty() && !isSafeFileName(app.uninstallKey)) {
        err = where + "\"uninstallKey\" ungültig";
        return false;
    }
    if (app.exe.empty() && app.uninstallKey.empty()) {
        err = where + "keine Erkennung (detect.exe oder detect.uninstallKey)";
        return false;
    }
    if (!readCommand(a["install"], app.install, where + "install", err) || !readCommand(a["uninstall"], app.uninstall, where + "uninstall", err) ||
        !readCommand(a["open"], app.open, where + "open", err))
        return false;
    if (app.install.script.empty()) {
        err = where + "\"install.script\" fehlt";
        return false;
    }
    // Programme dürfen nur aus dem eigenen Installationsordner gestartet werden
    for (const Command* c : {&app.install, &app.uninstall, &app.open})
        if (!c->exe.empty() && (app.installDir.empty() || !startsWithDir(c->exe, app.installDir))) {
            err = where + "\"exe\" muss im Installationsordner liegen";
            return false;
        }
    return true;
}

}  // namespace

bool parse(const std::string& text, Catalog& out, std::string& err) {
    out = Catalog();
    json::Value root;
    std::string jerr;
    if (!json::parse(text, root, &jerr)) {
        err = "Katalog ist kein gültiges JSON: " + jerr;
        return false;
    }
    if (!root.isObject()) {
        err = "Katalog: Objekt erwartet";
        return false;
    }
    const json::Value& schema = root["schema"];
    if (!schema.isNumber() || schema.num != double(int(schema.num))) {
        err = "Katalog: \"schema\" fehlt";
        return false;
    }
    out.schema = int(schema.num);
    out.name = root.getString("name", "efficient lamp");
    out.release = root.getString("release");
    out.published = root.getString("published");
    out.repo = root.getString("repo");

    // Launcher-Abschnitt immer lesen – damit kann sich auch ein zu alter Launcher noch selbst aktualisieren
    const json::Value& l = root["launcher"];
    Launcher la;
    la.version = l.getString("version");
    la.file = l.getString("file");
    la.url = l.getString("url");
    la.sha256 = lower(l.getString("sha256"));
    if (isValidVersion(la.version) && !la.url.empty() && hex64(la.sha256) && readSize(l, "size", la.size)) out.launcher = la;

    out.supported = out.schema == kSchema;
    if (!out.supported) return true;   // Programme in unbekanntem Format nicht anfassen

    const json::Value& apps = root["apps"];
    if (!apps.isArray()) {
        err = "Katalog: \"apps\" fehlt";
        return false;
    }
    for (const json::Value& a : apps.arr) {
        App app;
        std::string why;
        if (!readApp(a, app, why)) {
            out.skipped.push_back(why);
            continue;
        }
        bool dup = false;
        for (const App& other : out.apps) dup = dup || other.id == app.id;
        if (dup) {
            out.skipped.push_back(app.id + ": App-ID doppelt");
            continue;
        }
        out.apps.push_back(std::move(app));
    }
    return true;
}

}  // namespace cat
