// efficient lamp – Downloads (siehe net.h)
#include "net.h"

#include <windows.h>
#include <winhttp.h>

#include "common.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace lamp {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::wstring winhttpError(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: return L"Zeitüberschreitung – der Server antwortet nicht";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR: return L"keine Verbindung zu GitHub (Internet prüfen)";
        case ERROR_WINHTTP_SECURE_FAILURE: return L"sichere Verbindung fehlgeschlagen (Zertifikat)";
        case ERROR_WINHTTP_REDIRECT_FAILED: return L"Weiterleitung fehlgeschlagen";
        default: return L"Verbindung fehlgeschlagen (Fehler " + std::to_wstring(e) + L")";
    }
}

std::atomic<bool> g_allowLocal{false};

bool startsWith(const std::wstring& s, const wchar_t* prefix) { return s.rfind(prefix, 0) == 0; }

// Request-Handle schließen – außer ein Abbruch hat es schon geschlossen
struct RequestGuard {
    HINTERNET h = nullptr;
    DownloadCancel* cancel = nullptr;
    ~RequestGuard() {
        if (h && (!cancel || cancel->detach())) WinHttpCloseHandle(h);
    }
};

}  // namespace

void DownloadCancel::cancel() {
    EnterCriticalSection(&cs_);
    cancelled_ = true;
    if (request_) {   // bricht auch ein blockierendes WinHttpReadData ab
        WinHttpCloseHandle(static_cast<HINTERNET>(request_));
        request_ = nullptr;
    }
    LeaveCriticalSection(&cs_);
}

bool DownloadCancel::attach(void* request) {
    EnterCriticalSection(&cs_);
    bool ok = !cancelled_;
    if (ok) request_ = request;
    LeaveCriticalSection(&cs_);
    return ok;
}

bool DownloadCancel::detach() {
    EnterCriticalSection(&cs_);
    bool own = request_ != nullptr;
    request_ = nullptr;
    LeaveCriticalSection(&cs_);
    return own;
}

void allowLocalSources(bool allow) { g_allowLocal = allow; }

bool download(const std::wstring& url, std::string& out, std::wstring& error, const Progress& progress, size_t maxBytes, DownloadCancel* cancel) {
    out.clear();
    error.clear();
    const bool isHttps = startsWith(url, L"https://"), isHttp = startsWith(url, L"http://");
    if (!isHttps && !g_allowLocal) {   // im Normalbetrieb nur HTTPS – keine Dateipfade, keine Netzwerkfreigaben
        error = L"unsichere Adresse abgelehnt (nur https://)";
        return false;
    }
    if (!isHttps && !isHttp) {   // Testbetrieb: Katalog/Pakete aus einem Ordner
        std::wstring path = startsWith(url, L"file:///") ? url.substr(8) : url;
        for (wchar_t& c : path)
            if (c == L'/') c = L'\\';
        if (!readFile(path, out, maxBytes)) {
            error = L"Datei nicht gefunden: " + path;
            return false;
        }
        if (progress) progress(out.size(), out.size());
        return true;
    }

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = L"ungültige Adresse";
        return false;
    }
    const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

    std::wstring agent = std::wstring(L"efficient-lamp/") + kVersion;
    Handle session;
    session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        error = winhttpError(GetLastError());
        return false;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // ältere Windows-Versionen kennen TLS 1.3 nicht
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 30000);

    Handle conn;
    conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    RequestGuard req;
    if (conn.h)
        req.h = WinHttpOpenRequest(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) {
        error = winhttpError(GetLastError());
        return false;
    }
    if (cancel) {
        if (!cancel->attach(req.h)) {
            WinHttpCloseHandle(req.h);
            req.h = nullptr;
            error = L"abgebrochen";
            return false;
        }
        req.cancel = cancel;
    }
    auto failed = [&](DWORD e) { return cancel && cancel->cancelled() ? std::wstring(L"abgebrochen") : winhttpError(e); };
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
    if (!WinHttpSendRequest(req.h, L"Cache-Control: no-cache\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        error = failed(GetLastError());
        return false;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = status == 404 ? L"Datei nicht gefunden (404) – gibt es schon ein Release?" : L"Server antwortet mit Fehler " + std::to_wstring(status);
        return false;
    }
    uint64_t total = 0;
    wchar_t lenBuf[32];
    DWORD lenSize = sizeof lenBuf;
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(lenBuf, nullptr, 10);
    if (total > maxBytes) {
        error = L"Datei ist unerwartet groß";
        return false;
    }
    if (total) out.reserve(size_t(total));
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) {
            error = failed(GetLastError());
            return false;
        }
        if (avail == 0) break;
        if (out.size() + avail > maxBytes) {
            error = L"Datei ist unerwartet groß";
            return false;
        }
        size_t old = out.size();
        out.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, &out[old], avail, &got)) {
            error = failed(GetLastError());
            return false;
        }
        out.resize(old + got);
        if (progress && !progress(out.size(), total)) {
            error = L"abgebrochen";
            return false;
        }
    }
    if (total && out.size() != total) {
        error = L"Download unvollständig";
        return false;
    }
    return true;
}

}  // namespace lamp
