// FragDesk – Anfragen an den eigenen Server über WinHTTP: HTTPS, Proxy wie in Windows eingestellt, feste Wartezeiten,
// keine Weiterleitungen. Synchron – nur aus Hintergrund-Threads aufrufen, nie aus dem UI-Thread.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace fdw {

// Adresse eines Servers, z. B. https://203-0-113-5.sslip.io/ oder https://example.org:8443/fragdesk/
struct ServerUrl {
    bool secure = true;
    std::wstring host;        // ohne eckige Klammern (IPv6)
    int port = 443;
    std::string path = "/";   // beginnt und endet mit "/"
    std::string text() const;    // wieder als Adresse (Standard-Port weggelassen)
    std::wstring label() const;  // für die Anzeige: Host[:Port][Pfad]
};

// Prüft und zerlegt eine Adresse. Nur https:// – http:// nur zu diesem PC (localhost, 127.x.x.x, [::1]), sonst ginge der
// Schlüssel unverschlüsselt durchs Netz. Anfrage (?…) und Anker (#…) werden ignoriert.
bool parseServerUrl(const std::string& url, ServerUrl& out);

struct HttpRequest {
    const wchar_t* method = L"GET";
    std::string path;                  // hinter der Server-Adresse, z. B. "api/v1/sync/state"
    std::string token;                 // Konto-Token (leer = ohne Anmeldung)
    std::string contentType;           // leer = ohne Körper
    const char* body = nullptr;        // Körper (gehört dem Aufrufer)
    size_t bodySize = 0;
    size_t maxBody = 1u << 20;         // längere Antworten werden abgebrochen
    std::function<bool()> cancel;      // vor jedem Stück des Körpers gefragt: true = abbrechen (Pause, Beenden)
};

struct HttpResponse {
    int status = 0;           // HTTP-Status; 0 = keine (vollständige) Antwort
    DWORD error = 0;          // WinHTTP-Fehler, wenn status == 0
    bool cancelled = false;   // über cancel oder abort() abgebrochen
    std::string body;
};

// Fehler ohne Antwort als Text („Zeitüberschreitung“, „Zertifikat ungültig“ …)
std::wstring netErrorText(DWORD error);

// Eine Sitzung (Verbindungen werden wiederverwendet); WinHTTP wird erst mit der ersten Anfrage geladen
class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse send(const ServerUrl& server, const HttpRequest& req);   // immer aus demselben Thread
    void abort();   // laufende und alle weiteren Anfragen abbrechen (aus einem anderen Thread, beim Beenden)

private:
    bool open();
    void* session_ = nullptr;   // HINTERNET
    DWORD openError_ = 0;
    std::mutex mu_;
    void* current_ = nullptr;   // laufende Anfrage (HINTERNET), damit abort() sie schließen kann
    bool aborted_ = false;
};

}  // namespace fdw
