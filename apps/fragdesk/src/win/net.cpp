// FragDesk – Anfragen an den eigenen Server (siehe net.h)
#include "net.h"

#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <vector>

#include "common.h"

namespace fdw {

namespace {

bool isDigits(const std::wstring& s) { return !s.empty() && s.find_first_not_of(L"0123456789") == std::wstring::npos; }

// Name (sslip.io, eigene Domain) oder IPv4-Adresse; IPv6 in Klammern wird nicht unterstützt
bool validHost(const std::wstring& h) {
    if (h.empty() || h.size() > 253 || h.front() == L'.' || h.front() == L'-' || h.back() == L'-') return false;
    for (wchar_t ch : h)
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'.')) return false;
    return h.find(L"..") == std::wstring::npos;
}

bool isLoopback(const std::wstring& host) {
    std::wstring h = lowerCase(host);
    if (h == L"localhost") return true;
    unsigned a = 0, b = 0, c = 0, d = 0;
    wchar_t rest = 0;
    return swscanf(h.c_str(), L"%u.%u.%u.%u%lc", &a, &b, &c, &d, &rest) == 4 && a == 127 && b < 256 && c < 256 && d < 256;
}

}  // namespace

std::string ServerUrl::text() const {
    std::string s = (secure ? "https://" : "http://") + narrow(host);
    if (port != (secure ? 443 : 80)) s += ":" + std::to_string(port);
    return s + path;
}

std::wstring ServerUrl::label() const {
    std::wstring s = host;
    if (port != (secure ? 443 : 80)) s += L":" + std::to_wstring(port);
    if (path != "/") s += widen(path.substr(0, path.size() - 1));
    return s;
}

bool parseServerUrl(const std::string& url, ServerUrl& out) {
    std::wstring u = widen(url);
    if (u.empty() || u.size() > 300) return false;
    for (wchar_t ch : u)
        if (ch <= 32 || ch == 127 || wcschr(L"\"<>\\^`{|}", ch)) return false;
    ServerUrl r;
    size_t start = 0;
    std::wstring scheme = lowerCase(u.substr(0, 8));
    if (scheme.rfind(L"https://", 0) == 0) {
        r.secure = true;
        r.port = 443;
        start = 8;
    } else if (scheme.rfind(L"http://", 0) == 0) {
        r.secure = false;
        r.port = 80;
        start = 7;
    } else return false;
    size_t end = u.find_first_of(L"/?#", start);
    std::wstring authority = u.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (authority.empty() || authority.find(L'@') != std::wstring::npos) return false;   // keine Zugangsdaten in der Adresse
    std::wstring host = authority;
    size_t colon = authority.find(L':');
    if (colon != std::wstring::npos) {
        std::wstring p = authority.substr(colon + 1);
        if (!isDigits(p) || p.size() > 5) return false;
        int port = _wtoi(p.c_str());
        if (port < 1 || port > 65535) return false;
        r.port = port;
        host = authority.substr(0, colon);
    }
    if (!validHost(host)) return false;
    if (!r.secure && !isLoopback(host)) return false;   // ohne HTTPS nur zu diesem PC
    r.host = lowerCase(host);
    r.path = "/";
    if (end != std::wstring::npos && u[end] == L'/') {
        size_t pe = u.find_first_of(L"?#", end);
        r.path = narrow(u.substr(end, pe == std::wstring::npos ? std::wstring::npos : pe - end));
        if (r.path.find("/../") != std::string::npos || r.path.find("/./") != std::string::npos) return false;
    }
    if (r.path.back() != '/') r.path += '/';
    out = r;
    return true;
}

bool parseDemoUrl(const std::string& url, WebUrl& out) {
    std::wstring u = widen(url);
    if (u.size() < 12 || u.size() > 512) return false;
    for (wchar_t ch : u)
        if (ch <= 32 || ch >= 127 || wcschr(L"\"<>\\^`{|}", ch)) return false;
    WebUrl r;
    size_t start;
    std::wstring scheme = lowerCase(u.substr(0, 8));
    if (scheme.rfind(L"https://", 0) == 0) {
        r.secure = true;
        r.port = 443;
        start = 8;
    } else if (scheme.rfind(L"http://", 0) == 0) {
        r.secure = false;
        r.port = 80;
        start = 7;
    } else return false;
    size_t end = u.find_first_of(L"/?#", start);
    if (end == std::wstring::npos || u[end] != L'/') return false;
    std::wstring authority = u.substr(start, end - start), host = authority;
    if (authority.find(L'@') != std::wstring::npos) return false;
    size_t colon = authority.find(L':');
    if (colon != std::wstring::npos) {
        std::wstring p = authority.substr(colon + 1);
        if (!isDigits(p) || p.size() > 5) return false;
        r.port = _wtoi(p.c_str());
        if (r.port < 1 || r.port > 65535) return false;
        host = authority.substr(0, colon);
    }
    host = lowerCase(host);
    if (!validHost(host)) return false;
    auto under = [&](const wchar_t* domain) { return endsWithI(host, std::wstring(L".") + domain) || host == domain; };
    if (!under(L"valve.net") && !under(L"wmsj.cn") && !isLoopback(host)) return false;
    size_t q = u.find_first_of(L"?#", end);
    std::wstring path = u.substr(end, q == std::wstring::npos ? std::wstring::npos : q - end);
    if (path.find(L"/../") != std::wstring::npos || path.find(L"/./") != std::wstring::npos) return false;
    bool demo = false;
    for (const wchar_t* ext : {L".dem.bz2", L".dem.gz", L".dem.zst", L".dem"})
        if (endsWithI(path, ext) && path.size() > wcslen(ext) + 1) demo = true;
    if (!demo) return false;
    size_t hash = u.find(L'#', end);
    r.host = host;
    r.path = narrow(u.substr(end, hash == std::wstring::npos ? std::wstring::npos : hash - end));
    out = r;
    return true;
}

std::wstring netErrorText(DWORD error) {
    switch (error) {
        case ERROR_WINHTTP_TIMEOUT: return L"Zeitüberschreitung";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return L"Adresse nicht gefunden – kein Internet oder Server-Adresse falsch";
        case ERROR_WINHTTP_CANNOT_CONNECT: return L"keine Verbindung – läuft der Server?";
        case ERROR_WINHTTP_CONNECTION_ERROR: return L"Verbindung abgebrochen";
        case ERROR_WINHTTP_OPERATION_CANCELLED: return L"abgebrochen";
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return L"ungültige Antwort";
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR: return L"keine sichere Verbindung (Zertifikat ungültig oder kein HTTPS?)";
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return L"das Zertifikat des Servers ist ungültig";
        case ERROR_WINHTTP_SECURE_FAILURE_PROXY: return L"der Proxy lässt keine sichere Verbindung zu";
        default: {
            wchar_t b[48];
            swprintf(b, 48, L"Netzwerkfehler %lu", static_cast<unsigned long>(error));
            return b;
        }
    }
}

bool HttpClient::open() {
    if (session_) return true;
    if (openError_) return false;
    std::wstring agent = std::wstring(L"FragDesk/") + kVersion;
    // Proxy wie in Windows eingestellt, samt automatischer Erkennung (ab Windows 8.1); sonst die Standardeinstellung
    HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        DWORD err = GetLastError();
        openError_ = err ? err : DWORD(ERROR_WINHTTP_INTERNAL_ERROR);
        logLine(L"Server: WinHTTP startet nicht (%lu)", static_cast<unsigned long>(openError_));
        return false;
    }
    WinHttpSetTimeouts(s, 15000, 15000, 30000, 60000);   // Namen auflösen, verbinden, je Stück senden, auf Antwort warten
    DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto)) {
        proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto);
    }
    DWORD gzip = WINHTTP_DECOMPRESSION_FLAG_ALL;   // Caddy packt JSON (ab Windows 8.1; sonst eben ungepackt)
    WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &gzip, sizeof gzip);
    session_ = s;
    return true;
}

HttpClient::~HttpClient() {
    if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

void HttpClient::abort() {
    std::lock_guard<std::mutex> lk(mu_);
    aborted_ = true;
    if (current_) {   // bricht das Warten in send() ab (ERROR_WINHTTP_OPERATION_CANCELLED)
        WinHttpCloseHandle(static_cast<HINTERNET>(current_));
        current_ = nullptr;
    }
}

HttpResponse HttpClient::send(const ServerUrl& server, const HttpRequest& rq) {
    HttpResponse r;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (aborted_) {
            r.cancelled = true;
            r.error = ERROR_WINHTTP_OPERATION_CANCELLED;
            return r;
        }
    }
    if (!open()) {
        r.error = openError_;
        return r;
    }
    HINTERNET con = WinHttpConnect(static_cast<HINTERNET>(session_), server.host.c_str(), INTERNET_PORT(server.port), 0);
    if (!con) {
        r.error = GetLastError();
        return r;
    }
    std::wstring object = widen(server.path + rq.path);
    HINTERNET req = WinHttpOpenRequest(con, rq.method, object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       server.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        r.error = GetLastError();
        WinHttpCloseHandle(con);
        return r;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (aborted_) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            r.cancelled = true;
            r.error = ERROR_WINHTTP_OPERATION_CANCELLED;
            return r;
        }
        current_ = req;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;   // der Schlüssel geht nur an diese Adresse
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);

    std::wstring headers = L"X-FragDesk: 1\r\nAccept: application/json\r\n";
    if (!rq.token.empty()) headers += L"Authorization: Bearer " + widen(rq.token) + L"\r\n";
    if (!rq.contentType.empty()) headers += L"Content-Type: " + widen(rq.contentType) + L"\r\n";
    BOOL ok = WinHttpSendRequest(req, headers.c_str(), DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, DWORD(rq.bodySize), 0);
    // Körper in Stücken: dazwischen lässt sich abbrechen (CS2 startet, FragDesk wird beendet)
    size_t sent = 0;
    while (ok && sent < rq.bodySize) {
        if (rq.cancel && rq.cancel()) {
            r.cancelled = true;
            ok = FALSE;
            break;
        }
        DWORD piece = DWORD(std::min<size_t>(256 * 1024, rq.bodySize - sent)), wrote = 0;
        ok = WinHttpWriteData(req, rq.body + sent, piece, &wrote) && wrote > 0;
        sent += wrote;
    }
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (ok) {
        DWORD code = 0, len = sizeof code;
        ok = WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                 WINHTTP_NO_HEADER_INDEX);
        char buf[16384];
        while (ok) {
            if (rq.cancel && rq.cancel()) {   // z. B. CS2 startet, während 2D-Daten vom Server kommen
                r.cancelled = true;
                ok = FALSE;
                break;
            }
            DWORD got = 0;
            ok = WinHttpReadData(req, buf, sizeof buf, &got);
            if (!ok || got == 0) break;
            if (r.body.size() + got > rq.maxBody) {   // viel zu lang: das ist keine Antwort, die FragDesk erwartet
                ok = FALSE;
                SetLastError(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
                break;
            }
            r.body.append(buf, got);
        }
        if (ok) r.status = int(code);
    }
    if (!ok && !r.cancelled) r.error = GetLastError();
    bool mine = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        mine = current_ == req;
        if (mine) current_ = nullptr;
        if (aborted_) r.cancelled = true;
    }
    if (mine) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    if (r.cancelled) {
        r.status = 0;
        r.error = ERROR_WINHTTP_OPERATION_CANCELLED;
    }
    if (!ok) r.body.clear();
    return r;
}

bool HttpClient::begin(void* req) {
    std::lock_guard<std::mutex> lk(mu_);
    if (aborted_) return false;
    current_ = req;
    return true;
}

bool HttpClient::end(void* req) {
    std::lock_guard<std::mutex> lk(mu_);
    bool mine = current_ == req;
    if (mine) current_ = nullptr;
    return mine;
}

namespace {

std::string queryHeader(HINTERNET req, DWORD which) {
    DWORD len = 0;
    WinHttpQueryHeaders(req, which, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0 || len > 4096) return std::string();
    std::wstring w(len / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(req, which, WINHTTP_HEADER_NAME_BY_INDEX, &w[0], &len, WINHTTP_NO_HEADER_INDEX)) return std::string();
    w.resize(len / sizeof(wchar_t));
    return narrow(w);
}

int64_t toInt(const std::string& s) {
    if (s.empty() || s.size() > 18 || s.find_first_not_of("0123456789") != std::string::npos) return -1;
    return _strtoi64(s.c_str(), nullptr, 10);
}

}  // namespace

DownloadResult HttpClient::download(const DownloadRequest& rq) {
    DownloadResult r;
    if (!open()) {
        r.error = openError_;
        return r;
    }
    const std::wstring part = rq.file + L".part";
    int64_t have = std::max<int64_t>(0, fileSize(part));
    HINTERNET con = WinHttpConnect(static_cast<HINTERNET>(session_), rq.url.host.c_str(), INTERNET_PORT(rq.url.port), 0);
    if (!con) {
        r.error = GetLastError();
        return r;
    }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", widen(rq.url.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       rq.url.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        r.error = GetLastError();
        WinHttpCloseHandle(con);
        return r;
    }
    if (!begin(req)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        r.cancelled = true;
        return r;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP, hops = 3, none = 0;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    WinHttpSetOption(req, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &hops, sizeof hops);
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &none, sizeof none);   // die Demo selbst, Byte für Byte (Range)
    std::wstring headers = L"Accept: */*\r\n";
    if (have > 0) headers += L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
    BOOL ok = WinHttpSendRequest(req, headers.c_str(), DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr);
    HANDLE out = INVALID_HANDLE_VALUE;
    bool discard = false;   // zu groß oder keine Demo: nichts davon behalten
    if (ok) {
        DWORD code = 0, len = sizeof code;
        ok = WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                 WINHTTP_NO_HEADER_INDEX);
        r.status = ok ? int(code) : 0;
    }
    if (ok) {
        int64_t length = toInt(queryHeader(req, WINHTTP_QUERY_CONTENT_LENGTH));
        if (r.status == 416 && have > 0) {   // .part passt nicht (mehr) zur Datei dort: neu anfangen
            DeleteFileW(part.c_str());
            r.problem = L"Der Download passte nicht zum vorigen Stand – bitte noch einmal laden.";
            ok = FALSE;
        } else if (r.status == 404 || r.status == 410) {
            r.gone = true;
            ok = FALSE;
        } else if (r.status == 200 || (r.status == 206 && have > 0)) {
            int64_t from = r.status == 206 ? have : 0;
            if (r.status == 206) {   // Content-Range: bytes <a>-<b>/<gesamt> – muss genau dort weitergehen
                std::string cr = queryHeader(req, WINHTTP_QUERY_CONTENT_RANGE);
                size_t sp = cr.find(' '), dash = cr.find('-'), slash = cr.find('/');
                int64_t a = sp != std::string::npos && dash != std::string::npos ? toInt(cr.substr(sp + 1, dash - sp - 1)) : -1;
                r.total = slash != std::string::npos ? toInt(cr.substr(slash + 1)) : -1;
                if (a != have) {
                    DeleteFileW(part.c_str());
                    r.problem = L"Der Server setzt den Download nicht an der richtigen Stelle fort – bitte noch einmal laden.";
                    ok = FALSE;
                }
            } else r.total = length;
            if (ok && r.total > rq.maxBytes) {
                r.problem = L"Die Demo ist zu groß (" + std::to_wstring(r.total >> 20) + L" MB, höchstens " + std::to_wstring(rq.maxBytes >> 20) + L" MB).";
                discard = true;
                ok = FALSE;
            }
            if (ok) {
                out = CreateFileW(part.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                LARGE_INTEGER pos;
                pos.QuadPart = from;
                if (out == INVALID_HANDLE_VALUE || !SetFilePointerEx(out, pos, nullptr, FILE_BEGIN) || !SetEndOfFile(out)) {
                    r.problem = L"Die Demo lässt sich nicht speichern (" + std::to_wstring(GetLastError()) + L").";
                    ok = FALSE;
                } else r.done = from;
            }
        } else ok = FALSE;   // anderer Status: der Aufrufer sagt, was los ist
    }
    std::vector<char> buf(256 * 1024);
    bool checked = r.done > 0;
    while (ok && out != INVALID_HANDLE_VALUE) {
        if (rq.cancel && rq.cancel()) {
            r.cancelled = true;
            break;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), DWORD(buf.size()), &got)) {   // Verbindung weg, 60 s ohne Daten …
            r.error = GetLastError();
            r.interrupted = true;
            ok = FALSE;
            break;
        }
        if (got == 0) break;   // fertig
        if (!checked) {        // die ersten Bytes: ist das überhaupt eine Demo?
            checked = true;
            if (rq.check && !rq.check(buf.data(), got)) {
                r.problem = L"Die Antwort ist keine Demo.";
                discard = true;
                ok = FALSE;
                break;
            }
        }
        if (r.done + int64_t(got) > rq.maxBytes) {
            r.problem = L"Die Demo ist zu groß (über " + std::to_wstring(rq.maxBytes >> 20) + L" MB).";
            discard = true;
            ok = FALSE;
            break;
        }
        DWORD wrote = 0;
        if (!WriteFile(out, buf.data(), got, &wrote, nullptr) || wrote != got) {
            r.problem = L"Die Demo lässt sich nicht speichern (Platte voll?).";
            ok = FALSE;
            break;
        }
        r.done += got;
        if (rq.progress) rq.progress(r.done, r.total);
    }
    if (!ok && r.status == 0 && !r.cancelled) r.error = GetLastError();
    bool finished = ok && !r.cancelled && out != INVALID_HANDLE_VALUE;
    if (out != INVALID_HANDLE_VALUE) {
        if (finished) FlushFileBuffers(out);
        CloseHandle(out);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (aborted_) r.cancelled = true;
    }
    if (end(req)) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    if (discard) DeleteFileW(part.c_str());
    if (!r.problem.empty()) return r;
    if (finished && !r.cancelled) {
        if (r.total >= 0 && r.done != r.total) {   // vorzeitig zu: beim nächsten Versuch an der Stelle weiter
            r.interrupted = true;
            return r;
        }
        if (r.done <= 0 || !MoveFileExW(part.c_str(), rq.file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            r.problem = L"Die Demo lässt sich nicht speichern (" + std::to_wstring(GetLastError()) + L").";
            return r;
        }
        r.complete = true;
    }
    return r;
}

}  // namespace fdw
