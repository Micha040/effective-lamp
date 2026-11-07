// Tests für die Handy-App-Schnittstelle (Linux): JSON, Anfragen zerlegen, Kopplung, Routen, echter Server mit Range.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "../src/api/jsonw.h"
#include "../src/api/mobile.h"
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
