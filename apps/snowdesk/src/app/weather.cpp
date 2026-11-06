// SnowDesk – echtes Wetter abrufen (siehe weather.h)
#include "weather.h"

#include <winhttp.h>

#include <atomic>
#include <cstdio>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace app {

namespace {

std::atomic<int> g_inflight{0};   // global: ein Thread kann das App-Objekt überleben (Beenden während einer Anfrage)

struct Job {
    int kind;
    unsigned id;
    std::wstring host, path;
    HWND target;
};

bool httpsGet(const std::wstring& host, const std::wstring& path, std::string& body, std::wstring& err) {
    body.clear();
    HINTERNET session = WinHttpOpen(L"SnowDesk/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"SnowDesk/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        err = L"Internet nicht verfügbar (WinHTTP " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    WinHttpSetTimeouts(session, 8000, 8000, 10000, 10000);
    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                         : nullptr;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            if (body.size() + avail > 512 * 1024) break;   // Antworten sind klein (wenige KB)
            size_t old = body.size();
            body.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, &body[old], avail, &read)) {
                body.resize(old);
                break;
            }
            body.resize(old + read);
            if (read == 0) break;
        }
        if (status == 200) ok = true;
        else err = L"Wetterdienst antwortet mit " + std::to_wstring(status);
    } else {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_TIMEOUT) err = L"Wetterdienst antwortet nicht (Zeitüberschreitung)";
        else if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED || e == ERROR_WINHTTP_CANNOT_CONNECT) err = L"keine Internetverbindung";
        else err = L"Verbindung fehlgeschlagen (" + std::to_wstring(e) + L")";
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

DWORD WINAPI worker(LPVOID param) {
    Job* job = static_cast<Job*>(param);
    auto* reply = new WeatherReply();
    reply->kind = job->kind;
    reply->id = job->id;
    std::string body;
    if (httpsGet(job->host, job->path, body, reply->error)) {
        if (job->kind == 0) {
            reply->ok = wx::parseCurrent(body, reply->weather);
            if (!reply->ok) reply->error = L"Wetterdaten unverständlich";
        } else {
            reply->ok = wx::parsePlaces(body, reply->places);
            if (!reply->ok) reply->error = L"Ortssuche unverständlich";
        }
    }
    if (!job->target || !PostMessageW(job->target, WM_APP_WEATHER, 0, reinterpret_cast<LPARAM>(reply))) delete reply;
    delete job;
    --g_inflight;
    return 0;
}

std::wstring coord(double v) {
    wchar_t b[32];
    _snwprintf(b, 31, L"%.4f", v);
    b[31] = 0;
    return b;
}

}  // namespace

bool WeatherClient::busy() const { return g_inflight.load() > 0; }

unsigned WeatherClient::start(int kind, const std::wstring& path) {
    if (!hwnd_) return 0;
    const unsigned id = next_++;
    auto* job = new Job{kind, id, kind == 0 ? L"api.open-meteo.com" : L"geocoding-api.open-meteo.com", path, hwnd_};
    ++g_inflight;
    HANDLE t = CreateThread(nullptr, 256 * 1024, worker, job, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) {
        --g_inflight;
        delete job;
        logLine(L"Wetter: Thread nicht gestartet (%lu)", GetLastError());
        return 0;
    }
    SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(t);   // job gehört jetzt dem Thread
    return id;
}

unsigned WeatherClient::fetchWeather(double lat, double lon) {
    std::wstring path = L"/v1/forecast?latitude=" + coord(lat) + L"&longitude=" + coord(lon) +
                        L"&current=temperature_2m,precipitation,rain,showers,snowfall,weather_code,cloud_cover,wind_speed_10m,"
                        L"wind_direction_10m,wind_gusts_10m,is_day&wind_speed_unit=kmh&timezone=auto";
    return start(0, path);
}

unsigned WeatherClient::searchPlace(const std::wstring& name) {
    std::string q = wx::urlEncode(name);
    std::wstring wq(q.begin(), q.end());   // nur ASCII nach dem Kodieren
    return start(1, L"/v1/search?name=" + wq + L"&count=6&language=de&format=json");
}

}  // namespace app
