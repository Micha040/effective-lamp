// Tests für die Handy-App-Schnittstelle (Linux): JSON, Anfragen zerlegen, Kopplung, Routen, Radarbilder, Heatmaps,
// echter Server mit Range.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "../src/api/jsonw.h"
#include "../src/api/mobile.h"
#include "../src/core/heat.h"
#include "../src/core/json.h"

using namespace fdapi;

static int fails = 0, checks = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        ++checks;                                                        \
        if (!(c)) {                                                      \
            ++fails;                                                     \
            std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #c);   \
        }                                                                \
    } while (0)

namespace {

class EmptySource : public Source {
public:
    std::string me() override { return "1"; }
    std::string meName() override { return "Ich"; }
    bool loaded() override { return true; }
    std::vector<const fd::Match*> matches() override { return {}; }
    int generation() override { return 1; }
    bool detail(const std::string&, fd::Match&) override { return false; }
    std::vector<ClipInfo> clips() override { return {}; }
};

Request req(const std::string& method, const std::string& path, const std::string& query = "", const std::string& body = "") {
    Request r;
    r.method = method;
    r.path = path;
    r.query = query;
    r.body = body;
    return r;
}

// ---- Heatmaps: Matches und .heat-Dateien im Speicher ------------------------------------------------------------------
const char* kMe = "76561198000000001";

// Kleines Match (Schema 2 = mit Heatmap-Daten) mit zwei Runden: Ich in Team A (Runde 1 CT, Runde 2 T), ein Gegner
std::string miniMatch(const std::string& cs, const std::string& map, const std::string& date, int schema) {
    return std::string(R"({"schema":)") + std::to_string(schema) + R"(,"analyzer":"1.0.0","checksum":")" + cs + R"(",
"demo":{"path":"C:\\d\\m.dem","file":"m.dem","size":1000,"modified":")" + date + R"("},
"match":{"source":"valve","mode":"premier","map":")" + map + R"(","date":")" + date + R"(","duration":1800,"tickrate":64,
 "tickCount":100000,"serverName":"x","maxRounds":24,"overtimes":0,"scoreA":13,"scoreB":11,"winner":"A"},
"teams":[{"letter":"A","name":"Team A","score":13,"startSide":"CT"},{"letter":"B","name":"Team B","score":11,"startSide":"T"}],
"rounds":[{"n":1,"start":0,"freezeEnd":1000,"end":5000,"endOfficial":5400,"winner":"A","winnerSide":"CT","reason":"elimination","sideA":"CT"},
 {"n":2,"start":5400,"freezeEnd":6000,"end":8000,"endOfficial":8400,"winner":"B","winnerSide":"CT","reason":"elimination","sideA":"T"}],
"players":[{"steamId":"76561198000000001","name":"Micha","team":"A","result":"win","rating":1.2,"kast":75,"adr":90,
 "stats":{"kills":20,"deaths":15,"rounds":24},"statsT":{},"statsCT":{},"weapons":[],"deathsBy":{},"rounds":[],"opponents":[]},
 {"steamId":"76561198000000009","name":"Gegner","team":"B","result":"loss","stats":{"kills":15,"deaths":20,"rounds":24},
 "statsT":{},"statsCT":{},"weapons":[],"deathsBy":{},"rounds":[],"opponents":[]}],
"kills":[],"clutches":[],"highlights":[]})";
}

struct Bytes {
    std::string b;
    void u8(int v) { b.push_back(char(uint8_t(v))); }
    void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8(int((v >> (8 * i)) & 0xFF)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
    void str(const std::string& s) { u16(int(s.size())); b += s; }
    void vec(int x, int y, int z) { u16(x & 0xFFFF); u16(y & 0xFFFF); u16(z & 0xFFFF); }
};

// Heatmap zu miniMatch auf Nuke: Ich oben (zweimal in Runde 1 als CT, einmal in Runde 2 als T), der Gegner unten,
// extra weitere Abtastungen in Runde 2 (Spieler extraPlayer); ein Kopfschuss-Kill von mir (Opfer unten), eine Smoke
std::string sampleHeat(int extra = 0, int extraPlayer = 0) {
    Bytes f;
    f.u32(1500); f.u8(2);
    f.u8(0); f.u8(fd::HsCT); f.vec(-1000, 1000, 0);
    f.u8(1); f.u8(0); f.vec(0, 0, -600);
    f.u32(3000); f.u8(1); f.u8(0); f.u8(fd::HsCT); f.vec(-1000, 1000, 0);
    f.u32(500); f.u8(1); f.u8(0); f.u8(fd::HsCT); f.vec(-1000, 1000, 0);   // Kaufzeit: zählt nicht
    f.u32(6500); f.u8(1); f.u8(0); f.u8(0); f.vec(-500, 500, 0);
    for (int i = 0; i < extra; ++i) {
        f.u32(uint32_t(7000 + i)); f.u8(1); f.u8(uint8_t(extraPlayer)); f.u8(0); f.vec(-500, 500, 0);
    }
    Bytes b;
    b.b = "FDHM"; b.u16(1); b.u16(0); b.f32(64); b.str("de_nuke");
    b.u16(2); b.u64(76561198000000001ull); b.str("Micha"); b.u64(76561198000000009ull); b.str("Gegner");
    b.u16(2); b.str(""); b.str("AK-47");
    b.u32(1); b.u32(2000); b.u8(0); b.u8(1); b.u8(fd::HkKillerCT | fd::HkHeadshot); b.u8(1); b.vec(-1000, 1000, 0); b.vec(0, 0, -600);
    b.u32(1); b.u32(1200); b.u32(1300); b.u8(fd::GrSmoke); b.u8(0); b.u8(fd::HnCT); b.vec(-1000, 1000, 0); b.vec(0, 0, 0);
    b.u32(uint32_t(4 + extra)); b.u32(uint32_t(f.b.size())); b.b += f.b;
    b.b += "END!";
    return b.b;
}

class HeatSource : public Source {
public:
    std::vector<std::unique_ptr<fd::Match>> list;
    std::map<std::string, std::string> heat;   // Prüfsumme -> Pfad
    int gen = 100;
    void add(const std::string& text) {
        auto m = std::make_unique<fd::Match>();
        std::string err;
        if (fd::parseMatch(text, *m, err, false)) list.push_back(std::move(m));
        else std::printf("Testmatch nicht lesbar: %s\n", err.c_str());
    }
    std::string me() override { return kMe; }
    std::string meName() override { return "Micha"; }
    bool loaded() override { return true; }
    std::vector<const fd::Match*> matches() override {
        std::vector<const fd::Match*> v;
        for (const auto& m : list) v.push_back(m.get());
        return v;
    }
    int generation() override { return gen; }
    bool detail(const std::string&, fd::Match&) override { return false; }
    std::vector<ClipInfo> clips() override { return {}; }
    std::string matchFile(const std::string& checksum, const std::string& ext) override {
        auto it = heat.find(checksum);
        return ext == "heat" && it != heat.end() ? it->second : std::string();
    }
};

// Raster der Heatmap (Base64, Nullen als „0, Anzahl − 1“) -> belegte Zellen; -1, wenn die Länge nicht n × n ist
int gridCells(const std::string& b64, int n) {
    auto val = [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : 63; };
    std::string raw;
    for (size_t i = 0; i + 3 < b64.size(); i += 4) {
        uint32_t v = uint32_t(val(b64[i]) << 18 | val(b64[i + 1]) << 12 | (b64[i + 2] == '=' ? 0 : val(b64[i + 2]) << 6) | (b64[i + 3] == '=' ? 0 : val(b64[i + 3])));
        raw += char(v >> 16 & 255);
        if (b64[i + 2] != '=') raw += char(v >> 8 & 255);
        if (b64[i + 3] != '=') raw += char(v & 255);
    }
    int cells = 0, lit = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i]) ++cells, ++lit;
        else cells += uint8_t(raw[++i]) + 1;
    }
    return cells == n * n ? lit : -1;
}

std::string rawHttp(int port, const std::string& text) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return "";
    send(s, text.data(), text.size(), 0);
    std::string out;
    char b[4096];
    ssize_t k;
    while ((k = recv(s, b, sizeof b, 0)) > 0) out.append(b, size_t(k));
    close(s);
    return out;
}

}  // namespace

int main() {
    // JSON schreiben
    JsonW j;
    j.obj().kv("a", "x\"y\n").kn("n", 1.5).kn("i", 3).kn("nan", NAN).key("arr").arr().integer(1).boolean(true).null().end().end();
    CHECK(j.str() == "{\"a\":\"x\\\"y\\n\",\"n\":1.5,\"i\":3,\"nan\":null,\"arr\":[1,true,null]}");

    // Anfragen zerlegen
    Request r = req("GET", "/x", "a=1&b=h%C3%A4llo+welt&c");
    r.headers.push_back({"cookie", "x=1; fdk=abc ; y=2"});
    CHECK(r.param("b") == "h\xC3\xA4llo welt");
    CHECK(r.param("c").empty() && r.param("zz").empty());
    CHECK(r.cookie("fdk") == "abc");
    CHECK(urlDecode("%2e%2E/x") == "../x");

    // Kopplung
    Auth auth;
    int changes = 0;
    auth.onChange = [&] { ++changes; };
    std::string err;
    bool tooMany = false;
    CHECK(auth.pair("123456", "x", err, tooMany).empty());   // kein Code aktiv
    auth.startPairing(10, "123456");
    CHECK(auth.pairingCode() == "123456");
    CHECK(auth.pair("000000", "x", err, tooMany).empty() && !tooMany);
    std::string token = auth.pair("123456", "iPhone", err, tooMany);
    CHECK(token.size() == 64 && changes == 1);
    CHECK(auth.check(token) && !auth.check(token.substr(1)) && !auth.check(""));
    bool blocked = false;
    for (int i = 0; i < 5; ++i) {
        auth.pair("999999", "x", err, tooMany);
        blocked = blocked || tooMany;
    }
    CHECK(blocked && auth.pairingCode().empty());   // nach 5 Fehlversuchen insgesamt ist der Code weg
    CHECK(auth.remove(token) && !auth.check(token));

    // Routen
    EmptySource src;
    Auth a2;
    Host host;
    host.version = "test";
    host.auth = &a2;
    host.withSource = [&](const std::function<void(Source&)>& fn) {
        fn(src);
        return true;
    };
    host.asset = [](const std::string& p, std::string& body) {
        if (p != "index.html") return false;
        body = "<html>";
        return true;
    };
    Response res;
    handle(host, req("GET", "/api/v1/info"), res);
    CHECK(res.status == 401);
    res = Response();
    handle(host, req("POST", "/api/v1/pair", "", "{\"code\":\"111111\"}"), res);
    CHECK(res.status == 403);   // ohne X-FragDesk-Kopf
    a2.startPairing(10, "111111");
    Request pr = req("POST", "/api/v1/pair", "", "{\"code\":\"111111\"}");
    pr.headers.push_back({"x-fragdesk", "1"});
    res = Response();
    handle(host, pr, res);
    CHECK(res.status == 200);
    std::string cookie;
    for (const auto& kv : res.headers)
        if (kv.first == "Set-Cookie") cookie = kv.second.substr(0, kv.second.find(';'));
    CHECK(cookie.size() == 4 + 64);
    Request ir = req("GET", "/api/v1/overview", "mode=0");
    ir.headers.push_back({"cookie", cookie});
    res = Response();
    handle(host, ir, res);
    json::Value v;
    CHECK(res.status == 200 && json::parse(res.body, v) && v["empty"].getString("title") == "Noch keine Matches");
    res = Response();
    handle(host, req("GET", "/"), res);
    CHECK(res.status == 200 && res.body == "<html>" && res.type.find("text/html") == 0);
    res = Response();
    handle(host, req("GET", "/../secret"), res);
    CHECK(res.status == 404);
    Request mr = req("GET", "/media/clip/abc");
    res = Response();
    handle(host, mr, res);
    CHECK(res.status == 401);   // Videos nur gekoppelt

    // Radarbilder: nur gekoppelt, ETag/304, nur bekannte Karten und Ebenen
    const std::string png = "\x89PNG\r\n\x1a\nRADAR:";
    host.radar = [&](const std::string& name, bool lower, std::string& out) {
        out = png + name + (lower ? "_lower" : "");
        return true;
    };
    auto get = [&](const std::string& path, const std::string& query = "", const std::string& etag = "") {
        Request rq = req("GET", path, query);
        rq.headers.push_back({"cookie", cookie});
        if (!etag.empty()) rq.headers.push_back({"if-none-match", etag});
        Response rs;
        handle(host, rq, rs);
        return rs;
    };
    res = Response();
    handle(host, req("GET", "/media/radar/de_nuke.png"), res);
    CHECK(res.status == 401);
    Response rr = get("/media/radar/de_nuke.png");
    std::string etag;
    for (const auto& kv : rr.headers)
        if (kv.first == "ETag") etag = kv.second;
    CHECK(rr.status == 200 && rr.type == "image/png" && rr.body == png + "de_nuke" && etag.size() > 2);
    CHECK(get("/media/radar/de_nuke.png", "", etag).status == 304);
    CHECK(get("/media/radar/de_nuke_lower.png").body == png + "de_nuke_lower");
    CHECK(get("/media/radar/de_mirage_lower.png").status == 404);   // Mirage hat keine untere Ebene
    CHECK(get("/media/radar/de_gibtsnicht.png").status == 404 && get("/media/radar/de_nuke.jpg").status == 404);

    // Heatmaps: Matches und .heat-Dateien im Speicher
    HeatSource hs;
    hs.add(miniMatch("n0", "de_nuke", "2026-11-01T20:00:00Z", 1));          // ältere Auswertung: keine Heatmap
    hs.add(miniMatch("n1", "de_nuke", "2026-11-02T20:00:00Z", 2));
    hs.add(miniMatch("n2", "de_nuke", "2026-11-03T20:00:00Z", 2));
    hs.add(miniMatch("m1", "de_mirage", "2026-11-04T20:00:00Z", 2));        // Datei fehlt
    hs.add(miniMatch("x1", "de_nuke", "2026-11-05T20:00:00Z", 2));          // Datei kaputt
    hs.add(miniMatch("w1", "de_gibtsnicht", "2026-11-06T20:00:00Z", 2));    // Karte ohne Radarbild
    std::map<std::string, std::string> files = {
        {"mem/n1.heat", sampleHeat()}, {"mem/n2.heat", sampleHeat()}, {"mem/x1.heat", std::string("FDHM\x01\0\0\0kaputt", 14)}};
    hs.heat = {{"n1", "mem/n1.heat"}, {"n2", "mem/n2.heat"}, {"x1", "mem/x1.heat"}};
    host.withSource = [&](const std::function<void(Source&)>& fn) {
        fn(hs);
        return true;
    };
    host.openFile = [&](const std::string& path, int64_t& size) -> FileReader {
        auto it = files.find(path);
        if (it == files.end()) return nullptr;
        auto data = std::make_shared<std::string>(it->second);
        size = int64_t(data->size());
        return [data](int64_t off, char* buf, int64_t len) -> int64_t {
            int64_t n = std::min<int64_t>(len, int64_t(data->size()) - off);
            if (n <= 0) return 0;
            std::memcpy(buf, data->data() + off, size_t(n));
            return n;
        };
    };
    auto heat = [&](const std::string& query, int* status = nullptr) {
        Response rs = get("/api/v1/heat", query);
        if (status) *status = rs.status;
        json::Value v;
        json::parse(rs.body, v);
        return v;
    };
    int st = 0;
    res = Response();
    handle(host, req("GET", "/api/v1/heat", "match=n1"), res);
    CHECK(res.status == 401);   // nur gekoppelt
    heat("match=gibtsnicht", &st);
    CHECK(st == 404);

    // Ein Match: Standard ist Aufenthalt, „Ich“, beide Seiten, obere Ebene, Punkte an
    json::Value h1 = heat("match=n1", &st);
    CHECK(st == 200 && h1.getString("scope") == "match" && h1.getString("id") == "n1" && h1["head"].getString("map") == "Nuke" &&
          h1["head"].getString("score") == "13 : 11" && h1.getString("title") == "Nuke · 13:11" && h1["empty"].type == json::Value::Null);
    CHECK(h1["radar"].getString("lowerImage") == "/media/radar/de_nuke_lower.png?v=test" && h1["ui"]["levels"].arr.size() == 2);
    CHECK(h1.getBool("hasMe") && h1["filter"].getNumber("who") == 1 && h1["filter"].getNumber("layers") == 1 && h1["filter"].getBool("marks") &&
          !h1["filter"].getBool("throws") && h1["filter"].getNumber("sides") == 3 && !h1["filter"].getBool("lower"));
    CHECK(h1["ui"]["who"].arr.size() == 3 && h1["ui"]["who"].arr[0].getString("label") == "Ich" && h1["ui"]["layers"].arr.size() == 7);
    const json::Value& g1 = h1["grid"];
    CHECK(g1.getNumber("n") == 128 && g1["layers"].arr.size() == 1 && gridCells(g1["layers"].arr[0].getString("data"), 128) > 10);
    CHECK(h1["legend"].getBool("gradient") && h1["legend"]["items"].arr.size() == 1 &&
          h1["legend"]["items"].arr[0].getString("value") == "3 s");   // die Kaufzeit zählt nicht
    CHECK(h1["marks"].arr.empty() && h1.getString("foot").rfind("Aufenthalt: wo die gewählten Spieler", 0) == 0);

    // Alle Schichten mit Punkten und Wurfpositionen (obere Ebene): Kill und Smoke mit Linie; der Tod liegt unten
    json::Value h2 = heat("match=n1&layers=127&who=7&marks=1&throws=1");
    const std::vector<json::Value>& mk = h2["marks"].arr;
    CHECK(mk.size() == 2 && h2["legend"]["items"].arr.size() == 7 && !h2["legend"].getBool("gradient"));
    if (mk.size() == 2) {
        CHECK(mk[0].arr.size() == 8 && std::fabs(mk[0].arr[0].num - 3422) <= 1 && std::fabs(mk[0].arr[1].num - 2633) <= 1 &&
              mk[0].arr[2].num == fd::HlKills && mk[0].arr[3].num == 1 && mk[0].arr[4].num == 0 && mk[0].arr[5].num == 1 &&
              mk[0].arr[6].num == 2000 - 128 && mk[0].arr[7].str == "Runde 1 · 0:15\nMicha tötet Gegner (AK-47, Kopfschuss)");
        CHECK(mk[1].arr.size() == 10 && mk[1].arr[2].num == fd::HlSmoke && mk[1].arr[3].num == 7 && mk[1].arr[6].num == 1200 - 128 &&
              mk[1].arr[7].str == "Runde 1 · 0:04\nSmoke von Micha");
    }
    CHECK(h2["matches"].arr.size() == 1 && h2["matches"].arr[0].getString("id") == "n1" && h2.getString("marksNote").empty());

    // Untere Ebene, nur Gegner: sein Aufenthalt und sein Tod
    json::Value h3 = heat("match=n1&layers=5&who=4&lower=1&marks=1");
    CHECK(h3["filter"].getBool("lower") && h3["grid"]["layers"].arr.size() == 2 && h3["marks"].arr.size() == 1);
    CHECK(h3["legend"]["items"].arr.size() == 2 && h3["legend"]["items"].arr[0].getString("value") == "1 s" &&
          h3["legend"]["items"].arr[1].getString("value") == "1");
    json::Value h4 = heat("match=n1&layers=2&sides=1");   // nur T: mein Kill war als CT
    CHECK(h4["legend"]["items"].arr[0].getString("value") == "0" && h4["grid"]["layers"].arr.empty());
    json::Value h5 = heat("match=n1&layers=999&who=0&sides=9&lower=x&marks=7");   // Unsinn: Standard
    CHECK(h5["filter"].getNumber("layers") == 1 && h5["filter"].getNumber("who") == 1 && h5["filter"].getNumber("sides") == 3 &&
          !h5["filter"].getBool("lower") && h5["filter"].getBool("marks"));

    // Fehlende Daten: ältere Auswertung, keine Datei, kaputte Datei, Karte ohne Radarbild
    CHECK(heat("match=n0")["empty"].getString("title") == "Die Heatmap kommt mit dem Neu-Auswerten");
    CHECK(heat("match=m1")["empty"].getString("title") == "Keine Heatmap-Daten für dieses Match");
    CHECK(heat("match=x1")["empty"].getString("title") == "Keine Heatmap-Daten für dieses Match");
    json::Value w1 = heat("match=w1");
    CHECK(w1["empty"].getString("title") == "Kein Radarbild für diese Karte" && w1["radar"].type == json::Value::Null);
    host.kind = "server";
    CHECK(heat("match=m1")["empty"].getString("text").find("Abgleich") != std::string::npos);
    host.kind = "pc";

    // Statistiken: alle Matches einer Map – zur Wahl nur Maps mit Heatmap-Daten (Mirage: Datei fehlt), beschriftet wie in
    // FragDesk mit allen Matches; die ältere zählt für den Hinweis, die kaputte Datei wird übergangen
    json::Value s1 = heat("mode=0&period=2", &st);
    CHECK(st == 200 && s1.getString("scope") == "stats" && s1.getString("map") == "de_nuke" && s1["noMatches"].type == json::Value::Null &&
          s1.getString("sub") == "Micha · 6 Matches · 144 Runden");
    CHECK(s1["maps"].arr.size() == 1 && s1["maps"].arr[0].getString("label") == "Nuke · 4");
    CHECK(s1["notice"].getString("title") == "Ein Match wurde mit einer älteren FragDesk-Version ausgewertet");
    CHECK(s1.getString("foot").rfind("2 Matches auf Nuke · ", 0) == 0 && s1["legend"]["items"].arr[0].getString("value") == "6 s");
    CHECK(!s1["filter"].getBool("marks") && s1["marks"].arr.empty());   // in den Statistiken ohne Punkte (wie FragDesk)
    json::Value s2 = heat("mode=0&period=2&layers=2&marks=1");
    CHECK(s2["marks"].arr.size() == 2 && s2["matches"].arr.size() == 2 && s2["matches"].arr[1].getString("id") == "n2" &&
          s2["matches"].arr[1].getString("label").rfind("Nuke · 13:11 · ", 0) == 0);
    CHECK(heat("mode=0&period=2&pick=de_mirage").getString("map") == "de_nuke");   // ohne Daten nicht wählbar
    json::Value s3 = heat("mode=0&period=2&map=de_mirage");   // Map-Filter ohne Heatmap-Daten
    CHECK(s3.getString("map") == "de_mirage" && s3["maps"].arr.empty() && s3["empty"].getString("title") == "Noch keine Heatmap-Daten für Mirage");
    json::Value s4 = heat("mode=0&period=2&map=de_nuke&pick=de_mirage");   // Map-Filter: keine Auswahl
    CHECK(s4["maps"].arr.empty() && s4.getString("map") == "de_nuke" && s4.getString("foot").rfind("2 Matches auf Nuke", 0) == 0);
    CHECK(heat("mode=0&period=2&map=de_gibtsnicht")["empty"].getString("title") == "Keine Karte mit Radarbild");
    json::Value s5 = heat("mode=2&period=2");   // FACEIT: keine Matches
    CHECK(s5["noMatches"].getString("title") == "Keine passenden Matches" && s5["maps"].arr.empty());

    // Zwischenspeicher: eine neu hochgeladene Heatmap (andere Größe) wird ohne neue Generation gelesen, geänderte
    // Matches (neue Generation) verwerfen den alten Stand
    files["mem/n1.heat"] = sampleHeat(2, 0);
    CHECK(heat("match=n1")["legend"]["items"].arr[0].getString("value") == "5 s");
    files["mem/n1.heat"] = sampleHeat(2, 1);   // gleiche Größe, die zwei Abtastungen jetzt vom Gegner
    hs.gen++;
    CHECK(heat("match=n1")["legend"]["items"].arr[0].getString("value") == "3 s");

    // Echter Server: Range-Anfrage und Keep-Alive
    Server server;
    std::string data = "0123456789";
    std::string serr;
    bool ok = server.start("127.0.0.1", 0, [&](const Request& rq, Response& rs) {
        if (rq.path == "/f") {
            rs.type = "text/plain";
            rs.fileSize = int64_t(data.size());
            rs.file = [&](int64_t off, char* buf, int64_t len) -> int64_t {
                int64_t n = std::min<int64_t>(len, int64_t(data.size()) - off);
                std::copy(data.begin() + off, data.begin() + off + n, buf);
                return n;
            };
        } else rs.body = "{}";
    }, serr);
    CHECK(ok && server.port() > 0);
    std::string out = rawHttp(server.port(), "GET /f HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\nConnection: close\r\n\r\n");
    CHECK(out.find("206 Partial Content") != std::string::npos && out.find("Content-Range: bytes 2-4/10") != std::string::npos &&
          out.substr(out.size() - 3) == "234");
    out = rawHttp(server.port(), "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /a HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    size_t first = out.find("HTTP/1.1 200"), second = out.find("HTTP/1.1 200", first + 1);
    CHECK(first != std::string::npos && second != std::string::npos);   // zwei Antworten auf einer Verbindung
    out = rawHttp(server.port(), "GARBAGE\r\n\r\n");
    CHECK(out.find("400") != std::string::npos);
    server.stop();
    CHECK(!server.running());

    std::printf("api_test: %d Prüfungen, %d Fehler\n", checks, fails);
    return fails ? 1 : 0;
}
