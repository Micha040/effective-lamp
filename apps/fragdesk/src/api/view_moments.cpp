// FragDesk – Handy-App: Momente und Videos (wie page_highlights.cpp, ohne Aufnehmen)
#include <algorithm>
#include <map>

#include "views.h"

namespace fdapi {

void viewMoments(ViewCtx& c, Response& res) {
    bool all = c.req.param("who") == "all";
    fd::Filter f = filterFrom(c.req);   // wie in FragDesk: Modus und Map, nicht der Zeitraum
    auto matches = c.src.matches();
    auto clips = c.src.clips();
    std::map<std::string, const ClipInfo*> clipOf;
    for (const auto& ci : clips) clipOf[ci.match + "/" + ci.highlight] = &ci;

    JsonW j;
    j.obj().kv("title", "Momente").kv("sub", "Erkannte Highlights und deine Videos");
    std::string blocked = c.host.mediaBlocked ? c.host.mediaBlocked() : std::string();
    j.kv("blocked", blocked);

    // Vorschläge
    struct Mom {
        const fd::Match* m;
        const fd::HighlightInfo* h;
    };
    std::vector<Mom> moms;
    for (const fd::Match* mp : matches) {
        if (!f.mode.empty() && mp->mode != f.mode) continue;
        if (!f.map.empty() && mp->map != f.map) continue;
        for (const auto& h : mp->highlights)
            if (all || h.player == c.me) moms.push_back({mp, &h});
    }
    std::stable_sort(moms.begin(), moms.end(), [](const Mom& a, const Mom& b) {
        if (a.h->score != b.h->score) return a.h->score > b.h->score;
        return a.m->date > b.m->date;
    });
    j.key("suggestions").obj();
    if (moms.empty())
        j.key("empty")
            .obj()
            .kv("title", "Noch keine Momente")
            .kv("text", "Momente erkennt FragDesk beim Auswerten: Multikills, gewonnene Clutches, Noscopes, Wallbangs, Kills durch Smoke und mehr.")
            .end();
    else j.key("empty").null();
    size_t limit = std::min<size_t>(moms.size(), 60);
    j.key("items").arr();
    for (size_t i = 0; i < limit; ++i) {
        const Mom& mo = moms[i];
        std::string title = fd::highlightTitle(*mo.h);
        const fd::PlayerInfo* p = mo.m->player(mo.h->player);
        if (all && p) title = p->name + ": " + title;
        j.obj()
            .kv("match", mo.m->checksum)
            .kv("id", mo.h->id)
            .kv("title", title)
            .kv("meta", matchTitle(*mo.m, c.me) + " · Runde " + std::to_string(mo.h->round) + " · " + fd::fmtDec(mo.h->seconds(mo.m->tickrate), 0) +
                            " s · " + relativeDay(matchEpoch(*mo.m)))
            .ki("stars", std::max(1, std::min(5, mo.h->score / 40 + 1)))
            .kv("mapColor", hexColor(fd::mapColor(mo.m->map)))
            .kb("clutch", mo.h->clutch > 0)
            .ki("round", mo.h->round)   // 2D-Ansicht an dieser Stelle
            .ki("tick", momentTick(*mo.h, mo.m->tickrate));
        auto it = clipOf.find(mo.m->checksum + "/" + mo.h->id);
        if (it != clipOf.end()) j.kv("clip", it->second->id);
        else j.key("clip").null();
        j.end();
    }
    j.end();
    j.kv("more", moms.size() > limit ? "Die " + std::to_string(limit) + " besten von " + std::to_string(moms.size()) + " Momenten" : "");
    j.end();

    // Videos
    std::map<std::string, const fd::Match*> byId;
    for (const fd::Match* m : matches) byId[m->checksum] = m;
    j.key("videos").obj();
    if (clips.empty())
        j.key("empty")
            .obj()
            .kv("title", "Noch keine Videos")
            .kv("text", "Momente nimmst du in FragDesk auf deinem PC auf: unter „Momente“ oder in einem Match auf „Aufnehmen“. Die Videos erscheinen "
                        "dann hier.")
            .end();
    else j.key("empty").null();
    j.key("items").arr();
    for (const auto& ci : clips) {
        auto it = byId.find(ci.match);
        j.obj()
            .kv("id", ci.id)
            .kv("title", ci.title)
            .kv("sub", relativeDay(ci.created))
            .kv("match", it != byId.end() ? ci.match : "")
            .kv("matchTitle", it != byId.end() ? matchTitle(*it->second, c.me) : fd::mapName(ci.map))
            .kv("mapColor", hexColor(fd::mapColor(it != byId.end() ? it->second->map : ci.map)))
            .kv("duration", fd::fmtClock(ci.seconds))
            .kv("size", fd::fmtBytes(double(ci.size)))
            .kn("seconds", ci.seconds)
            .ki("bytes", ci.size)
            .kv("highlight", ci.highlight)
            .end();
    }
    j.end().end();
    res.body = j.end().take();
}

}  // namespace fdapi
