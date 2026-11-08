// fragdesk-server – alle Anfragen. Neu: Konto, Abgleich (Hochladen aus FragDesk), Admin, Befehlszeile, Gesundheit.
// Die bestehenden Routen der Handy-App beantwortet fdapi::handle wie in FragDesk – mit den Daten des Kontos, zu dem
// das Geräte-Token gehört.
//
// Anmeldung: Konto-Token (FragDesk) als „Authorization: Bearer …“ nur für /api/v1/account/*, /api/v1/sync/* und
// /api/v1/admin/*; Geräte-Token (Handy) als Cookie „fdk“ für die Handy-App, /api/v1/me, /api/v1/me/relink,
// /api/v1/me/autodl und /api/v1/admin/*. /api/v1/local/* (Befehlszeile, Helfer fragdesk-fetch): nur direkt über
// 127.0.0.1 (nicht über Caddy) mit „X-FragDesk-Local: <local.key>“.
// Schreibende Anfragen brauchen „X-FragDesk: 1“ (außer /api/v1/local/*: dort reicht der Schlüssel).
//
// Demos automatisch laden (siehe autodl.h):
//   GET/PUT/DELETE /api/v1/me/autodl, POST /api/v1/me/autodl/check                      Handy, jedes Konto
//   GET/DELETE /api/v1/admin/steam, POST …/steam/login, POST …/steam/code, PUT …/steam/apikey  Admin
//   GET /api/v1/local/fetch/job, POST …/fetch/login, …/fetch/token, …/fetch/run,
//   POST …/fetch/<konto>/state, PUT …/fetch/<konto>/match/<prüfsumme>/{json|heat|replay}  Helfer
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../src/api/http.h"
#include "../src/api/mobile.h"
#include "autodl.h"
#include "source.h"
#include "store.h"

namespace fdsrv {

// Fehlversuche je Schlüssel (meist die IP) in einem Zeitfenster
class RateLimit {
public:
    RateLimit(int maxFails, int windowSeconds) : max_(maxFails), window_(windowSeconds) {}
    bool blocked(const std::string& key);
    void fail(const std::string& key);
    void prune();

private:
    std::mutex mu_;
    std::map<std::string, std::vector<int64_t>> fails_;
    int max_, window_;
};

bool webAsset(const std::string& path, std::string& body);   // eingebaute Handy-App

class App {
public:
    App(Store& store, std::string version) : store_(store), views_(store), version_(std::move(version)) {}

    void handle(const fdapi::Request& req, fdapi::Response& res);
    int64_t bodyLimit(const fdapi::Request& head);   // für fdapi::Server::start
    void maintain();                                 // alle paar Minuten aus dem Hauptthread

    std::atomic<bool> stopping{false};
    std::atomic<int> busy{0};   // laufende Anfragen
    int64_t idleUnload = 15 * 60;   // Daten eines Kontos nach so vielen Sekunden ohne Handy freigeben
    std::string helperEnv;          // FRAGDESK_FETCH aus /etc/default/fragdesk (install.sh), siehe helperProblem()
    Views& views() { return views_; }

private:
    Store& store_;
    Views views_;
    std::string version_;
    RateLimit inviteLimit_{5, 15 * 60}, pairLimit_{5, 15 * 60}, localLimit_{5, 15 * 60};
    SteamLogin login_;   // Anmeldung des Steam-Bots, nur im Speicher

    void health(const fdapi::Request& req, fdapi::Response& res);
    void registerAccount(const fdapi::Request& req, fdapi::Response& res);
    void account(const fdapi::Request& req, fdapi::Response& res);
    void sync(const fdapi::Request& req, fdapi::Response& res);
    void admin(const fdapi::Request& req, fdapi::Response& res, const UserPtr& who, const std::string& sub);
    void local(const fdapi::Request& req, fdapi::Response& res);
    void pair(const fdapi::Request& req, fdapi::Response& res);
    void me(const fdapi::Request& req, fdapi::Response& res);
    void phoneLogout(const fdapi::Request& req, fdapi::Response& res);
    void phone(const fdapi::Request& req, fdapi::Response& res);

    void createInvite(const fdapi::Request& req, fdapi::Response& res, const std::string& by);
    void sendInvite(fdapi::Response& res, const std::string& by, bool admin, int days, const std::string& forUser);
    void relink(const fdapi::Request& req, fdapi::Response& res);
    void listUsers(fdapi::Response& res);
    std::string serverJson();   // {"version","url"}
    fdapi::Host host(const UserPtr& u);
    bool secure(const fdapi::Request& req);
    bool localKeyOk(const fdapi::Request& req);   // 127.0.0.1, nicht über Caddy, richtiger local.key

    // Ein Teil eines Matches hochladen (json, heat, replay, noreplay) – für FragDesk (/sync) und den Helfer
    // (/local/fetch) dieselben Grenzen und Prüfungen. false: res enthält schon den Fehler
    bool putMatch(const fdapi::Request& req, fdapi::Response& res, const UserPtr& u, const std::string& checksum, const std::string& what,
                  bool* isNew);
    // Demos automatisch laden
    void meAutodl(const fdapi::Request& req, fdapi::Response& res);
    void writeAutodl(fdapi::Response& res, const UserPtr& u);
    void adminSteam(const fdapi::Request& req, fdapi::Response& res, const std::string& sub);
    void writeSteam(fdapi::Response& res);
    void localFetch(const fdapi::Request& req, fdapi::Response& res, const std::string& sub);
};

}  // namespace fdsrv
