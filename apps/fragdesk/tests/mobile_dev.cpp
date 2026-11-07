// FragDesk – Testserver für die Handy-App unter Linux: liest ausgewertete Matches (matches/*.json), clips.json und
// die App-Dateien von der Platte und beantwortet dieselben Anfragen wie FragDesk.
//
//   mobile_dev --data <Ordner> --web <Ordner> [--port 7420] [--me <SteamID>] [--code 123456] [--blocked]
//
// --code: fester Kopplungscode (sonst zufällig, wird ausgegeben). --blocked: so tun, als liefe CS2 (keine Videos).
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "../src/api/mobile.h"
#include "../src/core/json.h"

using namespace fdapi;

namespace {

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

class DirSource : public Source {
public:
    std::string dir, meId, name = "Testspieler";
    std::vector<std::unique_ptr<fd::Match>> list;
    std::vector<ClipInfo> clipList;
    int gen = 1;

    void load() {
        list.clear();
        DIR* d = opendir((dir + "/matches").c_str());
        if (d) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() < 6 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
                std::string text, err;
                if (!readAll(dir + "/matches/" + n, text)) continue;
                auto m = std::make_unique<fd::Match>();
                if (fd::parseMatch(text, *m, err, false)) list.push_back(std::move(m));
                else std::cerr << "übersprungen: " << n << ": " << err << "\n";
            }
            closedir(d);
        }
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a->date > b->date; });
        clipList.clear();
        std::string text;
        json::Value v;
        if (readAll(dir + "/clips.json", text) && json::parse(text, v) && v.isArray()) {
            for (const auto& c : v.arr) {
                ClipInfo ci;
                ci.path = c.getString("path");
                ci.id = clipId(ci.path);
                ci.title = c.getString("title");
                ci.match = c.getString("match");
                ci.highlight = c.getString("highlight");
                ci.map = c.getString("map");
                ci.created = int64_t(c.getNumber("created"));
                ci.seconds = c.getNumber("seconds");
                std::ifstream f(ci.path, std::ios::binary | std::ios::ate);
                if (!f) continue;
                ci.size = int64_t(f.tellg());
                clipList.push_back(ci);
            }
        }
        std::sort(clipList.begin(), clipList.end(), [](const ClipInfo& a, const ClipInfo& b) { return a.created > b.created; });
        ++gen;
    }

    std::string me() override { return meId; }
    std::string meName() override { return name; }
    bool loaded() override { return true; }
    std::vector<const fd::Match*> matches() override {
        std::vector<const fd::Match*> v;
        for (const auto& m : list) v.push_back(m.get());
        return v;
    }
    int generation() override { return gen; }
    bool detail(const std::string& checksum, fd::Match& out) override {
        for (const auto& m : list)
            if (m->checksum == checksum) {
                // Dateiname = Prüfsumme (wie in FragDesk) oder beliebig: suchen
                std::string text, err;
                DIR* d = opendir((dir + "/matches").c_str());
                bool ok = false;
                while (d && !ok) {
                    dirent* e = readdir(d);
                    if (!e) break;
                    std::string n = e->d_name;
                    if (n.size() < 6 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
                    if (!readAll(dir + "/matches/" + n, text)) continue;
                    if (text.find(checksum) == std::string::npos) continue;
                    fd::Match tmp;
                    if (fd::parseMatch(text, tmp, err, true) && tmp.checksum == checksum) {
                        out = std::move(tmp);
                        ok = true;
                    }
                }
                if (d) closedir(d);
                return ok;
            }
        return false;
    }
    std::vector<ClipInfo> clips() override { return clipList; }
    int newDemos() override { return 2; }
    std::string matchFile(const std::string& checksum, const std::string& ext) override {
        if ((ext != "heat" && ext != "replay") || checksum.empty() || checksum.find_first_of("/\\.") != std::string::npos) return std::string();
        std::string p = dir + "/matches/" + checksum + "." + ext;
        std::ifstream f(p, std::ios::binary);
        return f ? p : std::string();
    }
};

std::atomic<bool> stopFlag{false};
void onSignal(int) { stopFlag = true; }

}  // namespace

int main(int argc, char** argv) {
    std::string data = ".", web = "web", radars = "res/radars", me, code;
    int port = 7420;
    bool blocked = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") data = next();
        else if (a == "--web") web = next();
        else if (a == "--radars") radars = next();
        else if (a == "--port") port = std::stoi(next());
        else if (a == "--me") me = next();
        else if (a == "--code") code = next();
        else if (a == "--blocked") blocked = true;
    }
    DirSource src;
    src.dir = data;
    if (me.empty()) {   // wie FragDesk: aus settings.json
        std::string text;
        json::Value v;
        if (readAll(data + "/settings.json", text) && json::parse(text, v)) me = v.getString("steamId");
    }
    src.meId = me;
    src.load();
    std::mutex srcMu;

    Auth auth;
    Host host;
    host.version = "dev";
    host.auth = &auth;
    host.withSource = [&](const std::function<void(Source&)>& fn) {
        std::lock_guard<std::mutex> lk(srcMu);
        fn(src);
        return true;
    };
    host.asset = [&](const std::string& path, std::string& body) { return readAll(web + "/" + path, body); };
    host.openFile = [](const std::string& path, int64_t& size) -> FileReader {
        auto f = std::make_shared<std::ifstream>(path, std::ios::binary | std::ios::ate);
        if (!*f) return nullptr;
        size = int64_t(f->tellg());
        auto mu = std::make_shared<std::mutex>();
        return [f, mu](int64_t off, char* buf, int64_t len) -> int64_t {
            std::lock_guard<std::mutex> lk(*mu);
            f->clear();
            f->seekg(off);
            f->read(buf, len);
            return int64_t(f->gcount());
        };
    };
    host.thumbnail = [](const ClipInfo& ci, std::string& jpeg) {
        std::string p = ci.path;
        size_t dot = p.rfind('.');
        return dot != std::string::npos && readAll(p.substr(0, dot) + ".jpg", jpeg);
    };
    host.radar = [&](const std::string& name, bool lower, std::string& png) {
        return readAll(radars + "/" + name + (lower ? "_lower.png" : ".png"), png);
    };
    host.mediaBlocked = [blocked]() { return blocked ? std::string("CS2 läuft gerade – Videos gibt es wieder, sobald das Spiel zu ist.") : std::string(); };

    auth.startPairing(600, code);
    std::string err;
    Server server;
    if (!server.start("0.0.0.0", port, [&](const Request& req, Response& res) { handle(host, req, res); }, err)) {
        std::cerr << "Start fehlgeschlagen: " << err << "\n";
        return 1;
    }
    std::cout << "Handy-Testserver auf Port " << server.port() << ", " << src.list.size() << " Matches, " << src.clipList.size()
              << " Videos, Code " << auth.pairingCode() << std::endl;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!stopFlag) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.stop();
    return 0;
}
