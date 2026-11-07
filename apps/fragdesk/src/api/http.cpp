#include "http.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

namespace fdapi {

namespace {

#ifdef _WIN32
using sock_t = SOCKET;
const sock_t kBad = INVALID_SOCKET;
void closeSock(sock_t s) { closesocket(s); }
void shutSock(sock_t s) { shutdown(s, SD_BOTH); }
#else
using sock_t = int;
const sock_t kBad = -1;
void closeSock(sock_t s) { close(s); }
void shutSock(sock_t s) { shutdown(s, SHUT_RDWR); }
#endif

const int kMaxConnections = 24;            // gleichzeitig; ein Handy braucht selten mehr als 6
const int kTimeoutMs = 20000;              // Lesen/Schreiben, auch Leerlauf zwischen zwei Anfragen
const size_t kMaxHeader = 16 * 1024;
const size_t kMaxBody = 512 * 1024;
const int kMaxRequestsPerConnection = 1000;
const int64_t kChunk = 256 * 1024;

void setOptions(sock_t s) {
#ifdef _WIN32
    DWORD t = kTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    BOOL one = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
#else
    timeval tv{kTimeoutMs / 1000, (kTimeoutMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#endif
}

bool sendAll(sock_t s, const char* p, size_t n) {
    while (n > 0) {
        int part = int(std::min<size_t>(n, 1 << 20));
#ifdef _WIN32
        int k = send(s, p, part, 0);
#else
        ssize_t k = send(s, p, size_t(part), MSG_NOSIGNAL);
#endif
        if (k <= 0) return false;
        p += k;
        n -= size_t(k);
    }
    return true;
}
bool sendAll(sock_t s, const std::string& data) { return sendAll(s, data.data(), data.size()); }

std::string lower(std::string s) {
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool parseInt64(const std::string& s, int64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    int64_t v = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
    }
    out = v;
    return true;
}

// "bytes=a-b", "bytes=a-", "bytes=-n" (nur ein Bereich). false = nicht erfüllbar.
bool parseRange(const std::string& header, int64_t size, int64_t& from, int64_t& to) {
    std::string h = trim(header);
    if (h.compare(0, 6, "bytes=") != 0 || h.find(',') != std::string::npos) return false;
    std::string spec = h.substr(6);
    size_t dash = spec.find('-');
    if (dash == std::string::npos || size <= 0) return false;
    std::string a = trim(spec.substr(0, dash)), b = trim(spec.substr(dash + 1));
    int64_t x = 0, y = 0;
    if (a.empty()) {   // die letzten n Bytes
        if (!parseInt64(b, y) || y == 0) return false;
        from = std::max<int64_t>(0, size - y);
        to = size - 1;
        return true;
    }
    if (!parseInt64(a, x) || x >= size) return false;
    if (b.empty()) to = size - 1;
    else {
        if (!parseInt64(b, y) || y < x) return false;
        to = std::min<int64_t>(y, size - 1);
    }
    from = x;
    return true;
}

void plainResponse(sock_t s, int code) {
    std::string body = "{\"error\":\"" + statusText(code) + "\"}";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + statusText(code) +
                    "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body;
    sendAll(s, h);
}

}  // namespace

// ---- Request / Response -----------------------------------------------------------------------------------------
std::string Request::header(const std::string& lowerName) const {
    for (const auto& kv : headers)
        if (kv.first == lowerName) return kv.second;
    return std::string();
}

std::string Request::param(const std::string& name) const {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string part = query.substr(pos, amp - pos);
        size_t eq = part.find('=');
        std::string k = urlDecode(part.substr(0, eq));
        if (k == name) return eq == std::string::npos ? std::string() : urlDecode(part.substr(eq + 1));
        pos = amp + 1;
    }
    return std::string();
}

std::string Request::cookie(const std::string& name) const {
    std::string all = header("cookie");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t semi = all.find(';', pos);
        if (semi == std::string::npos) semi = all.size();
        std::string part = trim(all.substr(pos, semi - pos));
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == name) return part.substr(eq + 1);
        pos = semi + 1;
    }
    return std::string();
}

void Response::error(int code, const std::string& message) {
    status = code;
    type = "application/json; charset=utf-8";
    std::string esc;
    for (char ch : message) {
        if (ch == '"' || ch == '\\') esc += '\\';
        if (static_cast<unsigned char>(ch) >= 0x20) esc += ch;
    }
    body = "{\"error\":\"" + esc + "\"}";
    file = nullptr;
    fileSize = -1;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char ch = s[i];
        if (ch == '+') out += ' ';
        else if (ch == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) && isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += ch;
    }
    return out;
}

std::string statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 416: return "Range Not Satisfiable";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 507: return "Insufficient Storage";
        default: return "Status";
    }
}

// ---- Server -----------------------------------------------------------------------------------------------------
struct Server::State {
    std::mutex mu;
    std::condition_variable cv;
    std::set<sock_t> conns;
    int active = 0;
    sock_t listen = kBad;
    std::thread acceptor;
    Handler handler;
    BodyLimit bodyLimit;
    std::atomic<bool> running{false};
    int port = 0;
};

namespace {

bool writeResponse(sock_t s, const Request& req, Response& res, bool keep) {
    bool head = req.method == "HEAD";
    int64_t from = 0, length = 0;
    std::string extra;
    if (res.file && res.fileSize >= 0) {
        length = res.fileSize;
        std::string range = req.header("range");
        if (res.status == 200 && !range.empty()) {
            int64_t a = 0, b = 0;
            if (parseRange(range, res.fileSize, a, b)) {
                res.status = 206;
                from = a;
                length = b - a + 1;
                extra += "Content-Range: bytes " + std::to_string(a) + "-" + std::to_string(b) + "/" + std::to_string(res.fileSize) + "\r\n";
            } else {
                res.status = 416;
                res.file = nullptr;
                res.body.clear();
                length = 0;
                extra += "Content-Range: bytes */" + std::to_string(res.fileSize) + "\r\n";
            }
        }
        extra += "Accept-Ranges: bytes\r\n";
    } else {
        res.file = nullptr;
        length = int64_t(res.body.size());
    }
    std::string h = "HTTP/1.1 " + std::to_string(res.status) + " " + statusText(res.status) + "\r\n";
    h += "Content-Type: " + res.type + "\r\n";
    h += "Content-Length: " + std::to_string(length) + "\r\n";
    h += extra;
    h += "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n";
    for (const auto& kv : res.headers) h += kv.first + ": " + kv.second + "\r\n";
    h += keep ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    if (!sendAll(s, h)) return false;
    if (head) return true;
    if (!res.file) return sendAll(s, res.body);
    std::vector<char> buf(size_t(std::min<int64_t>(kChunk, std::max<int64_t>(length, 1))));
    int64_t pos = from, left = length;
    while (left > 0) {
        int64_t want = std::min<int64_t>(left, int64_t(buf.size()));
        int64_t got = res.file(pos, buf.data(), want);
        if (got <= 0) return false;   // Datei kürzer als angekündigt: Verbindung abbrechen
        if (!sendAll(s, buf.data(), size_t(got))) return false;
        pos += got;
        left -= got;
    }
    return true;
}

void serveConnection(std::shared_ptr<Server::State> st, sock_t s, std::string remote) {
    setOptions(s);
    std::string buf;
    char chunk[8192];
    for (int served = 0; served < kMaxRequestsPerConnection && st->running; ++served) {
        // Kopf
        size_t end = std::string::npos;
        bool ok = true;
        while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
            if (buf.size() > kMaxHeader) {
                plainResponse(s, 431);
                ok = false;
                break;
            }
            int k = int(recv(s, chunk, sizeof chunk, 0));
            if (k <= 0) {
                ok = false;
                break;
            }
            buf.append(chunk, size_t(k));
        }
        if (!ok) break;
        std::string head = buf.substr(0, end);
        buf.erase(0, end + 4);

        Request req;
        req.remote = remote;
        size_t lineEnd = head.find("\r\n");
        std::string line = head.substr(0, lineEnd);
        size_t sp1 = line.find(' '), sp2 = line.rfind(' ');
        if (sp1 == std::string::npos || sp2 == sp1) {
            plainResponse(s, 400);
            break;
        }
        req.method = line.substr(0, sp1);
        std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1), version = line.substr(sp2 + 1);
        if (target.empty() || target[0] != '/' || version.compare(0, 5, "HTTP/") != 0) {
            plainResponse(s, 400);
            break;
        }
        size_t q = target.find('?');
        req.path = urlDecode(target.substr(0, q));
        if (q != std::string::npos) req.query = target.substr(q + 1);
        size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
        while (pos < head.size()) {
            size_t e = head.find("\r\n", pos);
            if (e == std::string::npos) e = head.size();
            std::string hl = head.substr(pos, e - pos);
            size_t colon = hl.find(':');
            if (colon != std::string::npos) req.headers.push_back({lower(trim(hl.substr(0, colon))), trim(hl.substr(colon + 1))});
            pos = e + 2;
        }
        // Körper (nur mit Content-Length)
        if (!req.header("transfer-encoding").empty()) {
            plainResponse(s, 501);
            break;
        }
        int64_t len = 0;
        std::string cl = req.header("content-length");
        if (!cl.empty() && !parseInt64(cl, len)) {
            plainResponse(s, 400);
            break;
        }
        if (len > (st->bodyLimit ? st->bodyLimit(req) : int64_t(kMaxBody))) {
            plainResponse(s, 413);
            break;
        }
        while (int64_t(buf.size()) < len) {
            int k = int(recv(s, chunk, sizeof chunk, 0));
            if (k <= 0) {
                ok = false;
                break;
            }
            buf.append(chunk, size_t(k));
        }
        if (!ok) break;
        req.body = buf.substr(0, size_t(len));
        buf.erase(0, size_t(len));

        std::string conn = lower(req.header("connection"));
        bool keep = version == "HTTP/1.1" ? conn != "close" : conn == "keep-alive";
        Response res;
        try {
            st->handler(req, res);
        } catch (...) {
            res = Response();
            res.error(500, "Interner Fehler");
        }
        if (!writeResponse(s, req, res, keep) || !keep) break;
    }
    std::lock_guard<std::mutex> lk(st->mu);
    st->conns.erase(s);
    closeSock(s);
    --st->active;
    st->cv.notify_all();
}

void acceptLoop(std::shared_ptr<Server::State> st) {
    while (st->running) {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        sock_t c = accept(st->listen, reinterpret_cast<sockaddr*>(&a), &al);
        if (c == kBad) {
            if (!st->running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        char ip[64] = "";
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->running && st->active < kMaxConnections) {
                ++st->active;
                st->conns.insert(c);
                accepted = true;
            }
        }
        if (!accepted) {
            setOptions(c);
            plainResponse(c, 503);
            closeSock(c);
            continue;
        }
        std::thread(serveConnection, st, c, std::string(ip)).detach();
    }
}

}  // namespace

Server::~Server() { stop(); }

bool Server::start(const std::string& addr, int port, Handler handler, std::string& err, BodyLimit limit) {
    stop();
#ifdef _WIN32
    static bool wsa = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    if (!wsa) {
        err = "Das Netzwerk von Windows ist nicht verfügbar.";
        return false;
    }
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, addr.c_str(), &a.sin_addr) != 1) {
        err = "Ungültige Adresse " + addr + ".";
        return false;
    }
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBad) {
        err = "Der Server lässt sich nicht starten.";
        return false;
    }
#ifdef _WIN32
    BOOL excl = TRUE;   // kein anderes Programm darf denselben Port mitbenutzen
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof excl);
#else
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        err = "Port " + std::to_string(port) + " ist schon belegt.";
        closeSock(s);
        return false;
    }
    if (listen(s, 16) != 0) {
        err = "Der Server lässt sich nicht starten.";
        closeSock(s);
        return false;
    }
    sockaddr_in bound{};
    socklen_t bl = sizeof bound;
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &bl);
    auto st = std::make_shared<State>();
    st->listen = s;
    st->handler = std::move(handler);
    st->bodyLimit = std::move(limit);
    st->port = ntohs(bound.sin_port);
    st->running = true;
    st->acceptor = std::thread(acceptLoop, st);
    st_ = st;
    return true;
}

void Server::stop() {
    std::shared_ptr<State> st = std::move(st_);
    if (!st) return;
    st->running = false;
    shutSock(st->listen);
    closeSock(st->listen);
    if (st->acceptor.joinable()) st->acceptor.join();
    std::unique_lock<std::mutex> lk(st->mu);
    for (sock_t c : st->conns) shutSock(c);   // laufende Antworten abbrechen
    st->cv.wait_for(lk, std::chrono::seconds(3), [&] { return st->active == 0; });
}

bool Server::running() const { return st_ && st_->running; }
int Server::port() const { return st_ ? st_->port : 0; }

}  // namespace fdapi
