// FragDesk – gemeinsame Helfer der Handy-Ansichten und /api/v1/info
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>

#include "views.h"

#include "../core/radar.h"

namespace fdapi {

namespace {

std::mutex timeMu;   // localtime ist nicht threadsicher

bool localParts(int64_t epoch, std::tm& out) {
    std::lock_guard<std::mutex> lk(timeMu);
    time_t t = time_t(epoch);
    std::tm* p = std::localtime(&t);
    if (!p) return false;
    out = *p;
    return true;
}

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {   // H. Hinnant
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

int64_t dayNumber(const std::tm& t) { return daysFromCivil(t.tm_year + 1900, unsigned(t.tm_mon + 1), unsigned(t.tm_mday)); }

}  // namespace

std::string hexColor(uint32_t rgb) {
    char b[8];
    snprintf(b, sizeof b, "#%06x", rgb & 0xFFFFFFu);
    return b;
}

const char* ratingTone(double r) {
    if (std::isnan(r)) return "muted";
    if (r >= 1.10) return "good";
    if (r < 0.90) return "bad";
    return "";
}

const char* resultTone(const std::string& result) {
    if (result == "win") return "win";
    if (result == "loss") return "loss";
    if (result == "draw" || result == "tie") return "draw";
    return "";
}

int64_t matchEpoch(const fd::Match& m) {
    int64_t t = 0;
    if (!fd::parseIsoUtc(m.date, t)) fd::parseIsoUtc(m.demoModified, t);
    return t;
}

std::string dateText(int64_t epoch) {
    std::tm t{};
    if (epoch <= 0 || !localParts(epoch, t)) return "–";
    char b[32];
    snprintf(b, sizeof b, "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return b;
}

std::string dateTimeText(int64_t epoch) {
    std::tm t{};
    if (epoch <= 0 || !localParts(epoch, t)) return "–";
    char b[40];
    snprintf(b, sizeof b, "%02d.%02d.%04d, %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    return b;
}

std::string relativeDay(int64_t epoch) {
    std::tm t{}, now{};
    if (epoch <= 0 || !localParts(epoch, t) || !localParts(nowEpoch(), now)) return "–";
    int64_t diff = dayNumber(now) - dayNumber(t);
    char b[40];
    if (diff == 0) snprintf(b, sizeof b, "Heute, %02d:%02d", t.tm_hour, t.tm_min);
    else if (diff == 1) snprintf(b, sizeof b, "Gestern, %02d:%02d", t.tm_hour, t.tm_min);
    else if (t.tm_year == now.tm_year) snprintf(b, sizeof b, "%02d.%02d., %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
    else snprintf(b, sizeof b, "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return b;
}

std::string matchTitle(const fd::Match& m, const std::string& me) {
    int a = m.scoreA, b = m.scoreB;
    if (const fd::PlayerInfo* p = me.empty() ? nullptr : m.player(me)) {
        a = m.scoreOf(p->team);
        b = m.scoreOf(p->team == "A" ? "B" : "A");
    }
    return fd::mapName(m.map) + " · " + std::to_string(a) + ":" + std::to_string(b);
}

const std::vector<std::string>& modeLabels() {
    static const std::vector<std::string> v = {"Alle", "Premier", "FACEIT", "Wettkampf", "Wingman", "Sonstige"};
    return v;
}

const std::vector<std::string>& periodLabels() {
    static const std::vector<std::string> v = {"Letzte 10", "Letzte 30", "Alle Matches"};
    return v;
}

const char* modeKey(int index) {
    static const char* modes[] = {"", "premier", "faceit", "competitive", "wingman", "other"};
    return index > 0 && index < 6 ? modes[index] : "";
}

fd::Filter filterFrom(const Request& req) {
    fd::Filter f;
    std::string mode = req.param("mode"), period = req.param("period"), map = req.param("map");
    int mi = mode.size() == 1 && mode[0] >= '0' && mode[0] <= '5' ? mode[0] - '0' : 0;
    int pi = period.size() == 1 && period[0] >= '0' && period[0] <= '2' ? period[0] - '0' : 2;
    f.mode = modeKey(mi);
    f.lastN = pi == 0 ? 10 : pi == 1 ? 30 : 0;
    bool okMap = map.size() < 64;
    for (char ch : map)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) okMap = false;
    if (okMap) f.map = map;
    return f;
}

const fd::Career& careerFor(Source& src, const std::string& me, const fd::Filter& f) {
    // Nur aus dem Thread der Daten aufgerufen; die Karriere zeigt auf Matches, gilt also nur für diese Generation
    static std::map<std::string, fd::Career> cache;
    static int cacheGen = -1;
    int gen = src.generation();
    if (gen != cacheGen) {
        cache.clear();
        cacheGen = gen;
    }
    std::string key = me + "|" + f.mode + "|" + f.map + "|" + std::to_string(f.lastN);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (cache.size() >= 8) cache.clear();
    return cache[key] = fd::buildCareer(src.matches(), me, f);
}

double statNumber(const char* id, const fd::Counters& c) {
    const fd::Stat* s = fd::findStat(id);
    return s ? s->expr.eval(c) : NAN;
}

std::string statText(const char* id, const fd::Counters& c) {
    const fd::Stat* s = fd::findStat(id);
    return s ? fd::formatStat(s->expr.eval(c), s->def->fmt) : "–";
}

std::string sideLine(const char* id, const fd::Career& car) {
    const fd::Stat* s = fd::findStat(id);
    if (s && fd::statSideless(*s))
        return std::to_string(car.wins) + " Siege · " + std::to_string(car.losses) + " Niederlagen" +
               (car.draws ? " · " + std::to_string(car.draws) + " Unentschieden" : "");
    return "T " + statText(id, car.t) + " · CT " + statText(id, car.ct);
}

void writeStat(JsonW& j, const fd::Stat& s, const fd::Counters& all, const fd::Counters* t, const fd::Counters* ct) {
    double v = s.expr.eval(all);
    bool sideless = fd::statSideless(s);
    j.obj().kv("id", s.def->id).kv("label", s.def->label).kv("value", fd::formatStat(v, s.def->fmt)).kn("v", v);
    if (!sideless && t) j.kv("t", fd::formatStat(s.expr.eval(*t), s.def->fmt));
    if (!sideless && ct) j.kv("ct", fd::formatStat(s.expr.eval(*ct), s.def->fmt));
    j.kv("hint", s.def->hint ? s.def->hint : "").ki("better", s.def->better).kb("sideless", sideless);
    if (std::string(s.def->id) == "rating") j.kv("tone", ratingTone(v));
    j.end();
}

// ---- /api/v1/info -----------------------------------------------------------------------------------------------
void viewInfo(ViewCtx& c, Response& res) {
    JsonW j;
    j.obj().ki("api", kApiVersion).kv("version", c.host.version).kv("kind", c.host.kind);
    j.key("me").obj().kv("id", c.me).kv("name", c.src.meName()).end();
    auto matches = c.src.matches();
    j.kb("loaded", c.src.loaded()).ki("matches", int64_t(matches.size())).ki("newDemos", c.src.newDemos());
    j.key("modes").arr();
    for (const auto& l : modeLabels()) j.str(l);
    j.end();
    j.key("periods").arr();
    for (const auto& l : periodLabels()) j.str(l);
    j.end();
    // Maps wie im Auswahlmenü von FragDesk: alle mit eigener Beteiligung, mit Anzahl
    std::map<std::string, int> count;
    for (const fd::Match* m : matches)
        if (m->player(c.me)) ++count[m->map];
    j.key("maps").arr();
    for (const auto& kv : count)
        j.obj().kv("id", kv.first).kv("name", fd::mapName(kv.first)).kv("color", hexColor(fd::mapColor(kv.first))).ki("count", kv.second).end();
    j.end();
    j.key("features").obj().kb("heatmaps", true).kb("replay", true).end();
    j.end();
    res.body = j.take();
}

void writeRadar(JsonW& j, const std::string& map, const std::string& version) {
    const fd::RadarMap* r = fd::radarMap(map);
    if (!r) {
        j.null();
        return;
    }
    std::string base = std::string("/media/radar/") + r->name, v = "?v=" + version;
    j.obj().kv("name", r->name).kv("image", base + ".png" + v);
    if (r->lower) j.kv("lowerImage", base + "_lower.png" + v).kn("lowerZ", r->lowerZ);
    j.kn("posX", r->posX).kn("posY", r->posY).kn("span", r->span).end();
}

}  // namespace fdapi
