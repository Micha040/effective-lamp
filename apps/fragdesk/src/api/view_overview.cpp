// FragDesk – Handy-App: Übersicht (wie page_dashboard.cpp)
#include <algorithm>
#include <cmath>
#include <map>

#include "views.h"

namespace fdapi {

namespace {

void writePremier(JsonW& j, const fd::Career& car, const std::string& me) {
    std::vector<const fd::MatchPoint*> pts;
    for (const auto& p : car.points)
        if (p.premierNew > 0 || p.premierOld > 0) pts.push_back(&p);
    if (pts.empty()) {
        j.key("premier").null();
        j.key("premierEmpty")
            .obj()
            .kv("title", "Noch keine Premier-Matches ausgewertet")
            .kv("text", "Lade Premier-Demos in CS2 unter „Deine Matches“ herunter – FragDesk liest das Rating vor und nach jedem Match aus der Demo.")
            .end();
        return;
    }
    int cur = car.premierCurrent;
    j.key("premier").obj().kv("value", fd::fmtRating(cur)).kv("color", hexColor(fd::premierColor(cur)));
    const fd::MatchPoint* last = pts.back();
    if (last->premierOld > 0 && last->premierNew > 0) {
        int d = last->premierNew - last->premierOld;
        j.kv("delta", fd::fmtSigned(d, 0) + " zuletzt").kv("deltaTone", d >= 0 ? "good" : "bad");
    }
    j.kv("best", "Bestwert " + fd::fmtRating(car.premierBest));
    j.key("points").arr();
    for (const fd::MatchPoint* p : pts) {
        int v = p->premierNew > 0 ? p->premierNew : p->premierOld;
        int64_t t = matchEpoch(*p->match);
        std::string change;
        if (p->premierOld > 0 && p->premierNew > 0) change = " (" + fd::fmtSigned(p->premierNew - p->premierOld, 0) + ")";
        j.obj()
            .ki("y", v)
            .kv("label", dateText(t).substr(0, 6))
            .kv("color", hexColor(fd::premierColor(v)))
            .kv("tip", fd::fmtRating(v) + change + "\n" + matchTitle(*p->match, me) + " · " + fd::resultName(p->me->result) + "\n" + dateTimeText(t))
            .kv("match", p->match->checksum)
            .end();
    }
    j.end();
    j.key("bands").arr();
    for (int b : {0, 5000, 10000, 15000, 20000, 25000, 30000}) j.obj().ki("from", b).kv("color", hexColor(fd::premierColor(b + 1))).end();
    j.end();
    j.end();
}

void writeForm(JsonW& j, const fd::Career& car, const std::string& me) {
    size_t n = car.points.size(), from = n > 10 ? n - 10 : 0;
    int wins = 0, cnt = 0;
    double rat = 0, kills = 0, deaths = 0, dmg = 0, rounds = 0;
    j.key("form").obj().key("items").arr();
    for (size_t i = from; i < n; ++i) {
        const auto& p = car.points[i];
        bool win = p.me->result == "win", loss = p.me->result == "loss";
        j.obj()
            .kv("letter", win ? "S" : loss ? "N" : "U")
            .kv("result", win ? "win" : loss ? "loss" : "draw")
            .kv("mapColor", hexColor(fd::mapColor(p.match->map)))
            .kv("match", p.match->checksum)
            .kv("tip", matchTitle(*p.match, me) + "\n" + fd::resultName(p.me->result) + " · Rating " + fd::fmtDec(p.rating2, 2) + "\n" +
                           dateTimeText(matchEpoch(*p.match)))
            .end();
        wins += win;
        ++cnt;
        rat += p.rating2;
        kills += p.me->all.get("kills");
        deaths += p.me->all.get("deaths");
        dmg += p.me->all.get("dmg");
        rounds += p.me->all.get("rounds");
    }
    j.end();
    j.key("minis").arr();
    if (cnt > 0) {
        j.obj().kv("label", "Siege").kv("value", std::to_string(wins) + " / " + std::to_string(cnt)).kv("tone", "").end();
        j.obj().kv("label", "Ø Rating").kv("value", fd::fmtDec(rat / cnt, 2)).kv("tone", ratingTone(rat / cnt)).end();
        j.obj().kv("label", "ADR").kv("value", fd::fmtDec(rounds > 0 ? dmg / rounds : 0, 1)).kv("tone", "").end();
        j.obj().kv("label", "K/D").kv("value", fd::fmtDec(deaths > 0 ? kills / deaths : kills, 2)).kv("tone", "").end();
    }
    j.end().end();
}

}  // namespace

void viewOverview(ViewCtx& c, Response& res) {
    JsonW j;
    j.obj().kv("title", "Übersicht");
    auto matches = c.src.matches();
    std::string name = c.src.meName();
    std::string sub;
    if (!c.src.loaded()) sub = "Bibliothek wird geladen …";
    else if (matches.empty()) sub = "Willkommen bei FragDesk";
    else
        sub = (name.empty() ? std::string() : "Hallo " + name + " – ") + std::to_string(matches.size()) +
              (matches.size() == 1 ? " Match ausgewertet" : " Matches ausgewertet");
    j.kv("sub", sub);

    if (c.src.loaded() && matches.empty()) {
        j.key("empty")
            .obj()
            .kv("title", "Noch keine Matches")
            .kv("text", "Lade eine Demo herunter – in CS2 unter „Deine Matches“ oder bei FACEIT mit „Watch Demo“. FragDesk auf deinem PC findet sie "
                        "automatisch und wertet sie auf Knopfdruck aus.")
            .end();
        res.body = j.end().take();
        return;
    }
    fd::Filter f = filterFrom(c.req);
    const fd::Career& car = careerFor(c.src, c.me, f);
    if (car.matches == 0) {
        j.key("empty")
            .obj()
            .kv("title", "Keine passenden Matches")
            .kv("text", c.me.empty() ? "Wähle in FragDesk unter Einstellungen dein Konto aus."
                                     : "Für diese Auswahl gibt es noch keine ausgewerteten Matches. Ändere den Filter oder werte weitere Demos aus.")
            .end();
        res.body = j.end().take();
        return;
    }
    j.key("empty").null();
    writePremier(j, car, c.me);
    writeForm(j, car, c.me);

    // Kennzahlen
    struct K {
        const char* id;
        const char* label;
        const char* icon;
    } kpis[] = {
        {"rating", "Rating 2.0", "star"},     {"kd", "K/D", "crosshair"},
        {"adr", "ADR", "target"},             {"kast", "KAST", "shield"},
        {"hsPct", "Headshot-Quote", "crosshair"}, {"winrate", "Siegquote", "trophy"},
        {"openSuccess", "Eröffnungsduelle gewonnen", "flash"}, {"clutchRate", "Clutches gewonnen", "star"},
    };
    j.key("kpis").arr();
    for (const auto& k : kpis) {
        const fd::Stat* s = fd::findStat(k.id);
        if (!s) continue;
        double v = s->expr.eval(car.all);
        j.obj()
            .kv("id", k.id)
            .kv("label", k.label)
            .kv("icon", k.icon)
            .kv("value", fd::formatStat(v, s->def->fmt))
            .kv("sub", sideLine(k.id, car))
            .kv("hint", s->def->hint ? s->def->hint : "")
            .kv("tone", std::string(k.id) == "rating" ? ratingTone(v) : "")
            .end();
    }
    j.end();

    // Letzte Matches
    j.key("recent").arr();
    int shown = 0;
    for (size_t i = car.points.size(); i-- > 0 && shown < 6; ++shown) {
        const auto& p = car.points[i];
        int mine = p.match->scoreOf(p.me->team), theirs = p.match->scoreOf(p.me->team == "A" ? "B" : "A");
        j.obj()
            .kv("match", p.match->checksum)
            .kv("map", fd::mapName(p.match->map))
            .kv("mapColor", hexColor(fd::mapColor(p.match->map)))
            .kv("sub", relativeDay(matchEpoch(*p.match)) + " · " + fd::modeName(p.match->mode))
            .kv("score", std::to_string(mine) + ":" + std::to_string(theirs))
            .kv("result", resultTone(p.me->result))
            .kv("kd", fd::fmtInt(p.me->stat("kills")) + "–" + fd::fmtInt(p.me->stat("deaths")))
            .kv("rating", fd::fmtDec(p.rating2, 2))
            .kv("ratingTone", ratingTone(p.rating2))
            .end();
    }
    j.end();

    // Waffen
    std::vector<std::pair<double, std::string>> ws;
    for (const auto& kv : car.weapons) ws.push_back({kv.second.second.get("kills"), kv.first});
    std::sort(ws.rbegin(), ws.rend());
    double mx = ws.empty() || ws[0].first <= 0 ? 1 : ws[0].first;
    j.key("weapons").arr();
    for (size_t i = 0; i < ws.size() && i < 6 && ws[i].first > 0; ++i) {
        const auto& cnt = car.weapons.at(ws[i].second).second;
        j.obj()
            .kv("name", ws[i].second)
            .kv("value", fd::fmtInt(ws[i].first) + " Kills")
            .kv("sub", "HS " + fd::formatStat(fd::statValue("hsPct", cnt), fd::Fmt::Pct0) + " · Treffer " +
                           fd::formatStat(fd::statValue("accuracy", cnt), fd::Fmt::Pct0))
            .kn("frac", ws[i].first / mx)
            .end();
    }
    j.end();

    // Maps
    std::vector<std::pair<int, std::string>> maps;
    for (const auto& kv : car.maps) maps.push_back({kv.second.matches, kv.first});
    std::sort(maps.rbegin(), maps.rend());
    j.key("maps").arr();
    for (size_t i = 0; i < maps.size() && i < 7; ++i) {
        const auto& g = car.maps.at(maps[i].second);
        double rate = g.matches ? 100.0 * g.wins / g.matches : 0;
        j.obj()
            .kv("id", maps[i].second)
            .kv("name", fd::mapName(maps[i].second))
            .kv("color", hexColor(fd::mapColor(maps[i].second)))
            .kv("sub", std::to_string(g.matches) + (g.matches == 1 ? " Match" : " Matches") + " · Rating " +
                           fd::formatStat(fd::statValue("rating", g.all), fd::Fmt::Dec2))
            .kv("value", fd::fmtPct(rate, 0))
            .kn("frac", rate / 100)
            .kv("tip", std::to_string(g.wins) + " Siege, " + std::to_string(g.losses) + " Niederlagen" +
                           (g.draws ? ", " + std::to_string(g.draws) + " Unentschieden" : ""))
            .end();
    }
    j.end();

    // Beste Momente (mit Video, falls aufgenommen)
    std::map<std::string, std::string> clipOf;
    for (const auto& ci : c.src.clips()) clipOf[ci.match + "/" + ci.highlight] = ci.id;
    struct Mom {
        const fd::Match* m;
        const fd::HighlightInfo* h;
    };
    std::vector<Mom> moms;
    for (const auto& p : car.points)
        for (const auto& h : p.match->highlights)
            if (h.player == c.me) moms.push_back({p.match, &h});
    std::stable_sort(moms.begin(), moms.end(), [](const Mom& a, const Mom& b) { return a.h->score > b.h->score; });
    j.key("moments").arr();
    for (size_t i = 0; i < moms.size() && i < 5; ++i) {
        const Mom& mo = moms[i];
        j.obj()
            .kv("match", mo.m->checksum)
            .kv("id", mo.h->id)
            .kv("title", fd::highlightTitle(*mo.h))
            .kv("sub", fd::mapName(mo.m->map) + " · Runde " + std::to_string(mo.h->round) + " · " + relativeDay(matchEpoch(*mo.m)))
            .kb("clutch", mo.h->clutch > 0);
        auto it = clipOf.find(mo.m->checksum + "/" + mo.h->id);
        if (it != clipOf.end()) j.kv("clip", it->second);
        else j.key("clip").null();
        j.end();
    }
    j.end();
    res.body = j.end().take();
}

}  // namespace fdapi
