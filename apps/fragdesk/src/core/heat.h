// FragDesk – Heatmap: Daten eines Matches (von fdanalyze neben der Auswertung, siehe analyzer/extras.go) und daraus
// Punkte und Dichte-Raster für die Ansicht.
//
// Dateiformat "FDHM" (Little Endian; str = u16 Länge + UTF-8):
//   "FDHM" u16 Version (1) u16 0, f32 Tickrate, str Karte
//   u16 Spieler   { u64 SteamID (0 = Bot), str Name }
//   u16 Waffen    { str Name }                                   Index 0 = keine
//   u32 Kills     { i32 Tick, u8 Schütze, u8 Opfer, u8 Flags, u8 Waffe, i16 Schütze x,y,z, i16 Opfer x,y,z }
//   u32 Granaten  { i32 Wurf-Tick, i32 Lande-Tick, u8 Art, u8 Werfer, u8 Flags, i16 Wurf x,y,z, i16 Landung x,y,z }
//   u32 Abtastungen, u32 Bytes, je { i32 Tick, u8 n, n × { u8 Spieler, u8 Flags, i16 x,y,z } }   (einmal pro Sekunde)
//   "END!"
// Spieler 255 = unbekannt. Granatenarten wie in replay.h (GrSmoke …).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model.h"
#include "radar.h"
#include "replay.h"

namespace fd {

enum : uint8_t { HkKillerCT = 1, HkVictimCT = 2, HkHeadshot = 4 };
enum : uint8_t { HnCT = 1, HnNoThrow = 2 };
enum : uint8_t { HsCT = 1 };
constexpr int kHeatVersion = 1;

struct HeatKill {
    int tick = 0;
    uint8_t killer = kNoPlayer, victim = kNoPlayer, flags = 0, weapon = 0;
    Vec3s killerPos, victimPos;
};

struct HeatNade {
    int tick = 0, land = 0;
    uint8_t kind = 0, thrower = kNoPlayer, flags = 0;
    Vec3s from, to;
};

struct HeatSample {
    uint8_t player = 0, flags = 0;
    Vec3s pos;
};

struct HeatFrame {
    int tick = 0;
    uint32_t first = 0;   // Index in Heat::samples
    uint8_t count = 0;
};

struct Heat {
    float tickrate = 64;
    std::string map;
    std::vector<ReplayPlayer> players;
    std::vector<std::string> weapons;
    std::vector<HeatKill> kills;
    std::vector<HeatNade> nades;
    std::vector<HeatFrame> frames;
    std::vector<HeatSample> samples;
};

// false bei falschem oder abgeschnittenem Inhalt (err sagt warum)
bool parseHeat(const std::string& bytes, Heat& out, std::string& err);

// ---- Aufbereitung für die Ansicht ------------------------------------------------------------------------------
enum HeatLayer : uint8_t { HlPresence, HlKills, HlDeaths, HlSmoke, HlMolotov, HlFlash, HlHE, HlCount };
enum : uint8_t { WhoMe = 1, WhoTeam = 2, WhoEnemy = 4 };
enum : uint8_t { SideT = 1, SideCT = 2 };

// Aufenthalt eines Spielers (eine Sekunde), kompakt: Lage auf dem Radar in 1/65535
struct HeatPoint {
    uint16_t u = 0, v = 0;
    uint8_t who = 0, side = 0;
    bool lower = false;
};

// Kill, Tod oder Granate – mit allem, was die Ansicht zum Anzeigen und Anspringen braucht
struct HeatMark {
    float u = 0, v = 0;     // Ort (Kill: Schütze, Tod: Opfer, Granate: Landung) auf dem Radar, 0..1
    float fu = 0, fv = 0;   // Granate: Wurfpunkt (sonst = u, v)
    uint8_t layer = 0, who = 0, side = 0;
    bool lower = false, fromLower = false, thrown = false;
    int match = 0;          // Index in HeatSet::matches
    int round = 0, tick = 0;
    std::string tip;        // Tooltip (UTF-8)
};

struct HeatMatchRef {
    std::string checksum, label;   // label: z. B. „Mirage · 13:11 · 07.11.2026“ (leer in der Ansicht eines Matches)
};

struct HeatSet {
    std::vector<HeatPoint> points;
    std::vector<HeatMark> marks;
    std::vector<HeatMatchRef> matches;
    bool hasMe = false;   // „Ich“ kommt vor (sonst bedeuten Team/Gegner Team A/Team B)
};

// Was aus einem Match nötig ist (wird für das Laden im Hintergrund kopiert)
struct HeatMatchInfo {
    std::string checksum, label, myTeam;   // myTeam: A/B, leer, wenn me nicht mitspielt
    double tickrate = 64;
    std::vector<RoundInfo> rounds;
};
HeatMatchInfo heatMatchInfo(const Match& m, const std::string& me, const std::string& label);

// Punkte eines Matches anhängen. Nur was in eine Runde fällt (ab Ende der Kaufzeit bis kurz nach Rundenende) zählt.
void addMatchHeat(HeatSet& set, const HeatMatchInfo& info, const Heat& h, const RadarMap& map, const std::string& me);

struct HeatFilter {
    uint8_t who = WhoMe | WhoTeam | WhoEnemy;
    uint8_t sides = SideT | SideCT;
    bool match(uint8_t w, uint8_t s) const { return (who & w) && (sides & s); }
};

// Dichte einer Ebene als n×n-Raster (Zeilen von oben), weichgezeichnet (sigma in Rasterzellen) und auf 0..1 gebracht
// (am 99,5-%-Wert der belegten Zellen, damit ein einzelner Brennpunkt nicht alles andere verblassen lässt).
// Liefert die Zahl der Punkte.
int heatDensity(const HeatSet& set, HeatLayer layer, const HeatFilter& f, bool lower, int n, float sigma, std::vector<float>& out);

}  // namespace fd
