// FragDesk – Daten eines ausgewerteten Matches (Ausgabe von fdanalyze, Schema 1 und 2)
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.h"

namespace fd {

// ---- Zähler ---------------------------------------------------------------------------------------------------
// Alle Zähler eines Spielers sind frei benannt (siehe analyzer/stats.go). Sie werden auf feste Nummern abgebildet,
// damit Summen über viele Matches schnell gehen. Schlüssel mit der Endung "Max" werden über das Maximum
// zusammengefasst, alle anderen addiert.
class Keys {
public:
    static uint16_t id(const std::string& name);       // legt unbekannte Schlüssel an
    static int find(const std::string& name);          // -1, wenn unbekannt
    static const std::string& name(uint16_t id);
    static bool isMax(uint16_t id);
    static size_t count();
};

struct Counters {
    std::vector<double> v;
    double get(uint16_t id) const { return id < v.size() ? v[id] : 0.0; }
    double get(const char* key) const;
    void set(uint16_t id, double x);
    void add(uint16_t id, double x) { set(id, get(id) + x); }
    void merge(const Counters& o);   // Summe bzw. Maximum
    bool empty() const;
};

bool readCounters(const json::Value& obj, Counters& out);

// ---- Match ----------------------------------------------------------------------------------------------------
struct TeamInfo {
    std::string letter, name, startSide;
    int score = 0, scoreFirstHalf = 0, scoreSecondHalf = 0;
};

struct RoundInfo {
    int n = 0, start = 0, freezeEnd = 0, end = 0, endOfficial = 0, overtime = 0;
    std::string winner, winnerSide, reason, sideA, econA, econB, bomb, site, planter, defuser;
    int scoreA = 0, scoreB = 0, equipA = 0, equipB = 0, spentA = 0, spentB = 0, moneyA = 0, moneyB = 0;
    int64_t durationMs = 0;
    int firstKill = -1;
};

struct PlayerRound {
    int n = 0;
    std::string side;
    bool won = false, dead = false, traded = false, kast = false, clutchWon = false, plant = false, defuse = false;
    int k = 0, hs = 0, a = 0, fa = 0, dmg = 0, util = 0, deathTick = 0, clutch = 0, equip = 0, spent = 0, money = 0,
        flashed = 0, firstKillTick = 0;
    double alive = 0;
    std::string killer, killerWeapon, econ, open;
};

struct WeaponStats {
    std::string name, type;
    Counters c;
};

struct OpponentStats {
    std::string steamId;
    int k = 0, d = 0, dmgDealt = 0, dmgTaken = 0, openK = 0, openD = 0;
};

struct PlayerInfo {
    std::string steamId, name, team, result, crosshair;
    int slot = 0, rankType = 0, rankOld = 0, rankNew = 0, wins = 0, mvps = 0, score = 0;
    double rating = 0, rating1 = 0, kast = 0, adr = 0;
    Counters all, t, ct;
    std::vector<WeaponStats> weapons;
    std::vector<std::pair<std::string, int>> deathsBy;
    std::vector<PlayerRound> rounds;
    std::vector<OpponentStats> opponents;

    double stat(const char* key) const { return all.get(key); }
    const PlayerRound* round(int n) const;
};

struct KillInfo {
    int tick = 0, round = 0, penetrated = 0;
    std::string killer, victim, assister, killerSide, victimSide, weapon, weaponType;
    bool headshot = false, smoke = false, noscope = false, killerBlind = false, victimBlind = false, killerAir = false,
         flashAssist = false, trade = false, traded = false, teamKill = false;
    double distance = 0;
    float kx = 0, ky = 0, vx = 0, vy = 0;
};

struct ClutchInfo {
    int round = 0, tick = 0, vs = 0, kills = 0;
    std::string player, side;
    bool won = false, survived = false;
};

struct HighlightInfo {
    std::string id, player, side;
    int round = 0, score = 0, kills = 0, clutch = 0;
    std::vector<std::string> tags, weapons;
    std::vector<std::pair<int, int>> segments;
    std::vector<int> killTicks;

    double seconds(double tickrate) const;
    bool hasTag(const char* tag) const;
};

// Version der Auswertung. 2: mit Crosshair/Reaktion und Heatmap-Datei – ältere Matches lassen sich neu auswerten.
constexpr int kMatchSchema = 2;

struct Match {
    int schema = 0;
    std::string analyzer, checksum;
    // Demo
    std::string demoPath, demoFile, demoModified, packed;
    int64_t demoSize = 0;
    // Match
    std::string source, mode, map, date, serverName, shareCode, winner;
    double duration = 0, tickrate = 64;
    int tickCount = 0, maxRounds = 0, overtimes = 0, scoreA = 0, scoreB = 0, buildNumber = 0;
    bool ranked = false;
    TeamInfo teams[2];
    std::vector<RoundInfo> rounds;
    std::vector<PlayerInfo> players;
    std::vector<KillInfo> kills;
    std::vector<ClutchInfo> clutches;
    std::vector<HighlightInfo> highlights;

    const PlayerInfo* player(const std::string& steamId) const;
    const TeamInfo* team(const std::string& letter) const;
    std::string playerName(const std::string& steamId) const;
    int scoreOf(const std::string& letter) const { return letter == "A" ? scoreA : scoreB; }
};

// Liest die Ausgabe von fdanalyze. details=false lässt Kills, Runden der Spieler, Gegner und Highlights weg
// (für die Bibliothek: spart Speicher).
bool parseMatch(const std::string& text, Match& out, std::string& error, bool details = true);

}  // namespace fd
