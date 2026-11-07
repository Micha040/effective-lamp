// FragDesk – Kennzahlen: Formeln über den Zählern, Gruppen für die Statistik-Seiten, Karriere-Auswertung
#pragma once
#include <map>
#include <string>
#include <vector>

#include "model.h"

namespace fd {

enum class Fmt { Int, Dec1, Dec2, Pct0, Pct1, Sec0, Sec1, Meter, Money, Signed, Signed1, Deg1, Ms };

// Kleine Formel über Zählern, z. B. "100 * hsKills / kills". Division durch 0 ergibt NaN (Anzeige "–").
class Expr {
public:
    bool compile(const std::string& text, std::string* error = nullptr);
    double eval(const Counters& c) const;

private:
    enum Op : uint8_t { Num, Var, Add, Sub, Mul, Div, Neg, Max2, Min2 };
    struct Tok {
        Op op;
        uint16_t var;
        double num;
    };
    std::vector<Tok> rpn_;
};

struct StatDef {
    const char* id;
    const char* label;
    const char* expr;
    Fmt fmt;
    int better;         // 1: mehr ist besser, -1: weniger ist besser, 0: neutral
    const char* hint;   // Erklärung für den Tooltip
};

struct Stat {
    const StatDef* def;
    Expr expr;
};

const std::vector<Stat>& allStats();
const Stat* findStat(const std::string& id);
// true, wenn die Kennzahl nur fürs ganze Match gilt (Matches, Siege, MVPs …) – getrennt nach T/CT ergibt sie keinen Sinn
bool statSideless(const Stat& s);
double statValue(const std::string& id, const Counters& c);   // NaN, wenn nicht berechenbar

struct StatGroup {
    const char* id;
    const char* title;
    std::vector<const char*> stats;
};
const std::vector<StatGroup>& statGroups();
const StatGroup* findGroup(const std::string& id);

// ---- Karriere -------------------------------------------------------------------------------------------------
struct Filter {
    std::string mode;   // leer = alle; premier, competitive, wingman, faceit, other
    std::string map;    // leer = alle
    int lastN = 0;      // 0 = alle Matches, sonst die letzten N (nach Datum)
};

struct MatchPoint {   // ein Match in Zeitreihen und Listen
    const Match* match = nullptr;
    const PlayerInfo* me = nullptr;
    double rating2 = 0, kd = 0, adr = 0, kast = 0, hsPct = 0;
    double crosshair = 0, reaction = 0;   // Grad / ms, NaN ohne Messung (ältere Auswertung)
    int premierOld = 0, premierNew = 0;   // 0 = unbekannt
};

struct GroupAgg {   // Summe für eine Map, einen Modus, eine Waffe …
    int matches = 0, wins = 0, losses = 0, draws = 0;
    Counters all, t, ct;
};

struct Career {
    std::string steamId, name;
    int matches = 0, wins = 0, losses = 0, draws = 0;
    Counters all, t, ct;
    std::map<std::string, GroupAgg> maps, modes;
    std::map<std::string, std::pair<std::string, Counters>> weapons;   // Name -> (Art, Zähler)
    std::map<std::string, int> deathsBy;
    std::vector<MatchPoint> points;   // chronologisch, älteste zuerst
    int premierCurrent = 0, premierBest = 0;
};

// matches: alle ausgewerteten Matches; nur die mit steamId werden berücksichtigt.
Career buildCareer(const std::vector<const Match*>& matches, const std::string& steamId, const Filter& filter);

// Zähler eines Spielers in einem Match plus Ergebniszähler (matches, matchWins, matchLosses, matchDraws).
Counters matchCounters(const PlayerInfo& p, const Counters& base);

}  // namespace fd
