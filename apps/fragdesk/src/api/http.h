// FragDesk – kleiner HTTP/1.1-Server für die Handy-App. Läuft in FragDesk (Windows) und im Testserver (Linux),
// ohne Fremdbibliotheken. Bewusst schlank: GET/HEAD/POST, Keep-Alive, Range-Anfragen für Videos, feste Grenzen
// für Kopf, Körper, Verbindungen und Wartezeiten.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fdapi {

struct Request {
    std::string method, path, query, body, remote;
    std::vector<std::pair<std::string, std::string>> headers;   // Namen in Kleinbuchstaben

    std::string header(const std::string& lowerName) const;
    std::string param(const std::string& name) const;   // aus der Query, dekodiert
    std::string cookie(const std::string& name) const;
};

// Liest ab offset bis zu len Bytes nach buf; liefert die Zahl gelesener Bytes (< 0 bei Fehler).
using FileReader = std::function<int64_t(int64_t offset, char* buf, int64_t len)>;

struct Response {
    int status = 200;
    std::string type = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    // statt body: große Datei in Stücken (mit Range-Unterstützung)
    FileReader file;
    int64_t fileSize = -1;

    void error(int code, const std::string& message);   // {"error": "…"}
    void set(const std::string& name, const std::string& value) { headers.push_back({name, value}); }
};

using Handler = std::function<void(const Request&, Response&)>;

class Server {
public:
    Server() = default;
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // addr: "127.0.0.1" (nur dieser PC) oder "0.0.0.0" (alle Netzwerke); port 0 = frei wählen. err auf Deutsch.
    bool start(const std::string& addr, int port, Handler handler, std::string& err);
    void stop();   // beendet die Annahme und bricht laufende Verbindungen ab (wartet höchstens 3 s)
    bool running() const;
    int port() const;

    struct State;

private:
    std::shared_ptr<State> st_;
};

std::string urlDecode(const std::string& s);
std::string statusText(int code);

}  // namespace fdapi
