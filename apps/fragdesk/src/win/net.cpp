// FragDesk – Anfragen an den eigenen Server (siehe net.h)
#include "net.h"

#include <winhttp.h>

#include <algorithm>
#include <cwchar>

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

}  // namespace fdw
