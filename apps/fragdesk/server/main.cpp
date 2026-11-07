// fragdesk-server – eigener Server für FragDesk: speichert Stats und Clips eines oder mehrerer Spieler, die Handy-App
// liest von hier. Linux x86_64, statisch gelinkt, ohne Fremdbibliotheken; davor Caddy für HTTPS (siehe install.sh).
//
//   fragdesk-server serve  [--data /var/lib/fragdesk] [--listen 127.0.0.1:7420] [--url https://<domain>/] [--min-free 512]
//   fragdesk-server invite [--admin] [--days 7] [--data …]   Einladungslink ausgeben (spricht mit dem laufenden Server)
//   fragdesk-server users  [--json] [--data …]               Konten auflisten
//   fragdesk-server version
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "../src/api/http.h"
#include "../src/api/views.h"
#include "../src/core/format.h"
#include "../src/core/json.h"
#include "app.h"
#include "files.h"
#include "store.h"

#ifndef FD_SERVER_VERSION
#define FD_SERVER_VERSION "dev"
#endif

using namespace fdsrv;

namespace {

const char* kVersion = FD_SERVER_VERSION;

struct Options {
    std::string cmd, data = "/var/lib/fragdesk", listen, url;
    bool admin = false, json = false;
    int days = 7;
    int64_t minFreeMb = 512;
};

void usage(FILE* out) {
    fprintf(out,
            "fragdesk-server %s – eigener Server für FragDesk (Stats und Clips für die Handy-App)\n\n"
            "  fragdesk-server serve  [--data DIR] [--listen 127.0.0.1:7420] [--url https://DOMAIN/] [--min-free MB]\n"
            "  fragdesk-server invite [--admin] [--days 7] [--data DIR]   Einladungslink für FragDesk ausgeben\n"
            "  fragdesk-server users  [--json] [--data DIR]               Konten auflisten\n"
            "  fragdesk-server version\n\n"
            "Standard für --data: /var/lib/fragdesk. invite und users sprechen mit dem laufenden Server über 127.0.0.1\n"
            "und brauchen <data>/local.key (als root oder Benutzer fragdesk ausführen).\n",
            kVersion);
}

bool splitListen(const std::string& listen, std::string& addr, int& port) {
    size_t colon = listen.rfind(':');
    if (colon == std::string::npos) return false;
    addr = listen.substr(0, colon);
    std::string p = listen.substr(colon + 1);
    if (addr.empty() || p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos) return false;
    port = std::stoi(p);
    return port >= 0 && port <= 65535;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ---- serve ------------------------------------------------------------------------------------------------------
int serve(const Options& o) {
    umask(077);
    std::signal(SIGPIPE, SIG_IGN);
    // Signale nur im Hauptthread annehmen (sigtimedwait); alle Threads des Servers erben die Maske
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

    if (!makeDirs(o.data, 0700)) {
        std::cerr << "Fehler: Der Datenordner " << o.data << " lässt sich nicht anlegen: " << lastError() << "\n";
        return 1;
    }
    // Nur ein Server pro Datenordner
    int lockFd = open(joinPath(o.data, ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "Fehler: Ein anderer fragdesk-server benutzt " << o.data << " schon.\n";
        return 1;
    }
    Store store;
    store.minFree = o.minFreeMb << 20;
    std::string err;
    if (!store.open(o.data, err)) {
        std::cerr << "Fehler: " << err << "\n";
        return 1;
    }
    std::string listen = o.listen.empty() ? "127.0.0.1:7420" : o.listen, addr;
    int port = 0;
    if (!splitListen(listen, addr, port)) {
        std::cerr << "Fehler: --listen erwartet Adresse:Port, z. B. 127.0.0.1:7420\n";
        return 2;
    }
    if (!o.url.empty()) {
        if (o.url.compare(0, 7, "http://") != 0 && o.url.compare(0, 8, "https://") != 0) {
            std::cerr << "Fehler: --url muss mit https:// (oder http://) beginnen\n";
            return 2;
        }
        store.setUrl(o.url);
    } else if (store.settings().url == Settings().url) store.setUrl("http://" + listen + "/");
    App app(store, kVersion);
    fdapi::Server server;
    bool started = server.start(
        addr, port, [&](const fdapi::Request& req, fdapi::Response& res) { app.handle(req, res); }, err,
        [&](const fdapi::Request& head) { return app.bodyLimit(head); });
    if (!started) {
        std::cerr << "Fehler: " << err << "\n";
        return 1;
    }
    store.setListen(addr + ":" + std::to_string(server.port()));
    std::cerr << "fragdesk-server " << kVersion << " lauscht auf " << addr << ":" << server.port() << " – " << store.settings().url << ", "
              << store.userCount() << (store.userCount() == 1 ? " Konto" : " Konten") << ", Daten in " << o.data << std::endl;

    // Im Leerlauf schläft der Hauptthread; einmal pro Minute wird aufgeräumt
    for (;;) {
        timespec wait{60, 0};
        int sig = sigtimedwait(&sigs, nullptr, &wait);
        if (sig == SIGINT || sig == SIGTERM) break;
        if (sig < 0 && errno != EAGAIN && errno != EINTR) std::this_thread::sleep_for(std::chrono::seconds(1));
        app.maintain();
    }

    // Beenden: keine neuen Anfragen mehr, laufende (z. B. ein Stück eines Videos) zu Ende schreiben lassen
    std::cerr << "fragdesk-server wird beendet …" << std::endl;
    app.stopping = true;
    for (int i = 0; i < 100 && app.busy > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // letzte Antworten verschicken
    server.stop();
    for (int i = 0; i < 30 && app.busy > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    store.flush();
    std::cerr << "fragdesk-server beendet" << std::endl;
    fflush(nullptr);
    _exit(0);   // ohne Destruktoren: Verbindungs-Threads laufen ggf. noch ihre letzten Millisekunden
}

// ---- invite / users (über 127.0.0.1 mit local.key) ------------------------------------------------------------------
bool localRequest(const Options& o, const std::string& method, const std::string& path, const std::string& body, int& status, std::string& out) {
    std::string key;
    if (!readFile(joinPath(o.data, "local.key"), key, 4096)) {
        std::cerr << "Fehler: " << joinPath(o.data, "local.key") << " lässt sich nicht lesen (" << lastError()
                  << "). Als root oder Benutzer fragdesk ausführen, ggf. --data angeben.\n";
        return false;
    }
    key = trim(key);
    std::string listen = o.listen;
    if (listen.empty()) {   // steht in server.json, seit der Server läuft
        std::string text;
        json::Value v;
        if (readFile(joinPath(o.data, "server.json"), text) && json::parse(text, v)) listen = v["settings"].getString("listen");
    }
    if (listen.empty()) listen = "127.0.0.1:7420";
    std::string addr;
    int port = 0;
    if (!splitListen(listen, addr, port)) {
        std::cerr << "Fehler: ungültige Adresse " << listen << "\n";
        return false;
    }
    if (addr == "0.0.0.0") addr = "127.0.0.1";
    int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    timeval tv{20, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (s < 0 || inet_pton(AF_INET, addr.c_str(), &a.sin_addr) != 1 || connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        std::cerr << "Fehler: Der Server läuft nicht (" << addr << ":" << port << "). Starten: systemctl start fragdesk-server\n";
        if (s >= 0) close(s);
        return false;
    }
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nX-FragDesk: 1\r\nX-FragDesk-Local: " + key +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    for (size_t sent = 0; sent < req.size();) {
        ssize_t k = send(s, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (k <= 0) break;
        sent += size_t(k);
    }
    std::string resp;
    char buf[8192];
    ssize_t k;
    while ((k = recv(s, buf, sizeof buf, 0)) > 0) resp.append(buf, size_t(k));
    close(s);
    size_t sp = resp.find(' '), end = resp.find("\r\n\r\n");
    if (sp == std::string::npos || end == std::string::npos) {
        std::cerr << "Fehler: keine Antwort vom Server\n";
        return false;
    }
    status = std::atoi(resp.c_str() + sp + 1);
    out = resp.substr(end + 4);
    return true;
}

int cliInvite(const Options& o) {
    std::string body = std::string("{\"admin\":") + (o.admin ? "true" : "false") + ",\"days\":" + std::to_string(o.days) + "}", out;
    int status = 0;
    if (!localRequest(o, "POST", "/api/v1/local/invite", body, status, out)) return 1;
    json::Value v;
    json::parse(out, v);
    if (status != 200) {
        std::cerr << "Fehler: " << v.getString("error", "Status " + std::to_string(status)) << "\n";
        return 1;
    }
    int days = int(v.getNumber("days", o.days));
    std::cout << (v.getBool("admin") ? "Einladung für einen Admin" : "Einladung") << " – gilt " << days << (days == 1 ? " Tag" : " Tage")
              << " und nur einmal. In FragDesk: Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“.\n"
              << v.getString("link") << std::endl;
    return 0;
}

std::string pad(const std::string& s, size_t width) {   // nach Zeichen (UTF-8), nicht nach Bytes
    size_t chars = 0;
    for (unsigned char ch : s) chars += (ch & 0xC0) != 0x80 ? 1 : 0;
    return chars >= width ? s + " " : s + std::string(width - chars, ' ');
}

int cliUsers(const Options& o) {
    std::string out;
    int status = 0;
    if (!localRequest(o, "GET", "/api/v1/local/users", "", status, out)) return 1;
    json::Value v;
    json::parse(out, v);
    if (status != 200) {
        std::cerr << "Fehler: " << v.getString("error", "Status " + std::to_string(status)) << "\n";
        return 1;
    }
    if (o.json) {
        std::cout << out << std::endl;
        return 0;
    }
    std::cout << v.arr.size() << (v.arr.size() == 1 ? " Konto" : " Konten") << "\n";
    if (v.arr.empty()) return 0;
    std::cout << pad("Name", 22) << pad("ID", 10) << pad("Admin", 7) << pad("Speicher", 26) << pad("Matches", 9) << pad("Videos", 8) << "Zuletzt\n";
    for (const auto& u : v.arr) {
        int64_t used = int64_t(u.getNumber("used")), limit = int64_t(u.getNumber("limit"));
        std::string space = fd::fmtBytes(double(used)) + (limit > 0 ? " von " + fd::fmtBytes(double(limit)) : " (unbegrenzt)");
        for (size_t p; (p = space.find("\xC2\xA0")) != std::string::npos;) space.replace(p, 2, " ");
        int64_t seen = int64_t(u.getNumber("lastSeen"));
        std::cout << pad(u.getString("name"), 22) << pad(u.getString("id"), 10) << pad(u.getBool("admin") ? "ja" : "–", 7) << pad(space, 26)
                  << pad(std::to_string(int64_t(u.getNumber("matches"))), 9) << pad(std::to_string(int64_t(u.getNumber("clips"))), 8)
                  << fdapi::dateTimeText(seen) << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    o.cmd = argc > 1 ? argv[1] : "help";
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Fehler: " << a << " braucht einen Wert\n";
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--data") o.data = next();
        else if (a == "--listen") o.listen = next();
        else if (a == "--url") o.url = next();
        else if (a == "--admin") o.admin = true;
        else if (a == "--json") o.json = true;
        else if (a == "--days") o.days = std::atoi(next().c_str());
        else if (a == "--min-free") o.minFreeMb = std::atoll(next().c_str());
        else if (a == "--help" || a == "-h") {
            usage(stdout);
            return 0;
        } else {
            std::cerr << "Fehler: unbekannte Option " << a << "\n\n";
            usage(stderr);
            return 2;
        }
    }
    if (o.days < 1 || o.days > 30) {
        std::cerr << "Fehler: --days zwischen 1 und 30\n";
        return 2;
    }
    if (o.cmd == "serve") return serve(o);
    if (o.cmd == "invite") return cliInvite(o);
    if (o.cmd == "users") return cliUsers(o);
    if (o.cmd == "version" || o.cmd == "--version") {
        std::cout << "fragdesk-server " << kVersion << std::endl;
        return 0;
    }
    bool help = o.cmd == "help" || o.cmd == "--help" || o.cmd == "-h";
    if (!help) std::cerr << "Fehler: unbekannter Befehl " << o.cmd << "\n\n";
    usage(help ? stdout : stderr);
    return help ? 0 : 2;
}
