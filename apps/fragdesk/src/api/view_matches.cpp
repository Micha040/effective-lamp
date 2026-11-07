// FragDesk – Handy-App: Liste aller Matches (wie page_matches.cpp, ohne die Demo-Verwaltung)
#include <cmath>

#include "views.h"

namespace fdapi {

void viewMatches(ViewCtx& c, Response& res) {
    auto matches = c.src.matches();
    int demos = c.src.newDemos();
    std::string sub = std::to_string(matches.size()) + (matches.size() == 1 ? " Match ausgewertet" : " Matches ausgewertet");
    if (demos > 0) sub += " · " + std::to_string(demos) + (demos == 1 ? " neue Demo am PC" : " neue Demos am PC");
    bool anyPremier = false;
    for (const fd::Match* m : matches)
        if (m->mode == "premier") anyPremier = true;

    JsonW j;
    j.obj().kv("title", "Matches").kv("sub", sub).kb("anyPremier", anyPremier);
    j.key("modes").arr();
    for (const auto& l : modeLabels()) j.str(l);
    j.end();
    j.key("modeKeys").arr();
    for (int i = 0; i < 6; ++i) j.str(modeKey(i));
    j.end();
    if (matches.empty()) {
        j.key("empty")
            .obj()
            .kv("title", "Noch keine Matches")
            .kv("text", "FragDesk schaut auf deinem PC im CS2-Ordner „replays“ und in deinen Downloads nach Demos. Lade eine Demo in CS2 unter "
                        "„Deine Matches“ herunter oder bei FACEIT mit „Watch Demo“ und werte sie in FragDesk aus – dann erscheint sie hier.")
            .end();
    } else j.key("empty").null();

    j.key("items").arr();
    for (const fd::Match* mp : matches) {
        const fd::Match& m = *mp;
        const fd::PlayerInfo* p = m.player(c.me);
        int64_t t = matchEpoch(m);
        j.obj()
            .kv("id", m.checksum)
            .kv("date", relativeDay(t))
            .ki("epoch", t)
            .kv("map", fd::mapName(m.map))
            .kv("mapColor", hexColor(fd::mapColor(m.map)))
            .kv("sub", fd::sourceName(m.source) + " · " + fd::fmtDuration(m.duration))
            .kv("mode", fd::modeName(m.mode))
            .kv("modeKey", m.mode);
        double hs = p && p->stat("kills") > 0 ? 100 * p->stat("hsKills") / p->stat("kills") : NAN;
        if (p) {
            int mine = m.scoreOf(p->team), theirs = m.scoreOf(p->team == "A" ? "B" : "A");
            j.kv("score", std::to_string(mine) + " : " + std::to_string(theirs))
                .kv("result", resultTone(p->result))
                .kv("k", fd::fmtInt(p->stat("kills")))
                .kv("d", fd::fmtInt(p->stat("deaths")))
                .kv("a", fd::fmtInt(p->stat("assists") + p->stat("flashAssists")))
                .kv("adr", fd::fmtDec(p->adr, 1))
                .kv("kast", fd::fmtPct(p->kast, 0))
                .kv("hs", fd::formatStat(hs, fd::Fmt::Pct0))
                .kv("rating", fd::fmtDec(p->rating, 2))
                .kv("ratingTone", ratingTone(p->rating));
        } else {
            j.kv("score", std::to_string(m.scoreA) + " : " + std::to_string(m.scoreB)).kv("result", "").kv("k", "–").kv("d", "–").kv("a", "–");
            j.kv("adr", "–").kv("kast", "–").kv("hs", "–").kv("rating", "–").kv("ratingTone", "muted");
        }
        int premier = 0;
        if (p && p->rankType == 11 && (p->rankNew > 0 || p->rankOld > 0)) {
            premier = p->rankNew > 0 ? p->rankNew : p->rankOld;
            j.key("premier").obj().kv("value", fd::fmtRating(premier)).kv("color", hexColor(fd::premierColor(premier)));
            if (p->rankOld > 0 && p->rankNew > 0) j.kv("delta", fd::fmtSigned(p->rankNew - p->rankOld, 0));
            j.end();
        } else j.key("premier").null();
        // Rohwerte zum Sortieren
        j.key("sort")
            .obj()
            .ki("date", t)
            .kv("map", fd::mapName(m.map))
            .kv("mode", fd::modeName(m.mode))
            .ki("score", p ? (p->result == "win" ? 1 : p->result == "loss" ? -1 : 0) : 0)
            .kn("k", p ? p->stat("kills") : NAN)
            .kn("d", p ? p->stat("deaths") : NAN)
            .kn("a", p ? p->stat("assists") + p->stat("flashAssists") : NAN)
            .kn("adr", p ? p->adr : NAN)
            .kn("kast", p ? p->kast : NAN)
            .kn("hs", hs)
            .kn("rating", p ? p->rating : NAN)
            .kn("premier", premier > 0 ? double(premier) : NAN)
            .end();
        j.end();
    }
    j.end();
    res.body = j.end().take();
}

}  // namespace fdapi
