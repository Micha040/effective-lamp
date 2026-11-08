// FragDesk – Handy-App: Heatmaps wie in FragDesk (heat_view.cpp) – der Tab „Heatmap“ eines Matches und Statistiken ›
// Heatmaps (alle Matches einer Map mit den Filtern Modus und Zeitraum, wie heatTab in page_stats.cpp).
//
//   GET /api/v1/heat?match=<Prüfsumme>&<Auswahl>                 ein Match
//   GET /api/v1/heat?mode=&period=&map=&pick=<Map>&<Auswahl>     Statistiken: mode, period, map wie /api/v1/stats (map =
//                                                                Map-Filter); ohne Map-Filter zeigt pick die gewählte Map,
//                                                                sonst die meistgespielte mit Heatmap-Daten
//   Auswahl: layers = Bits der Ebenen wie fd::HeatLayer (1 Aufenthalt, 2 Kills, 4 Tode, 8 Smokes, 16 Mollys, 32 Flashes,
//            64 HEs), who = Bits 1 Ich, 2 Team, 4 Gegner (fehlt: Ich – spielte ich nicht mit, beide Teams), sides = 1 T,
//            2 CT, 3 beide, lower = 1 untere Ebene, marks = 1 einzelne Punkte, throws = 1 Wurfpositionen
//
// Antwort (alle Texte fertig auf Deutsch, Zahlen wie in FragDesk):
//   scope "match": id, title, head (wie /api/v1/match) · scope "stats": title, sub (wie /api/v1/stats), noMatches,
//   maps [{id, label}] (Maps mit Heatmap-Daten, nur ohne Map-Filter), map, notice (ältere Auswertungen)
//   beide: mapName, radar (writeRadar), empty {title, text, icon} statt der Karte – sonst hasMe, filter (die
//   tatsächlich verwendete Auswahl), ui (Beschriftungen), grid, legend, marks, marksNote, matches, foot.
//   grid: {n, layers: [{id, data}]} – Dichte der gezeigten Ebene je Schicht (nur Schichten mit Punkten auf dieser
//         Ebene): n × n Bytes, Zeilen von oben, 0..255 (unter 0,03 wie in FragDesk 0); Nullen als Paar „0, Anzahl − 1“,
//         das Ganze als Base64.
//   legend: {gradient (genau eine Schicht an: klassischer Verlauf), items: [{id, label, value, color}]} – Zahlen über
//         beide Ebenen wie in FragDesk.
//   marks: [u, v, Schicht, Art, Match, Runde, Tick, Tipp, wu, wv] – u, v in 1/10000 des Radarbilds; Art: 1 Punkt auf
//         dieser Ebene (antippbar, mit Tipp), 2 Wurfstelle auf dieser Ebene (dann wu, wv), 4 Wurflinie; Match: Index in
//         matches [{id, label}]; Tick: Zeitpunkt des Ereignisses (die 2D-Ansicht startet 2 s davor, wie App::openReplayAt).
//
// Ressourcen: Im Thread der Daten (FragDesk: UI-Thread, Host::withSource) wird nur zusammengestellt, was billig ist –
// Matches, Runden, Pfade. Die .heat-Dateien liest und rechnet der Thread der Anfrage, immer nur eine Auswertung zur
// Zeit. Das Ergebnis bleibt für die nächsten Anfragen im Speicher: höchstens zwei, zusammen höchstens 64 MB, bis sich
// die Matches ändern oder 10 Minuten lang niemand eine Heatmap ansieht.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "../core/heat.h"
#include "../core/radar.h"
#include "views.h"

namespace fdapi {

namespace {

constexpr int kGrid = 128;                   // Raster fürs Handy (FragDesk: 256 – Weichzeichnung deshalb halb so viele Zellen)
constexpr int kPcGrid = 256;                 // für die Zahlen der Legende genau wie in FragDesk
constexpr int64_t kMaxHeatFile = 16 << 20;   // FragDesk schreibt 30–200 KB, der eigene Server nimmt höchstens 4 MB
constexpr size_t kMaxMatches = 200;          // Statistiken: höchstens die neuesten 200 Matches einer Map
constexpr size_t kMaxMarks = 1500;           // mehr Punkte auf einer Ebene: keine Einzelpunkte, nur die Dichte
constexpr size_t kCacheEntries = 2;
constexpr size_t kCacheBytes = 64u << 20;
constexpr int kIdleSeconds = 600;            // so lange keine Heatmap gefragt: Speicher freigeben
constexpr int kWaitSeconds = 12;             // auf eine laufende Auswertung warten (die App gibt nach 15 s auf)

struct LayerInfo {
    fd::HeatLayer layer;
    const char* label;
    uint32_t color;
    float sigma;   // Weichzeichnung in Zellen von kGrid
};
// wie kLayers in heat_view.cpp (dort 2,2 bzw. 3,0 Zellen bei 256)
const LayerInfo kLayers[] = {
    {fd::HlPresence, "Aufenthalt", 0x3BA3FF, 1.1f}, {fd::HlKills, "Kills", 0x34D399, 1.5f},    {fd::HlDeaths, "Tode", 0xF87171, 1.5f},
    {fd::HlSmoke, "Smokes", 0xD7DCE3, 1.5f},       {fd::HlMolotov, "Mollys", 0xFB923C, 1.5f}, {fd::HlFlash, "Flashes", 0xFDE047, 1.5f},
    {fd::HlHE, "HEs", 0xF472B6, 1.5f},
};

bool isNade(uint8_t layer) { return layer >= fd::HlSmoke && layer <= fd::HlHE; }

// ---- Ausgewertete Daten und Zwischenspeicher ------------------------------------------------------------------------
// Ein Bereich (ein Match oder die Matches einer Map) – nach dem Bau unveränderlich, darf also ohne Sperre gelesen werden
struct HeatData {
    fd::HeatSet set;                // die Tipps stehen nicht in set.marks, sondern in tips: ein Block statt Tausender kleiner
    std::string tips;
    std::vector<uint32_t> tipAt;    // Beginn je Punkt, am Ende das Ende des letzten
    size_t bytes = 0;               // ungefährer Speicherbedarf

    std::string tip(size_t i) const { return tips.substr(tipAt[i], tipAt[i + 1] - tipAt[i]); }
};
using DataPtr = std::shared_ptr<const HeatData>;

void trimHeap() {
#ifdef __GLIBC__
    malloc_trim(0);   // freigegebenen Speicher wirklich ans System zurückgeben (wie fragdesk-server beim Aufräumen)
#endif
}

class Cache {
public:
    DataPtr get(const std::string& key) {
        std::lock_guard<std::mutex> lk(mu_);
        used_ = Clock::now();
        for (auto it = list_.begin(); it != list_.end(); ++it)
            if (it->key == key) {
                list_.splice(list_.begin(), list_, it);
                return list_.front().data;
            }
        return nullptr;
    }

    // Stände desselben Spielers mit einer anderen Generation sind veraltet (die Matches haben sich geändert)
    void dropStale(const std::string& me, int gen) {
        std::list<Entry> old;   // erst nach dem Entsperren freigeben
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = list_.begin(); it != list_.end();) {
            auto next = std::next(it);
            if (it->me == me && it->gen != gen) old.splice(old.end(), list_, it);
            it = next;
        }
    }

    void put(const std::string& key, const std::string& me, int gen, DataPtr data) {
        std::list<Entry> old;
        std::lock_guard<std::mutex> lk(mu_);
        used_ = Clock::now();
        list_.push_front({key, me, gen, std::move(data)});
        size_t n = 0, total = 0;
        for (auto it = list_.begin(); it != list_.end();) {
            auto next = std::next(it);
            total += it->data ? it->data->bytes : 0;
            if (++n > 1 && (n > kCacheEntries || total > kCacheBytes)) old.splice(old.end(), list_, it);   // der neueste bleibt
            it = next;
        }
        if (!janitor_) {
            try {
                std::thread([this] { janitor(); }).detach();
                janitor_ = true;
            } catch (...) {
                // ohne eigenen Thread räumen nur Generation und Grenzen auf
            }
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::string key, me;
        int gen;
        DataPtr data;
    };

    // Aufräumen nach kIdleSeconds ohne Heatmap-Anfrage; der Thread endet, sobald nichts mehr gespeichert ist
    void janitor() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!list_.empty()) {
            Clock::time_point due = used_ + std::chrono::seconds(kIdleSeconds);
            if (Clock::now() < due) {
                cv_.wait_until(lk, due);
                continue;
            }
            std::list<Entry> old;
            old.swap(list_);
            lk.unlock();
            old.clear();
            trimHeap();
            lk.lock();
        }
        janitor_ = false;
    }

    std::mutex mu_;
    std::condition_variable cv_;   // wird nie geweckt – nur Warten ohne Sperre
    std::list<Entry> list_;        // zuletzt benutzt vorn
    Clock::time_point used_ = Clock::now();
    bool janitor_ = false;
};

// Absichtlich nie zerstört: Aufräum-Thread und Anfragen dürfen bis zum Ende des Programms darauf zugreifen
Cache& cache() {
    static Cache* c = new Cache();
    return *c;
}
std::timed_mutex& buildMutex() {   // nur eine Auswertung zur Zeit
    static std::timed_mutex* m = new std::timed_mutex();
    return *m;
}

// ---- Zusammenstellen (im Thread der Daten) --------------------------------------------------------------------------
struct HeatFile {
    std::string path, label;   // label: Match in den Statistiken, z. B. „Mirage · 13:11 · 07.11.2026“
    fd::HeatMatchInfo info;
};

struct Empty {
    std::string title, text;
    const char* icon = "map";
};

struct Plan {
    bool stats = false, server = false;
    int status = 200;
    std::string error;
    std::string me;
    int gen = 0;
    // Match
    std::string checksum, title, head;   // head: fertiges JSON
    bool outdated = false;               // mit einer älteren FragDesk-Version ausgewertet
    // Statistiken
    std::string sub;
    bool noMatches = false;
    Empty noMatch;
    std::vector<std::pair<std::string, std::string>> maps;   // id, Beschriftung
    std::string noticeTitle, noticeText;
    size_t total = 0;   // Matches der Map mit aktueller Auswertung (vor der Begrenzung auf kMaxMatches)
    // Karte und Dateien
    std::string mapId;
    const fd::RadarMap* radar = nullptr;
    Empty empty;   // title leer: es gibt etwas zu zeigen
    std::vector<HeatFile> files;
};

// Kopf wie writeHead in view_match.cpp – der Tab „Heatmap“ kommt ohne /api/v1/match aus
std::string headJson(const fd::Match& m, const std::string& me) {
    const fd::PlayerInfo* p = m.player(me);
    std::string meta = fd::modeName(m.mode) + " · " + dateTimeText(matchEpoch(m)) + " · " + fd::fmtDuration(m.duration) + " · " +
                       std::to_string(m.rounds.size()) + " Runden" + (m.overtimes ? " (Verlängerung)" : "");
    std::string score = std::to_string(m.scoreA) + " : " + std::to_string(m.scoreB);
    const char* tone = "";
    if (p) {
        score = std::to_string(m.scoreOf(p->team)) + " : " + std::to_string(m.scoreOf(p->team == "A" ? "B" : "A"));
        tone = p->result == "win" ? "win" : p->result == "loss" ? "loss" : "draw";
    }
    JsonW j;
    j.obj()
        .kv("map", fd::mapName(m.map))
        .kv("mapColor", hexColor(fd::mapColor(m.map)))
        .kv("meta", meta)
        .kv("score", score)
        .kv("result", tone)
        .kv("resultText", p ? fd::resultName(p->result) : std::string())
        .kv("share", m.shareCode.empty() ? std::string() : "Share-Code: " + m.shareCode)
        .end();
    return j.take();
}

Empty noRadar() {
    return {"Kein Radarbild für diese Karte", "Die Heatmap braucht das Radarbild der Karte – FragDesk hat es für alle aktuellen offiziellen Karten.", "map"};
}

// Keine (lesbaren) Heatmap-Daten – Texte wie tabHeatmap bzw. heatTab in FragDesk, „Neu auswerten“ geht nur am PC
Empty noData(const Plan& p) {
    if (p.stats)
        return {"Noch keine Heatmap-Daten für " + fd::mapName(p.mapId),
                "Die gibt es für Matches, die mit FragDesk 1.2 oder neuer ausgewertet wurden – ältere in FragDesk am PC neu auswerten.", "map"};
    if (p.outdated)
        return {"Die Heatmap kommt mit dem Neu-Auswerten",
                "Dieses Match wurde mit einer älteren FragDesk-Version ausgewertet. In FragDesk am PC neu auswerten – dafür liest FragDesk "
                "die Demo noch einmal (ein paar Sekunden).",
                "map"};
    if (p.server)
        return {"Keine Heatmap-Daten für dieses Match",
                "Entweder hat FragDesk sie noch nicht auf den Server geladen (das passiert beim nächsten Abgleich), oder sie sind beim "
                "Auswerten nicht entstanden – dann steht der Grund in FragDesk am PC im Protokoll (fragdesk.log).",
                "warning"};
    return {"Keine Heatmap-Daten für dieses Match",
            "Beim Auswerten sind sie nicht entstanden. Details stehen in FragDesk am PC im Protokoll (fragdesk.log im Datenordner).", "warning"};
}

void planMatch(Source& src, const std::string& checksum, Plan& p) {
    const fd::Match* m = nullptr;
    for (const fd::Match* x : src.matches())
        if (x->checksum == checksum) {
            m = x;
            break;
        }
    if (!m) {
        p.status = 404;
        p.error = "Das Match gibt es nicht (mehr).";
        return;
    }
    p.checksum = m->checksum;
    p.title = matchTitle(*m, p.me);
    p.head = headJson(*m, p.me);
    p.mapId = m->map;
    p.outdated = m->schema < fd::kMatchSchema;
    p.radar = fd::radarMap(m->map);
    if (!p.radar) {   // wie HeatView::loadMatch: erst das Radarbild, dann die Datei
        p.empty = noRadar();
        return;
    }
    std::string path = src.matchFile(m->checksum, "heat");
    if (!path.empty()) p.files.push_back({path, std::string(), fd::heatMatchInfo(*m, p.me, std::string())});
}

void planStats(Source& src, const Request& req, Plan& p) {
    p.stats = true;
    fd::Filter f = filterFrom(req);
    const fd::Career& car = careerFor(src, p.me, f);
    if (car.matches)
        p.sub = (car.name.empty() ? std::string() : car.name + " · ") + std::to_string(car.matches) + (car.matches == 1 ? " Match · " : " Matches · ") +
                fd::fmtInt(car.all.get("rounds")) + " Runden";
    else p.sub = src.loaded() ? "Noch keine Daten für diese Auswahl" : "Bibliothek wird geladen …";
    if (car.matches == 0) {   // wie /api/v1/stats
        p.noMatches = true;
        p.noMatch = {"Keine passenden Matches",
                     p.me.empty() ? "Wähle in FragDesk unter Einstellungen dein Konto aus." : "Ändere den Filter oder werte weitere Demos aus.", "stats"};
        return;
    }
    // Maps mit Radarbild, meistgespielte zuerst – zur Auswahl wie in FragDesk („Mirage · 12“), aber nur die mit
    // Heatmap-Daten: aktuelle Auswertung und die Datei ist da (je Map genügt ein Match, meist also ein Blick pro Map)
    std::vector<std::pair<std::string, int>> maps;
    for (const auto& kv : car.maps)
        if (fd::radarMap(kv.first)) maps.push_back({kv.first, kv.second.matches});
    if (maps.empty()) {
        p.empty = {"Keine Karte mit Radarbild", "Die Heatmap gibt es für die offiziellen Karten.", "map"};
        return;
    }
    std::stable_sort(maps.begin(), maps.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (!f.map.empty()) p.mapId = f.map;   // Map-Filter: nur diese, keine Auswahl (wie FragDesk)
    else {
        std::vector<std::string> withData;
        for (auto it = car.points.rbegin(); it != car.points.rend(); ++it) {   // neueste zuerst
            const fd::Match& m = *it->match;
            if (m.schema < fd::kMatchSchema || std::find(withData.begin(), withData.end(), m.map) != withData.end()) continue;
            if (!src.matchFile(m.checksum, "heat").empty()) withData.push_back(m.map);
        }
        p.mapId = maps.front().first;   // ohne Heatmap-Daten: die meistgespielte (für Hinweis und leeren Zustand)
        const std::string pick = req.param("pick");
        for (const auto& mc : maps)
            if (std::find(withData.begin(), withData.end(), mc.first) != withData.end()) {
                p.maps.push_back({mc.first, fd::mapName(mc.first) + " · " + std::to_string(mc.second)});
                if (p.maps.size() == 1 || mc.first == pick) p.mapId = mc.first;
            }
    }
    p.radar = fd::radarMap(p.mapId);
    if (!p.radar) {
        p.empty = noRadar();
        return;
    }
    // Matches der Map: ältere Auswertungen für den Hinweis, sonst die neuesten kMaxMatches (car.points: älteste zuerst)
    std::vector<const fd::Match*> ms;
    int old = 0;
    for (const auto& pt : car.points) {
        if (pt.match->map != p.mapId) continue;
        if (pt.match->schema < fd::kMatchSchema) ++old;
        else ms.push_back(pt.match);
    }
    if (old > 0) {   // wie reanalyzeNote – „Neu auswerten“ gibt es nur am PC
        p.noticeTitle = old == 1 ? std::string("Ein Match wurde mit einer älteren FragDesk-Version ausgewertet")
                                 : std::to_string(old) + " Matches wurden mit einer älteren FragDesk-Version ausgewertet";
        p.noticeText = "Ihnen fehlen die Heatmap-Daten – in FragDesk am PC neu auswerten.";
    }
    p.total = ms.size();
    if (ms.size() > kMaxMatches) ms.erase(ms.begin(), ms.end() - long(kMaxMatches));
    for (const fd::Match* m : ms) {
        std::string path = src.matchFile(m->checksum, "heat");
        if (path.empty()) continue;
        p.files.push_back({path, matchTitle(*m, p.me) + " · " + dateText(matchEpoch(*m)), fd::heatMatchInfo(*m, p.me, std::string())});
    }
}

// ---- Lesen und Auswerten (im Thread der Anfrage) --------------------------------------------------------------------
std::shared_ptr<HeatData> build(Host& host, const Plan& p, const std::vector<int64_t>& sizes) {
    auto d = std::make_shared<HeatData>();
    int64_t total = 0;
    for (int64_t s : sizes) total += std::max<int64_t>(0, s);
    d->set.points.reserve(size_t(total / 8));   // eine Abtastung belegt 8 Bytes in der Datei
    d->tipAt.push_back(0);
    std::string bytes, err;
    for (size_t i = 0; i < p.files.size(); ++i) {
        if (sizes[i] <= 0) continue;
        const HeatFile& f = p.files[i];
        fd::Heat h;
        if (!readAll(host, f.path, bytes, kMaxHeatFile) || !fd::parseHeat(bytes, h, err)) continue;   // wie FragDesk: übergehen
        fd::HeatSet one;
        fd::addMatchHeat(one, f.info, h, *p.radar, p.me);
        const int mi = int(d->set.matches.size());
        d->set.matches.push_back({f.info.checksum, f.label});
        d->set.hasMe = d->set.hasMe || one.hasMe;
        d->set.points.insert(d->set.points.end(), one.points.begin(), one.points.end());
        for (fd::HeatMark& mk : one.marks) {
            mk.match = mi;
            d->tips += mk.tip;
            d->tipAt.push_back(uint32_t(d->tips.size()));
            std::string().swap(mk.tip);
            d->set.marks.push_back(std::move(mk));
        }
    }
    bytes.clear();
    d->set.marks.shrink_to_fit();
    d->tips.shrink_to_fit();
    d->tipAt.shrink_to_fit();
    d->bytes = d->set.points.capacity() * sizeof(fd::HeatPoint) + d->set.marks.capacity() * sizeof(fd::HeatMark) + d->tips.capacity() +
               d->tipAt.capacity() * sizeof(uint32_t) + d->set.matches.size() * 128;
    return d;
}

// Daten aus dem Zwischenspeicher oder neu gebaut; nullptr mit busy, wenn eine andere Auswertung zu lange dauert
DataPtr load(Host& host, const Plan& p, bool& busy) {
    // Schlüssel: Bereich, Spieler, Stand der Bibliothek und die Dateien mit Größe – eine später hochgeladene oder neu
    // ausgewertete Heatmap ändert die Größe, auch wenn sich die Generation (auf dem Server) nicht ändert
    std::string key = std::string(p.stats ? "s|" : "m|") + p.me + "|" + std::to_string(p.gen) + "|" + p.mapId;
    std::vector<int64_t> sizes(p.files.size(), -1);
    for (size_t i = 0; i < p.files.size(); ++i) {
        int64_t size = 0;
        FileReader rd = host.openFile ? host.openFile(p.files[i].path, size) : nullptr;
        if (!rd || size <= 0 || size > kMaxHeatFile) continue;
        sizes[i] = size;
        key += "|" + p.files[i].info.checksum + ":" + std::to_string(size);
    }
    Cache& c = cache();
    c.dropStale(p.me, p.gen);
    if (DataPtr d = c.get(key)) return d;
    std::unique_lock<std::timed_mutex> lk(buildMutex(), std::defer_lock);
    if (!lk.try_lock_for(std::chrono::seconds(kWaitSeconds))) {
        busy = true;
        return nullptr;
    }
    if (DataPtr d = c.get(key)) return d;   // inzwischen von einer anderen Anfrage gebaut
    DataPtr d = build(host, p, sizes);
    c.put(key, p.me, p.gen, d);
    trimHeap();   // Zwischenspeicher des Bauens (und verdrängte Daten) gleich zurückgeben
    return d;
}

// ---- Antwort ------------------------------------------------------------------------------------------------------
struct Params {
    int layers = 1, who = 0, sides = fd::SideT | fd::SideCT;
    bool lower = false, marks = false, throws = false;
};

int intParam(const Request& req, const char* name, int def, int lo, int hi) {
    std::string s = req.param(name);
    if (s.empty() || s.size() > 3 || !std::all_of(s.begin(), s.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return def;
    int v = std::stoi(s);
    return v < lo || v > hi ? def : v;
}

Params paramsFrom(const Request& req, bool stats) {
    Params prm;
    prm.layers = intParam(req, "layers", 1 << fd::HlPresence, 0, (1 << fd::HlCount) - 1);
    prm.who = intParam(req, "who", 0, 1, fd::WhoMe | fd::WhoTeam | fd::WhoEnemy);
    prm.sides = intParam(req, "sides", fd::SideT | fd::SideCT, 1, fd::SideT | fd::SideCT);
    prm.lower = req.param("lower") == "1";
    prm.marks = intParam(req, "marks", stats ? 0 : 1, 0, 1) == 1;   // wie FragDesk: im Match an, in den Statistiken aus
    prm.throws = req.param("throws") == "1";
    return prm;
}

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    auto at = [&](size_t i) { return uint32_t(uint8_t(in[i])); };
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = at(i) << 16 | at(i + 1) << 8 | at(i + 2);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += t[v >> 6 & 63];
        out += t[v & 63];
    }
    if (i < in.size()) {
        uint32_t v = at(i) << 16 | (i + 1 < in.size() ? at(i + 1) << 8 : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < in.size() ? t[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

// Dichte als Bytes; Nullen (unter 0,03 – das zeichnet FragDesk nicht) als Lauf „0, Anzahl − 1“
std::string encodeGrid(const std::vector<float>& g) {
    std::string raw;
    raw.reserve(g.size() / 2);
    for (size_t i = 0; i < g.size();) {
        if (g[i] < 0.03f) {
            size_t run = 1;
            while (run < 256 && i + run < g.size() && g[i + run] < 0.03f) ++run;
            raw += '\0';
            raw += char(uint8_t(run - 1));
            i += run;
        } else {
            raw += char(uint8_t(std::clamp<long>(std::lround(g[i] * 255), 1, 255)));
            ++i;
        }
    }
    return base64(raw);
}

// Punkte einer Schicht über beide Ebenen – so wie FragDesk sie in der Legende zählt (Raster 256, nur was darauf liegt)
int countLayer(const fd::HeatSet& s, fd::HeatLayer layer, const fd::HeatFilter& f) {
    auto inside = [](float u, float v) {
        int x = int(u * float(kPcGrid)), y = int(v * float(kPcGrid));
        return x >= 0 && y >= 0 && x < kPcGrid && y < kPcGrid;
    };
    int n = 0;
    if (layer == fd::HlPresence) {
        for (const auto& p : s.points)
            if (f.match(p.who, p.side) && inside(p.u / 65535.0f, p.v / 65535.0f)) ++n;
    } else {
        for (const auto& m : s.marks)
            if (m.layer == layer && f.match(m.who, m.side) && inside(m.u, m.v)) ++n;
    }
    return n;
}

std::string legendValue(fd::HeatLayer layer, int n) {   // wie HeatView::panel
    if (layer != fd::HlPresence) return fd::fmtInt(n);
    return n < 120 ? std::to_string(n) + " s" : fd::fmtDuration(n);
}

void writeEmpty(JsonW& j, const char* key, const Empty& e) {
    if (e.title.empty()) j.key(key).null();
    else j.key(key).obj().kv("title", e.title).kv("text", e.text).kv("icon", e.icon).end();
}

int coord(float x) { return int(std::lround(x * 10000)); }

void writeData(JsonW& j, const Plan& p, const HeatData& d, const Params& prm) {
    const fd::HeatSet& set = d.set;
    // Auswahl: ohne „Ich“ (ich spielte nicht mit) bedeuten Team und Gegner Team A und Team B – dann beide (wie FragDesk)
    const int teams = fd::WhoTeam | fd::WhoEnemy;
    int who = prm.who ? prm.who : set.hasMe ? int(fd::WhoMe) : teams;
    if (!set.hasMe) who = (who & teams) ? (who & teams) : teams;
    fd::HeatFilter f;
    f.who = uint8_t(who);
    f.sides = uint8_t(prm.sides);
    const bool lower = prm.lower && p.radar->lower;
    j.kb("hasMe", set.hasMe);
    j.key("filter")
        .obj()
        .ki("layers", prm.layers)
        .ki("who", f.who)
        .ki("sides", f.sides)
        .kb("lower", lower)
        .kb("marks", prm.marks)
        .kb("throws", prm.throws)
        .end();

    // Beschriftungen wie im Bereich rechts neben der Karte in FragDesk
    j.key("ui").obj().kv("layersTitle", "Zeigen").key("layers").arr();
    for (const auto& li : kLayers) j.obj().ki("id", li.layer).kv("label", li.label).kv("color", hexColor(li.color)).end();
    j.end().kv("whoTitle", "Spieler").key("who").arr();
    if (set.hasMe) j.obj().ki("bit", fd::WhoMe).kv("label", "Ich").end();
    j.obj().ki("bit", fd::WhoTeam).kv("label", set.hasMe ? "Team" : "Team A").end();
    j.obj().ki("bit", fd::WhoEnemy).kv("label", set.hasMe ? "Gegner" : "Team B").end();
    j.end().kv("sidesTitle", "Seite").key("sides").arr();
    j.obj().ki("value", fd::SideT | fd::SideCT).kv("label", "Beide").end();
    j.obj().ki("value", fd::SideT).kv("label", "T").end();
    j.obj().ki("value", fd::SideCT).kv("label", "CT").end();
    j.end();
    if (p.radar->lower) j.key("levels").arr().str("Obere Ebene").str("Untere Ebene").end();
    j.key("marks").obj().kv("label", "Punkte").kv("sub", "einzeln, Tippen zeigt, was dort war, und führt in die 2D-Ansicht").end();
    j.key("throws").obj().kv("label", "Wurfpositionen").kv("sub", "Linie von der Wurfstelle zur Landung").end();
    j.kv("low", "wenig").kv("high", "viel").end();

    // Dichte der gezeigten Ebene je Schicht, Legende über beide Ebenen
    int active = 0;
    for (const auto& li : kLayers)
        if (prm.layers & (1 << li.layer)) ++active;
    std::vector<float> g;
    j.key("grid").obj().ki("n", kGrid).key("layers").arr();
    for (const auto& li : kLayers) {
        if (!(prm.layers & (1 << li.layer))) continue;
        if (fd::heatDensity(set, li.layer, f, lower, kGrid, li.sigma, g) > 0) j.obj().ki("id", li.layer).kv("data", encodeGrid(g)).end();
    }
    j.end().end();
    j.key("legend").obj().kb("gradient", active == 1).key("items").arr();
    for (const auto& li : kLayers)
        if (prm.layers & (1 << li.layer))
            j.obj()
                .ki("id", li.layer)
                .kv("label", li.label)
                .kv("value", legendValue(li.layer, countLayer(set, li.layer, f)))
                .kv("color", hexColor(li.color))
                .end();
    j.end().end();

    // Punkte und Wurflinien der gezeigten Ebene (wie HeatView::draw: Punkte, und mit Wurfpositionen die Granaten mit Wurf)
    std::vector<std::pair<size_t, int>> list;   // Index, Art
    if (prm.marks || prm.throws) {
        for (size_t i = 0; i < set.marks.size(); ++i) {
            const fd::HeatMark& m = set.marks[i];
            if (!(prm.layers & (1 << m.layer)) || !f.match(m.who, m.side)) continue;
            int kind = 0;
            if (prm.marks && m.lower == lower) kind |= 1;
            if (prm.throws && isNade(m.layer) && m.thrown) {
                if (m.fromLower == lower) kind |= 2;
                if (m.fromLower == m.lower && m.lower == lower) kind |= 4;
            }
            if (kind) list.push_back({i, kind});
        }
    }
    std::string note;
    if (list.size() > kMaxMarks) {
        note = "Zu viele Punkte für die Einzelansicht (" + fd::fmtInt(double(list.size())) +
               ") – Auswahl eingrenzen, z. B. nur „Ich“, eine Seite oder eine Granatenart";
        list.clear();
    }
    j.key("marks").arr();
    for (const auto& [i, kind] : list) {
        const fd::HeatMark& m = set.marks[i];
        int tick = std::max(0, m.tick);   // Zeitpunkt des Ereignisses – die 2D-Ansicht beginnt selbst 2 s davor (replay.js)
        j.arr().integer(coord(m.u)).integer(coord(m.v)).integer(m.layer).integer(kind).integer(m.match).integer(m.round).integer(tick);
        j.str(kind & 1 ? d.tip(i) : std::string());
        if (kind & 2) j.integer(coord(m.fu)).integer(coord(m.fv));
        j.end();
    }
    j.end().kv("marksNote", note);
    j.key("matches").arr();
    if (!list.empty())
        for (const auto& mr : set.matches) j.obj().kv("id", mr.checksum).kv("label", mr.label).end();
    j.end();

    // Fußnote wie in FragDesk
    const char* what = "Aufenthalt einmal pro Sekunde · Kills vom Ort des Schützen · Granaten am Landepunkt";
    if (!p.stats)
        j.kv("foot",
             "Aufenthalt: wo die gewählten Spieler während der Runden waren (einmal pro Sekunde) · Kills vom Ort des Schützen · Granaten am Landepunkt");
    else {
        size_t n = set.matches.size();
        std::string count = p.total > kMaxMatches ? "Die neuesten " + std::to_string(kMaxMatches) + " von " + std::to_string(p.total) + " Matches"
                                                  : std::to_string(n) + (n == 1 ? " Match" : " Matches");
        j.kv("foot", count + " auf " + fd::mapName(p.mapId) + " · " + what);
    }
}

}  // namespace

void heatRoute(Host& host, const Request& req, Response& res) {
    Plan p;
    p.server = host.kind == "server";
    const std::string checksum = req.param("match");
    bool ok = host.withSource([&](Source& src) {   // nur Billiges: kein Lesen von Dateien
        p.me = src.me();
        p.gen = src.generation();
        if (!checksum.empty()) planMatch(src, checksum, p);
        else planStats(src, req, p);
    });
    if (!ok) return res.error(503, "FragDesk ist gerade nicht bereit.");
    if (p.status != 200) return res.error(p.status, p.error);

    DataPtr data;
    if (p.empty.title.empty() && !p.noMatches && p.radar && !p.files.empty()) {
        bool busy = false;
        data = load(host, p, busy);
        if (busy) {
            res.error(503, "Gerade wird eine andere Heatmap berechnet – gleich noch einmal versuchen.");
            res.set("Retry-After", "2");
            return;
        }
    }
    if (p.empty.title.empty() && !p.noMatches && (!data || data->set.matches.empty())) p.empty = noData(p);

    JsonW j;
    j.obj().kv("scope", p.stats ? "stats" : "match");
    if (p.stats) {
        j.kv("title", "Statistiken").kv("sub", p.sub);
        writeEmpty(j, "noMatches", p.noMatch);
        j.key("maps").arr();
        for (const auto& m : p.maps) j.obj().kv("id", m.first).kv("label", m.second).end();
        j.end().kv("map", p.mapId);
        if (p.noticeTitle.empty()) j.key("notice").null();
        else j.key("notice").obj().kv("title", p.noticeTitle).kv("text", p.noticeText).end();
    } else {
        j.kv("id", p.checksum).kv("title", p.title).key("head").raw(p.head);
    }
    j.kv("mapName", p.mapId.empty() ? std::string() : fd::mapName(p.mapId)).key("radar");
    if (p.radar) writeRadar(j, p.mapId, host.version);
    else j.null();
    writeEmpty(j, "empty", p.empty);
    if (p.empty.title.empty() && !p.noMatches && data) writeData(j, p, *data, paramsFrom(req, p.stats));
    res.body = j.end().take();
}

}  // namespace fdapi
