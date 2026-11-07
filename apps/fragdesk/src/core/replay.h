// FragDesk – Daten der 2D-Ansicht (von fdanalyze -replay, siehe analyzer/replay.go).
//
// Dateiformat "FDRP" (Little Endian; str = u16 Länge + UTF-8):
//   "FDRP" u16 Version (1) u16 0
//   f32 Tickrate, u16 Abstand der Abtastungen in Ticks, str Karte
//   u16 Spieler   { u64 SteamID (0 = Bot), str Name }
//   u16 Waffen    { str Name }                                  Index 0 = keine
//   u32 Frames, u32 Bytes der Frames,
//       je Frame  { i32 Tick, u8 Bombe (0 getragen/unbekannt, 1 liegt, 2 gelegt) [i16 x,y,z wenn 1/2],
//                   u8 n, n × { u8 Spieler, i16 x,y,z, u8 Blickrichtung (256 = 360°), u8 HP, u8 Waffe, u8 Flags } }
//                   – nur lebende Spieler
//   u32 Flugbahnen { u8 Art, u8 Werfer, u8 CT, u16 n, n × { i32 Tick, i16 x,y,z } }
//   u32 Wirkungen  { u8 Art, u8 Werfer, u8 CT, i32 Start, i32 Ende, i16 x,y,z, u8 n, n × { i16 x, i16 y } (Feuerfläche) }
//   u32 Kills      { i32 Tick, u8 Killer, u8 Opfer, u8 Helfer, u8 Waffe, u8 Flags, i16 Killer x,y,z, i16 Opfer x,y,z }
//   u32 Schüsse    { i32 Tick, u8 Spieler, u8 Waffe }
//   u32 Bombe      { i32 Tick, u8 Art, u8 Spieler, i16 x,y,z }
//   "END!"
// Spieler 255 = unbekannt.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fd {

enum : uint8_t { RpCT = 1, RpBomb = 2, RpDefusing = 4, RpPlanting = 8, RpDucking = 16, RpScoped = 32, RpFlashed = 64, RpKit = 128 };
enum : uint8_t { GrSmoke = 1, GrFlash, GrHE, GrMolotov, GrIncendiary, GrDecoy };
enum : uint8_t { FxSmoke = 1, FxFire, FxFlash, FxHE, FxDecoy };
enum : uint8_t { KfHeadshot = 1, KfWallbang = 2, KfNoScope = 4, KfSmoke = 8, KfBlind = 16 };
enum : uint8_t { BombPlanted = 1, BombDefused, BombExploded };
constexpr uint8_t kNoPlayer = 255;
constexpr int kReplayVersion = 1;

struct Vec3s {
    int16_t x = 0, y = 0, z = 0;
};

struct ReplayPlayer {
    std::string steamId;   // leer bei Bots
    std::string name;
};

struct ReplaySample {
    uint8_t player = 0, yaw = 0, hp = 0, weapon = 0, flags = 0;
    Vec3s pos;
};

struct ReplayFrame {
    int tick = 0;
    uint8_t bomb = 0;
    Vec3s bombPos;
    uint32_t first = 0;   // Index in Replay::samples
    uint8_t count = 0;
};

struct ReplayTrack {
    uint8_t kind = 0, thrower = kNoPlayer;
    bool ct = false;
    std::vector<std::pair<int, Vec3s>> points;
};

struct ReplayEffect {
    uint8_t kind = 0, thrower = kNoPlayer;
    bool ct = false;
    int start = 0, end = 0;   // end 0 = unbekannt
    Vec3s pos;
    std::vector<std::pair<int16_t, int16_t>> hull;
};

struct ReplayKill {
    int tick = 0;
    uint8_t killer = kNoPlayer, victim = kNoPlayer, assister = kNoPlayer, weapon = 0, flags = 0;
    Vec3s killerPos, victimPos;
};

struct ReplayShot {
    int tick = 0;
    uint8_t player = kNoPlayer, weapon = 0;
};

struct ReplayBomb {
    int tick = 0;
    uint8_t kind = 0, player = kNoPlayer;
    Vec3s pos;
};

struct Replay {
    float tickrate = 64;
    int interval = 8;
    std::string map;
    std::vector<ReplayPlayer> players;
    std::vector<std::string> weapons;
    std::vector<ReplayFrame> frames;
    std::vector<ReplaySample> samples;
    std::vector<ReplayTrack> tracks;
    std::vector<ReplayEffect> effects;
    std::vector<ReplayKill> kills;
    std::vector<ReplayShot> shots;
    std::vector<ReplayBomb> bomb;
};

// false bei falschem oder abgeschnittenem Inhalt (err sagt warum)
bool parseReplay(const std::string& bytes, Replay& out, std::string& err);

// Index des letzten Frames mit tick <= t (0, wenn t vor dem ersten liegt); frames muss aufsteigend sein
size_t frameAt(const Replay& r, double tick);

// Spieler zu einem Zeitpunkt (Ticks mit Bruchteil), zwischen zwei Abtastungen linear verschoben
struct PlayerAt {
    uint8_t player = 0, hp = 0, weapon = 0, flags = 0;
    float x = 0, y = 0, z = 0, yaw = 0;   // yaw in Grad
};
void playersAt(const Replay& r, double tick, std::vector<PlayerAt>& out);

// Name einer Waffe (leer bei unbekannt)
const std::string& replayWeapon(const Replay& r, uint8_t idx);

}  // namespace fd
