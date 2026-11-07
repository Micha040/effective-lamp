// FragDesk – Kennzahlen und Karriere (siehe stats.h)
#include "stats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>

namespace fd {

// ---- Formeln --------------------------------------------------------------------------------------------------

bool Expr::compile(const std::string& text, std::string* error) {
    rpn_.clear();
    size_t i = 0;
    std::string err;
    auto ws = [&] {
        while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    };
    auto fail = [&](const char* what) {
        if (err.empty()) err = std::string(what) + " bei Zeichen " + std::to_string(i);
        return false;
    };
    std::function<bool()> sum, product, unary, primary;
    primary = [&]() -> bool {
        ws();
        if (i >= text.size()) return fail("Wert erwartet");
        char c = text[i];
        if (c == '(') {
            ++i;
            if (!sum()) return false;
            ws();
            if (i >= text.size() || text[i] != ')') return fail("')' erwartet");
            ++i;
            return true;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            size_t start = i;
            while (i < text.size() && (std::isdigit((unsigned char)text[i]) || text[i] == '.')) ++i;
            rpn_.push_back({Num, 0, std::strtod(text.substr(start, i - start).c_str(), nullptr)});
            return true;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t start = i;
            while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '_')) ++i;
            std::string name = text.substr(start, i - start);
            ws();
            if (i < text.size() && text[i] == '(') {
                Op op;
                if (name == "max") op = Max2;
                else if (name == "min") op = Min2;
                else return fail("unbekannte Funktion");
                ++i;
                if (!sum()) return false;
                ws();
                if (i >= text.size() || text[i] != ',') return fail("',' erwartet");
                ++i;
                if (!sum()) return false;
                ws();
                if (i >= text.size() || text[i] != ')') return fail("')' erwartet");
                ++i;
                rpn_.push_back({op, 0, 0});
                return true;
            }
            rpn_.push_back({Var, Keys::id(name), 0});
            return true;
        }
        return fail("unerwartetes Zeichen");
    };
    unary = [&]() -> bool {
        ws();
        if (i < text.size() && text[i] == '-') {
            ++i;
            if (!unary()) return false;
            rpn_.push_back({Neg, 0, 0});
            return true;
        }
        return primary();
    };
    product = [&]() -> bool {
        if (!unary()) return false;
        for (;;) {
            ws();
            if (i < text.size() && (text[i] == '*' || text[i] == '/')) {
                Op op = text[i] == '*' ? Mul : Div;
                ++i;
                if (!unary()) return false;
                rpn_.push_back({op, 0, 0});
            } else return true;
        }
    };
    sum = [&]() -> bool {
        if (!product()) return false;
        for (;;) {
            ws();
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
                Op op = text[i] == '+' ? Add : Sub;
                ++i;
                if (!product()) return false;
                rpn_.push_back({op, 0, 0});
            } else return true;
        }
    };
    bool ok = sum();
    ws();
    if (ok && i != text.size()) ok = fail("Text nach dem Ende");
    if (!ok) {
        rpn_.clear();
        if (error) *error = err;
    }
    return ok;
}

double Expr::eval(const Counters& c) const {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double stack[32];
    int sp = 0;
    for (const Tok& t : rpn_) {
        switch (t.op) {
            case Num:
                if (sp >= 32) return nan;
                stack[sp++] = t.num;
                break;
            case Var:
                if (sp >= 32) return nan;
                stack[sp++] = c.get(t.var);
                break;
            case Neg:
                if (sp < 1) return nan;
                stack[sp - 1] = -stack[sp - 1];
                break;
            default: {
                if (sp < 2) return nan;
                double b = stack[--sp];
                double a = stack[sp - 1];
                double r = 0;
                switch (t.op) {
                    case Add: r = a + b; break;
                    case Sub: r = a - b; break;
                    case Mul: r = a * b; break;
                    case Div: r = b == 0 ? nan : a / b; break;
                    case Max2: r = std::isnan(a) || std::isnan(b) ? nan : std::max(a, b); break;
                    case Min2: r = std::isnan(a) || std::isnan(b) ? nan : std::min(a, b); break;
                    default: r = nan;
                }
                stack[sp - 1] = r;
            }
        }
    }
    return sp == 1 ? stack[0] : nan;
}

// ---- Kennzahlen -----------------------------------------------------------------------------------------------
namespace {

#define CLUTCHES "(clutch1+clutch2+clutch3+clutch4+clutch5)"
#define CLUTCHWINS "(clutchWon1+clutchWon2+clutchWon3+clutchWon4+clutchWon5)"
#define NADES "(flashThrown+smokeThrown+heThrown+fireThrown+decoyThrown)"
#define KPR "(kills/rounds)"
#define APR "((assists+flashAssists)/rounds)"
#define IMPACT "(2.13*" KPR "+0.42*" APR "-0.41)"
#define RATING2 "max(0, 0.0073*(100*kastRounds/rounds) + 0.3591*" KPR " - 0.5329*(deaths/rounds) + 0.2372*" IMPACT " + 0.0032*(dmg/rounds) + 0.1587)"
#define RATING1 "(" KPR "/0.679 + 0.7*((rounds-deaths)/rounds/0.317) + ((k1+4*k2+9*k3+16*k4+25*k5)/rounds/1.277))/2.7"

const StatDef kDefs[] = {
    // Allgemein
    {"matches", "Matches", "matches", Fmt::Int, 0, "Ausgewertete Matches"},
    {"winrate", "Siegquote", "100*matchWins/matches", Fmt::Pct0, 1, "Anteil gewonnener Matches"},
    {"wins", "Siege", "matchWins", Fmt::Int, 1, ""},
    {"losses", "Niederlagen", "matchLosses", Fmt::Int, -1, ""},
    {"draws", "Unentschieden", "matchDraws", Fmt::Int, 0, ""},
    {"rounds", "Runden", "rounds", Fmt::Int, 0, "Gespielte Runden"},
    {"roundWinrate", "Runden gewonnen", "100*roundsWon/rounds", Fmt::Pct0, 1, ""},
    {"rating", "Rating 2.0", RATING2, Fmt::Dec2, 1, "Näherung an das HLTV-Rating 2.0 – 1,00 ist Durchschnitt"},
    {"rating1", "Rating 1.0", RATING1, Fmt::Dec2, 1, "HLTV-Rating 1.0 aus Kills, Überleben und Multikills"},
    {"impact", "Impact", IMPACT, Fmt::Dec2, 1, "Einfluss auf die Runden durch Kills und Assists"},
    {"kd", "K/D", "kills/deaths", Fmt::Dec2, 1, "Kills pro Tod"},
    {"kdDiff", "K/D-Differenz", "kills-deaths", Fmt::Signed, 1, "Kills minus Tode"},
    {"kpr", "Kills pro Runde", "kills/rounds", Fmt::Dec2, 1, ""},
    {"dpr", "Tode pro Runde", "deaths/rounds", Fmt::Dec2, -1, ""},
    {"apr", "Assists pro Runde", APR, Fmt::Dec2, 1, "Schadens- und Flash-Assists pro Runde"},
    {"adr", "ADR", "dmg/rounds", Fmt::Dec1, 1, "Schaden an Gegnern pro Runde"},
    {"kast", "KAST", "100*kastRounds/rounds", Fmt::Pct1, 1, "Runden mit Kill, Assist, Überleben oder getradetem Tod"},
    {"hsPct", "Headshot-Quote", "100*hsKills/kills", Fmt::Pct0, 1, "Anteil der Kills per Kopfschuss"},
    {"kills", "Kills", "kills", Fmt::Int, 1, ""},
    {"deaths", "Tode", "deaths", Fmt::Int, -1, ""},
    {"assists", "Assists", "assists", Fmt::Int, 1, "Assists durch Schaden"},
    {"flashAssists", "Flash-Assists", "flashAssists", Fmt::Int, 1, "Kills, bei denen deine Flash den Gegner geblendet hat"},
    {"hsKills", "Headshot-Kills", "hsKills", Fmt::Int, 1, ""},
    {"survival", "Überlebt", "100*survived/rounds", Fmt::Pct0, 1, "Runden, die du überlebt hast"},
    {"roundsWithKill", "Runden mit Kill", "100*(k1+k2+k3+k4+k5)/rounds", Fmt::Pct0, 1, ""},
    {"mvps", "MVPs", "mvps", Fmt::Int, 1, ""},
    {"mvpPerMatch", "MVPs pro Match", "mvps/matches", Fmt::Dec1, 1, ""},
    {"scorePerMatch", "Punkte pro Match", "score/matches", Fmt::Dec1, 1, "Punkte im Scoreboard"},
    // Multikills
    {"k1", "1-Kill-Runden", "k1", Fmt::Int, 0, ""},
    {"k2", "2-Kill-Runden", "k2", Fmt::Int, 1, ""},
    {"k3", "3-Kill-Runden", "k3", Fmt::Int, 1, ""},
    {"k4", "4-Kill-Runden", "k4", Fmt::Int, 1, ""},
    {"k5", "Asse", "k5", Fmt::Int, 1, "Runden mit 5 Kills"},
    {"multiRounds", "Multikill-Runden", "k2+k3+k4+k5", Fmt::Int, 1, "Runden mit mindestens 2 Kills"},
    {"multiRate", "Multikill-Quote", "100*(k2+k3+k4+k5)/rounds", Fmt::Pct1, 1, ""},
    {"bigRoundsPerMatch", "3+ Kills pro Match", "(k3+k4+k5)/matches", Fmt::Dec2, 1, "Runden mit 3 oder mehr Kills pro Match"},
    // Eröffnung & Trades
    {"openDuels", "Eröffnungsduelle", "openKills+openDeaths", Fmt::Int, 0, "Am ersten Kill der Runde beteiligt"},
    {"openRate", "Eröffnungsduelle pro Runde", "100*(openKills+openDeaths)/rounds", Fmt::Pct0, 0, "Wie oft du am ersten Duell der Runde beteiligt bist"},
    {"openSuccess", "Eröffnungsduelle gewonnen", "100*openKills/(openKills+openDeaths)", Fmt::Pct0, 1, ""},
    {"openKills", "Eröffnungskills", "openKills", Fmt::Int, 1, "Erster Kill der Runde"},
    {"openDeaths", "Eröffnungstode", "openDeaths", Fmt::Int, -1, "Als Erster der Runde gestorben"},
    {"openKillsPerRound", "Eröffnungskills pro Runde", "openKills/rounds", Fmt::Dec2, 1, ""},
    {"openConv", "Runde nach Eröffnungskill gewonnen", "100*openKillsWon/openKills", Fmt::Pct0, 1, ""},
    {"openDeathWon", "Runde trotz Eröffnungstod gewonnen", "100*openDeathsWon/openDeaths", Fmt::Pct0, 1, ""},
    {"openTraded", "Eröffnungstode getradet", "100*openDeathsTraded/openDeaths", Fmt::Pct0, 1, "Ein Mitspieler hat dich innerhalb von 5 s gerächt"},
    {"tradeKills", "Trade-Kills", "tradeKills", Fmt::Int, 1, "Gegner getötet, der kurz vorher einen Mitspieler erwischt hat"},
    {"tradeKillsPerRound", "Trade-Kills pro Runde", "tradeKills/rounds", Fmt::Dec2, 1, ""},
    {"tradedDeaths", "Getradete Tode", "tradedDeaths", Fmt::Int, 1, "Tode, die ein Mitspieler innerhalb von 5 s gerächt hat"},
    {"tradedPct", "Tode getradet", "100*tradedDeaths/deaths", Fmt::Pct0, 1, ""},
    // Clutches
    {"clutches", "Clutch-Situationen", CLUTCHES, Fmt::Int, 0, "Als Letzter deines Teams gegen 1 bis 5 Gegner"},
    {"clutchWins", "Clutches gewonnen", CLUTCHWINS, Fmt::Int, 1, ""},
    {"clutchRate", "Clutch-Siegquote", "100*" CLUTCHWINS "/" CLUTCHES, Fmt::Pct0, 1, ""},
    {"clutch1Rate", "1 gegen 1", "100*clutchWon1/clutch1", Fmt::Pct0, 1, ""},
    {"clutch2Rate", "1 gegen 2", "100*clutchWon2/clutch2", Fmt::Pct0, 1, ""},
    {"clutch3Rate", "1 gegen 3", "100*clutchWon3/clutch3", Fmt::Pct0, 1, ""},
    {"clutch4Rate", "1 gegen 4", "100*clutchWon4/clutch4", Fmt::Pct0, 1, ""},
    {"clutch5Rate", "1 gegen 5", "100*clutchWon5/clutch5", Fmt::Pct0, 1, ""},
    {"clutchKills", "Kills in Clutches", "clutchKills", Fmt::Int, 1, ""},
    {"clutchSaved", "Gerettet", "clutchSaved", Fmt::Int, 0, "Clutch verloren, aber überlebt"},
    // Utility
    {"nadesPerRound", "Granaten pro Runde", NADES "/rounds", Fmt::Dec2, 1, ""},
    {"utilDmg", "Utility-Schaden", "utilDmg", Fmt::Int, 1, "Schaden durch Granaten und Feuer"},
    {"utilDmgPerRound", "Utility-Schaden pro Runde", "utilDmg/rounds", Fmt::Dec1, 1, ""},
    {"flashThrown", "Flashes geworfen", "flashThrown", Fmt::Int, 0, ""},
    {"enemiesFlashed", "Gegner geblendet", "enemiesFlashed", Fmt::Int, 1, ""},
    {"enemiesPerFlash", "Gegner pro Flash", "enemiesFlashed/flashThrown", Fmt::Dec2, 1, ""},
    {"blindTime", "Ø Blendzeit pro Gegner", "enemyFlashDur/enemiesFlashed", Fmt::Sec1, 1, ""},
    {"blindTimePerRound", "Blendzeit pro Runde", "enemyFlashDur/rounds", Fmt::Sec1, 1, "Sekunden, die du Gegner pro Runde blind machst"},
    {"flashEffective", "Wirksame Flashes", "100*flashesEffective/flashThrown", Fmt::Pct0, 1, "Flashes, die mindestens einen Gegner 1 s oder länger blenden"},
    {"flashLeadKills", "Kills nach deinen Flashes", "flashLeadKills", Fmt::Int, 1, "Gegner, die von dir geblendet waren, als sie getötet wurden"},
    {"teamFlashed", "Mitspieler geblendet", "teamFlashed", Fmt::Int, -1, ""},
    {"teamPerFlash", "Mitspieler pro Flash", "teamFlashed/flashThrown", Fmt::Dec2, -1, ""},
    {"selfFlashed", "Selbst geblendet", "selfFlashed", Fmt::Int, -1, "Von deiner eigenen Flash"},
    {"gotFlashed", "Geblendet worden pro Runde", "gotFlashed/rounds", Fmt::Dec2, -1, "Wie oft dich Gegner pro Runde blenden"},
    {"gotFlashedDur", "Ø Blendzeit gegen dich", "gotFlashedDur/gotFlashed", Fmt::Sec1, -1, ""},
    {"heThrown", "HE geworfen", "heThrown", Fmt::Int, 0, ""},
    {"heDmg", "HE-Schaden", "heDmg", Fmt::Int, 1, ""},
    {"hePerNade", "Schaden pro HE", "heDmg/heThrown", Fmt::Dec1, 1, ""},
    {"heHitsPerNade", "Getroffene pro HE", "heHits/heThrown", Fmt::Dec2, 1, ""},
    {"fireThrown", "Molotovs geworfen", "fireThrown", Fmt::Int, 0, "Molotovs und Brandgranaten"},
    {"fireDmg", "Feuerschaden", "fireDmg", Fmt::Int, 1, ""},
    {"firePerNade", "Schaden pro Molotov", "fireDmg/fireThrown", Fmt::Dec1, 1, ""},
    {"smokeThrown", "Smokes geworfen", "smokeThrown", Fmt::Int, 0, ""},
    {"decoyThrown", "Decoys geworfen", "decoyThrown", Fmt::Int, 0, ""},
    {"nadeKills", "Granaten-Kills", "heKills+fireKills", Fmt::Int, 1, "Kills mit HE oder Feuer"},
    {"teamUtilDmg", "Utility-Schaden an Mitspielern", "teamUtilDmg", Fmt::Int, -1, ""},
    // Zielen
    {"shots", "Schüsse", "shots", Fmt::Int, 0, "Abgefeuerte Kugeln (ohne Granaten und Messer)"},
    {"accuracy", "Treffsicherheit", "100*shotsHit/shots", Fmt::Pct1, 1, "Anteil der Schüsse, die einen Spieler getroffen haben"},
    {"firstShotAcc", "Erster Schuss", "100*firstShotsHit/firstShots", Fmt::Pct1, 1, "Treffsicherheit des ersten Schusses einer Salve"},
    {"sprayAcc", "Spray ab der 4. Kugel", "100*sprayShotsHit/sprayShots", Fmt::Pct1, 1, ""},
    {"movingPct", "Schüsse in Bewegung", "100*movingShots/shots", Fmt::Pct0, -1, "Schneller als 34 % des Waffentempos geschossen – dann streut die Waffe"},
    {"movingAcc", "Treffer in Bewegung", "100*movingShotsHit/movingShots", Fmt::Pct1, 1, ""},
    {"standingAcc", "Treffer im Stand", "100*standingShotsHit/standingShots", Fmt::Pct1, 1, ""},
    {"headHitPct", "Kopftreffer", "100*hitHead/hits", Fmt::Pct1, 1, "Anteil der Kopftreffer an allen Treffern"},
    {"hitNeckPct", "Hals", "100*hitNeck/hits", Fmt::Pct1, 0, ""},
    {"hitChestPct", "Brust", "100*hitChest/hits", Fmt::Pct1, 0, ""},
    {"hitStomachPct", "Bauch", "100*hitStomach/hits", Fmt::Pct1, 0, ""},
    {"hitArmsPct", "Arme", "100*hitArms/hits", Fmt::Pct1, 0, ""},
    {"hitLegsPct", "Beine", "100*hitLegs/hits", Fmt::Pct1, 0, ""},
    {"shotsPerKill", "Schüsse pro Kill", "shots/kills", Fmt::Dec1, -1, ""},
    {"dmgPerHit", "Schaden pro Treffer", "(dmg-utilDmg)/hits", Fmt::Dec1, 1, ""},
    {"avgDist", "Ø Kill-Distanz", "distSum/kills", Fmt::Meter, 0, ""},
    {"maxDist", "Weitester Kill", "distMax", Fmt::Meter, 0, ""},
    {"longKills", "Weite Kills", "kLong", Fmt::Int, 1, "Ab 30 m, mit Scharfschützengewehr ab 60 m"},
    // Besondere Kills
    {"kWallbang", "Wallbangs", "kWallbang", Fmt::Int, 1, "Kills durch Wände oder Objekte"},
    {"kNoscope", "Noscopes", "kNoscope", Fmt::Int, 1, "Scharfschützen-Kills ohne Zielfernrohr"},
    {"kSmoke", "Durch Smoke", "kSmoke", Fmt::Int, 1, ""},
    {"kBlind", "Geblendet getroffen", "kBlind", Fmt::Int, 1, "Kills, während du selbst geblendet warst"},
    {"kOnBlind", "Geblendete Gegner getötet", "kOnBlind", Fmt::Int, 0, ""},
    {"kAirborne", "Kills im Sprung", "kAirborne", Fmt::Int, 1, ""},
    {"kOnAirborne", "Springende Gegner getötet", "kOnAirborne", Fmt::Int, 0, ""},
    {"kCollateral", "Kollateral-Kills", "kCollateral", Fmt::Int, 1, "Zwei Gegner mit einer Kugel"},
    {"kKnife", "Messer-Kills", "kKnife", Fmt::Int, 1, ""},
    {"kZeus", "Zeus-Kills", "kZeus", Fmt::Int, 1, ""},
    {"kOnEco", "Kills gegen Eco", "kOnEco", Fmt::Int, 0, "Gegner hatte gespart (Eco oder Halb-Eco)"},
    {"kUnderdog", "Kills gegen besser Ausgerüstete", "kUnderdog", Fmt::Int, 1, "Gegner hatte mindestens 1.500 $ mehr Ausrüstung"},
    {"teamKills", "Teamkills", "teamKills", Fmt::Int, -1, ""},
    {"suicides", "Selbstmorde", "suicides", Fmt::Int, -1, "Auch Fallschaden und eigene Granaten"},
    {"deathsHsPct", "Tode durch Headshot", "100*deathsHs/deaths", Fmt::Pct0, -1, ""},
    {"deathsBlind", "Tode im Blind", "deathsBlind", Fmt::Int, -1, "Gestorben, während du geblendet warst"},
    {"pctRifle", "Gewehre", "100*kRifle/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctSniper", "Scharfschützengewehre", "100*kSniper/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctSmg", "Maschinenpistolen", "100*kSmg/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctShotgun", "Schrotflinten", "100*kShotgun/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctMg", "Maschinengewehre", "100*kMg/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctPistol", "Pistolen", "100*kPistol/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctKnife", "Messer", "100*kKnife/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    {"pctGrenade", "Granaten", "100*kGrenade/kills", Fmt::Pct0, 0, "Anteil an allen Kills"},
    // Schaden & Zeit
    {"dmg", "Schaden gesamt", "dmg", Fmt::Int, 1, "Schaden an Gegnern"},
    {"armorDmg", "Rüstungsschaden", "armorDmg", Fmt::Int, 1, ""},
    {"dmgTakenPerRound", "Schaden erhalten pro Runde", "dmgTaken/rounds", Fmt::Dec1, -1, ""},
    {"dmgBalance", "Schadensbilanz pro Runde", "(dmg-dmgTaken)/rounds", Fmt::Signed1, 1, "Ausgeteilt minus eingesteckt"},
    {"teamDmg", "Teamschaden", "teamDmg", Fmt::Int, -1, ""},
    {"teamDmgTaken", "Teamschaden erhalten", "teamDmgTaken", Fmt::Int, 0, ""},
    {"aliveAvg", "Ø Überlebenszeit pro Runde", "aliveSec/rounds", Fmt::Sec0, 1, "Ab dem Ende der Kaufzeit"},
    {"firstKillTime", "Ø Zeit bis zum ersten Kill", "firstKillSec/firstKillRounds", Fmt::Sec1, 0, "In Runden mit Kill, ab dem Ende der Kaufzeit"},
    {"deathTime", "Ø Todeszeitpunkt", "deathSec/deaths", Fmt::Sec0, 1, "Sekunden nach dem Ende der Kaufzeit"},
    // Economy & Bombe
    {"equipAvg", "Ø Ausrüstungswert", "equipSum/econRounds", Fmt::Money, 0, ""},
    {"spentAvg", "Ø Ausgaben pro Runde", "spent/econRounds", Fmt::Money, 0, ""},
    {"moneyAvg", "Ø Geld zu Rundenbeginn", "moneySum/econRounds", Fmt::Money, 0, ""},
    {"dmgPer1000", "Schaden pro 1.000 $", "1000*dmg/spent", Fmt::Dec1, 1, "Wie viel Schaden du für dein Geld machst"},
    {"pistolWin", "Pistolenrunden gewonnen", "100*pistolWon/pistolRounds", Fmt::Pct0, 1, ""},
    {"ecoWin", "Eco-Runden gewonnen", "100*ecoWon/ecoRounds", Fmt::Pct0, 1, "Runden, in denen du gespart hast"},
    {"semiWin", "Halb-Eco gewonnen", "100*semiWon/semiRounds", Fmt::Pct0, 1, ""},
    {"forceWin", "Force-Buys gewonnen", "100*forceWon/forceRounds", Fmt::Pct0, 1, ""},
    {"fullWin", "Volle Käufe gewonnen", "100*fullWon/fullRounds", Fmt::Pct0, 1, ""},
    {"ecoRounds", "Eco-Runden", "ecoRounds", Fmt::Int, 0, ""},
    {"forceRounds", "Force-Buys", "forceRounds", Fmt::Int, 0, ""},
    {"fullRounds", "Volle Käufe", "fullRounds", Fmt::Int, 0, ""},
    {"plants", "Bomben gelegt", "plants", Fmt::Int, 1, ""},
    {"plantsA", "Gelegt auf A", "plantsA", Fmt::Int, 0, ""},
    {"plantsB", "Gelegt auf B", "plantsB", Fmt::Int, 0, ""},
    {"plantRate", "Plants abgeschlossen", "100*plants/plantStarts", Fmt::Pct0, 1, "Angefangen und auch gelegt"},
    {"defuses", "Bomben entschärft", "defuses", Fmt::Int, 1, ""},
    {"defuseRate", "Defuses abgeschlossen", "100*defuses/defuseStarts", Fmt::Pct0, 1, "Angefangen und auch entschärft"},
    {"defusesVsAlive", "Defuses unter Druck", "defusesVsAlive", Fmt::Int, 1, "Entschärft, während noch Terroristen lebten"},
};

#undef CLUTCHES
#undef CLUTCHWINS
#undef NADES
#undef KPR
#undef APR
#undef IMPACT
#undef RATING2
#undef RATING1

std::vector<Stat> makeStats() {
    std::vector<Stat> out;
    for (const StatDef& d : kDefs) {
        Stat s{&d, Expr()};
        std::string err;
        if (!s.expr.compile(d.expr, &err)) continue;   // wird im Test geprüft
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

const std::vector<Stat>& allStats() {
    static const std::vector<Stat> stats = makeStats();
    return stats;
}

const Stat* findStat(const std::string& id) {
    for (const Stat& s : allStats())
        if (id == s.def->id) return &s;
    return nullptr;
}

bool statSideless(const Stat& s) {
    // Zähler, die es nur fürs ganze Match gibt (nicht in statsT/statsCT)
    static const char* const kMatchOnly[] = {"matches", "matchWins", "matchLosses", "matchDraws", "mvps", "score"};
    const char* e = s.def->expr;
    for (size_t i = 0; e[i];) {
        if (std::isalpha((unsigned char)e[i]) || e[i] == '_') {
            size_t j = i;
            while (e[j] && (std::isalnum((unsigned char)e[j]) || e[j] == '_')) ++j;
            std::string ident(e + i, j - i);
            for (const char* k : kMatchOnly)
                if (ident == k) return true;
            i = j;
        } else {
            ++i;
        }
    }
    return false;
}

double statValue(const std::string& id, const Counters& c) {
    const Stat* s = findStat(id);
    return s ? s->expr.eval(c) : std::numeric_limits<double>::quiet_NaN();
}

const std::vector<StatGroup>& statGroups() {
    static const std::vector<StatGroup> groups = {
        {"overview", "Allgemein",
         {"rating", "rating1", "impact", "kd", "kdDiff", "kpr", "dpr", "apr", "adr", "kast", "hsPct", "survival",
          "roundsWithKill", "winrate", "roundWinrate", "matches", "rounds", "kills", "deaths", "assists", "flashAssists",
          "hsKills", "mvps", "mvpPerMatch", "scorePerMatch"}},
        {"multi", "Multikills", {"k1", "k2", "k3", "k4", "k5", "multiRounds", "multiRate", "bigRoundsPerMatch"}},
        {"opening", "Eröffnung & Trades",
         {"openDuels", "openRate", "openSuccess", "openKills", "openDeaths", "openKillsPerRound", "openConv",
          "openDeathWon", "openTraded", "tradeKills", "tradeKillsPerRound", "tradedDeaths", "tradedPct"}},
        {"clutch", "Clutches",
         {"clutches", "clutchWins", "clutchRate", "clutch1Rate", "clutch2Rate", "clutch3Rate", "clutch4Rate",
          "clutch5Rate", "clutchKills", "clutchSaved"}},
        {"utility", "Utility",
         {"nadesPerRound", "utilDmg", "utilDmgPerRound", "flashThrown", "enemiesFlashed", "enemiesPerFlash",
          "blindTime", "blindTimePerRound", "flashEffective", "flashAssists", "flashLeadKills", "teamFlashed",
          "teamPerFlash", "selfFlashed", "gotFlashed", "gotFlashedDur", "heThrown", "heDmg", "hePerNade",
          "heHitsPerNade", "fireThrown", "fireDmg", "firePerNade", "smokeThrown", "decoyThrown", "nadeKills",
          "teamUtilDmg"}},
        {"aim", "Zielen",
         {"accuracy", "firstShotAcc", "sprayAcc", "movingPct", "movingAcc", "standingAcc", "headHitPct", "hsPct",
          "shots", "shotsPerKill", "dmgPerHit", "avgDist", "maxDist", "longKills"}},
        {"hitzones", "Trefferzonen", {"headHitPct", "hitNeckPct", "hitChestPct", "hitStomachPct", "hitArmsPct", "hitLegsPct"}},
        {"special", "Besondere Kills",
         {"kWallbang", "kNoscope", "kSmoke", "kBlind", "kOnBlind", "kAirborne", "kOnAirborne", "kCollateral",
          "kKnife", "kZeus", "nadeKills", "kOnEco", "kUnderdog", "longKills", "teamKills", "suicides",
          "deathsHsPct", "deathsBlind"}},
        {"classes", "Kills nach Waffenart",
         {"pctRifle", "pctSniper", "pctSmg", "pctShotgun", "pctMg", "pctPistol", "pctKnife", "pctGrenade"}},
        {"damage", "Schaden & Zeit",
         {"dmg", "adr", "armorDmg", "dmgTakenPerRound", "dmgBalance", "utilDmgPerRound", "teamDmg", "teamDmgTaken",
          "aliveAvg", "firstKillTime", "deathTime"}},
        {"economy", "Economy",
         {"equipAvg", "spentAvg", "moneyAvg", "dmgPer1000", "pistolWin", "ecoWin", "semiWin", "forceWin", "fullWin",
          "ecoRounds", "forceRounds", "fullRounds"}},
        {"bomb", "Bombe", {"plants", "plantsA", "plantsB", "plantRate", "defuses", "defuseRate", "defusesVsAlive"}},
    };
    return groups;
}

const StatGroup* findGroup(const std::string& id) {
    for (const auto& g : statGroups())
        if (id == g.id) return &g;
    return nullptr;
}

// ---- Karriere -------------------------------------------------------------------------------------------------
Counters matchCounters(const PlayerInfo& p, const Counters& base) {
    Counters c = base;
    c.add(Keys::id("matches"), 1);
    if (p.result == "win") c.add(Keys::id("matchWins"), 1);
    else if (p.result == "loss") c.add(Keys::id("matchLosses"), 1);
    else c.add(Keys::id("matchDraws"), 1);
    return c;
}

namespace {

void addResult(GroupAgg& g, const PlayerInfo& p) {
    ++g.matches;
    if (p.result == "win") ++g.wins;
    else if (p.result == "loss") ++g.losses;
    else ++g.draws;
}

}  // namespace

Career buildCareer(const std::vector<const Match*>& matches, const std::string& steamId, const Filter& filter) {
    Career c;
    c.steamId = steamId;
    std::vector<const Match*> sel;
    for (const Match* m : matches) {
        if (!m || !m->player(steamId)) continue;
        if (!filter.mode.empty() && m->mode != filter.mode) continue;
        if (!filter.map.empty() && m->map != filter.map) continue;
        sel.push_back(m);
    }
    std::stable_sort(sel.begin(), sel.end(), [](const Match* a, const Match* b) { return a->date < b->date; });
    if (filter.lastN > 0 && int(sel.size()) > filter.lastN) sel.erase(sel.begin(), sel.end() - filter.lastN);

    for (const Match* m : sel) {
        const PlayerInfo& p = *m->player(steamId);
        c.name = p.name;
        ++c.matches;
        if (p.result == "win") ++c.wins;
        else if (p.result == "loss") ++c.losses;
        else ++c.draws;

        Counters all = matchCounters(p, p.all);
        c.all.merge(all);
        c.t.merge(p.t);
        c.ct.merge(p.ct);
        // Ergebniszähler auch für die Seiten, damit Formeln wie "MVPs pro Match" dort funktionieren
        Counters tc = matchCounters(p, Counters()), ctc = tc;
        c.t.merge(tc);
        c.ct.merge(ctc);

        GroupAgg& mg = c.maps[m->map];
        addResult(mg, p);
        mg.all.merge(all);
        mg.t.merge(p.t);
        mg.ct.merge(p.ct);
        GroupAgg& md = c.modes[m->mode];
        addResult(md, p);
        md.all.merge(all);
        md.t.merge(p.t);
        md.ct.merge(p.ct);

        for (const auto& w : p.weapons) {
            auto& slot = c.weapons[w.name];
            if (slot.first.empty()) slot.first = w.type;
            slot.second.merge(w.c);
        }
        for (const auto& d : p.deathsBy) c.deathsBy[d.first] += d.second;

        MatchPoint pt;
        pt.match = m;
        pt.me = &p;
        pt.rating2 = p.rating;
        pt.adr = p.adr;
        pt.kast = p.kast;
        double k = p.all.get("kills"), d = p.all.get("deaths");
        pt.kd = d > 0 ? k / d : k;
        pt.hsPct = k > 0 ? 100 * p.all.get("hsKills") / k : 0;
        if (p.rankType == 11) {
            pt.premierOld = p.rankOld;
            pt.premierNew = p.rankNew;
            int latest = p.rankNew > 0 ? p.rankNew : p.rankOld;
            if (latest > 0) c.premierCurrent = latest;
            c.premierBest = std::max({c.premierBest, p.rankOld, p.rankNew});
        }
        c.points.push_back(pt);
    }
    return c;
}

}  // namespace fd
