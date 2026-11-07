// fragdesk-server – Daten für die Ansichten (siehe source.h)
#include "source.h"

#include <malloc.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <list>
#include <vector>

#include "../src/core/model.h"
#include "files.h"

namespace fdsrv {

namespace {

std::atomic<int> gNextGeneration{1};   // für alle Konten gemeinsam (siehe source.h)

using MatchMap = std::map<std::string, std::unique_ptr<fd::Match>>;

std::unique_ptr<fd::Match> readMatch(const std::string& path, const std::string& checksum, bool details) {
    std::string text, err;
    if (!readFile(path, text, kMaxMatchFile)) return nullptr;
    auto m = std::make_unique<fd::Match>();
    if (!fd::parseMatch(text, *m, err, details) || m->checksum != checksum) return nullptr;
    return m;
}

}  // namespace

class Views::UserSource : public fdapi::Source {
public:
    UserSource(Store& s, UserPtr u) : store(s), user(std::move(u)) {}

    Store& store;
    UserPtr user;
    std::mutex loadMu;
    std::atomic<int64_t> lastUse{0};
    // alles Folgende nur mit viewMu_
    bool isLoaded = false;
    MatchMap byChecksum;
    std::vector<const fd::Match*> sorted;
    int gen = 0;
    std::list<std::pair<std::string, std::unique_ptr<fd::Match>>> details;   // die zuletzt geöffneten, neueste vorn

    std::string me() override { return store.profileId(user); }
    std::string meName() override { return store.profileName(user); }
    bool loaded() override { return true; }   // with() lädt vor jeder Ansicht
    std::vector<const fd::Match*> matches() override { return sorted; }
    int generation() override { return gen; }
    std::vector<fdapi::ClipInfo> clips() override { return store.clipInfos(user); }
    std::string matchFile(const std::string& checksum, const std::string& ext) override {
        if (!byChecksum.count(checksum) || (ext != "heat" && ext != "replay")) return std::string();   // auch Schutz vor fremden Pfaden
        std::string path = store.matchPath(user->id, checksum, ("." + ext).c_str());
        return fileSize(path) > 0 ? path : std::string();
    }

    bool detail(const std::string& checksum, fd::Match& out) override {
        if (!byChecksum.count(checksum)) return false;   // auch Schutz vor fremden Pfaden
        for (auto it = details.begin(); it != details.end(); ++it)
            if (it->first == checksum) {
                details.splice(details.begin(), details, it);
                out = *details.front().second;
                return true;
            }
        auto m = readMatch(store.matchPath(user->id, checksum, ".json"), checksum, true);
        if (!m) return false;
        out = *m;
        details.emplace_front(checksum, std::move(m));
        while (details.size() > 2) details.pop_back();   // jeder Bereich eines Matches ist eine eigene Anfrage
        return true;
    }

    void install(MatchMap data) {   // mit viewMu_
        byChecksum = std::move(data);
        details.clear();
        resort();
        isLoaded = true;
    }

    void resort() {
        sorted.clear();
        for (const auto& kv : byChecksum) sorted.push_back(kv.second.get());
        std::stable_sort(sorted.begin(), sorted.end(), [](const fd::Match* a, const fd::Match* b) { return a->date > b->date; });   // wie FragDesk
        gen = gNextGeneration++;
    }

    void clear() {
        byChecksum.clear();
        sorted.clear();
        sorted.shrink_to_fit();
        details.clear();
        isLoaded = false;
        gen = gNextGeneration++;
    }
};

Views::~Views() {
    std::lock_guard<std::mutex> lk(mapMu_);
    sources_.clear();
}

std::shared_ptr<Views::UserSource> Views::get(const UserPtr& u, bool create) {
    std::lock_guard<std::mutex> lk(mapMu_);
    auto it = sources_.find(u->id);
    if (it != sources_.end()) return it->second;
    if (!create) return nullptr;
    auto s = std::make_shared<UserSource>(store_, u);
    sources_[u->id] = s;
    return s;
}

bool Views::with(const UserPtr& u, const std::function<void(fdapi::Source&)>& fn) {
    if (u->deleted) return false;
    auto src = get(u, true);
    for (int tries = 0; tries < 3; ++tries) {   // falls das Aufräumen genau dazwischenkommt
        src->lastUse = fdapi::nowEpoch();
        {
            std::lock_guard<std::mutex> ll(src->loadMu);
            bool need;
            {
                std::lock_guard<std::mutex> vl(viewMu_);
                need = !src->isLoaded;
            }
            if (need) {   // lesen ohne viewMu_: andere Konten warten nicht
                MatchMap data;
                std::string dir = joinPath(store_.userDir(u->id), "matches");
                for (const std::string& n : listDir(dir)) {
                    if (n.size() < 6 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
                    std::string c = n.substr(0, n.size() - 5);
                    if (!validChecksum(c)) continue;
                    auto m = readMatch(joinPath(dir, n), c, false);
                    if (m) data[c] = std::move(m);
                    else std::cerr << "Hinweis: " << u->id << "/matches/" << n << " lässt sich nicht lesen\n";
                }
                std::lock_guard<std::mutex> vl(viewMu_);
                src->install(std::move(data));
            }
        }
        std::lock_guard<std::mutex> vl(viewMu_);
        if (u->deleted) return false;
        if (!src->isLoaded) continue;
        fn(*src);
        return true;
    }
    return false;
}

void Views::matchChanged(const UserPtr& u, const std::string& checksum) {
    auto src = get(u, false);
    if (!src) return;
    std::lock_guard<std::mutex> ll(src->loadMu);
    {
        std::lock_guard<std::mutex> vl(viewMu_);
        if (!src->isLoaded) return;   // wird beim nächsten Laden ohnehin frisch gelesen
    }
    auto m = readMatch(store_.matchPath(u->id, checksum, ".json"), checksum, false);   // nullptr = gelöscht
    std::lock_guard<std::mutex> vl(viewMu_);
    if (!src->isLoaded) return;
    if (m) src->byChecksum[checksum] = std::move(m);
    else src->byChecksum.erase(checksum);
    src->details.remove_if([&](const std::pair<std::string, std::unique_ptr<fd::Match>>& d) { return d.first == checksum; });
    src->resort();
}

void Views::profileChanged(const UserPtr& u) {
    auto src = get(u, false);
    if (!src) return;
    std::lock_guard<std::mutex> vl(viewMu_);
    if (src->isLoaded) src->gen = gNextGeneration++;
}

void Views::drop(const std::string& userId) {
    std::shared_ptr<UserSource> src;
    {
        std::lock_guard<std::mutex> lk(mapMu_);
        auto it = sources_.find(userId);
        if (it == sources_.end()) return;
        src = it->second;
        sources_.erase(it);
    }
    std::lock_guard<std::mutex> ll(src->loadMu);
    std::lock_guard<std::mutex> vl(viewMu_);
    src->clear();
}

int Views::unloadIdle(int64_t maxIdleSeconds) {
    std::vector<std::shared_ptr<UserSource>> list;
    {
        std::lock_guard<std::mutex> lk(mapMu_);
        for (auto it = sources_.begin(); it != sources_.end();) {
            if (it->second->user->deleted) it = sources_.erase(it);
            else list.push_back((it++)->second);
        }
    }
    int64_t now = fdapi::nowEpoch();
    int freed = 0;
    for (auto& src : list) {
        if (now - src->lastUse < maxIdleSeconds) continue;
        std::lock_guard<std::mutex> ll(src->loadMu);
        std::lock_guard<std::mutex> vl(viewMu_);
        if (!src->isLoaded || now - src->lastUse < maxIdleSeconds) continue;
        src->clear();
        ++freed;
    }
    if (freed) malloc_trim(0);   // Speicher wirklich ans System zurückgeben
    return freed;
}

int Views::loadedCount() {
    std::vector<std::shared_ptr<UserSource>> list;
    {
        std::lock_guard<std::mutex> lk(mapMu_);
        for (auto& kv : sources_) list.push_back(kv.second);
    }
    std::lock_guard<std::mutex> vl(viewMu_);
    int n = 0;
    for (auto& s : list) n += s->isLoaded ? 1 : 0;
    return n;
}

}  // namespace fdsrv
