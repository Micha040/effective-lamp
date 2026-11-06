// SnowDesk – Schnee-Simulation (plattformunabhängig, ohne Windows-Header).
// Eine World gehört zu genau einem Monitor; Koordinaten in physischen Pixeln relativ zur linken oberen Monitorecke.
// Fenster-Oberkanten ("Plattformen") tragen eine Schneedecke; bewegt sich ein Fenster, fällt sie herunter.
//
// Die Schneedecke wächst gleichmäßig aus vielen kleinen, unsichtbaren Schnee-Portionen (gleiche Flugbahnen wie die
// sichtbaren Flocken, also auch mit Windschatten). Die sichtbaren Flocken bleiben beim Landen kurz liegen und
// schmelzen dann in die Decke ein. So wächst der Schnee weich und ohne Sprünge, egal wie schnell er sich füllt.
//
// Echtes Wetter: Regen (Spuren, Spritzer), Wassertropfen auf den Fensterkanten (sammeln sich, rollen zu den Ecken,
// hängen dort und tropfen ab), Tauwetter (Schnee schmilzt, Schmelzwasser tropft), Nebel und Blitze.
#pragma once
#include <cstdint>
#include <vector>

namespace snow {

enum Kind : uint8_t {
    KindSoft = 0, KindCrystal = 1, KindClump = 2, KindCrumb = 3, KindPuff = 4,
    KindRain = 5,     // Regenspur: size = Länge, soft = Breite, rot = Richtung (0 = senkrecht nach unten)
    KindDrop = 6,     // Wassertropfen (liegt, hängt oder fällt): size = Durchmesser, soft = senkrechte Streckung
    KindSplash = 7,   // Spritzer
    KindFog = 8,      // Nebelschwade: size = Durchmesser
    KindFlash = 9     // Blitz: ganzer Bildschirm, alpha = Helligkeit
};
enum ReleaseStyle : int { ReleaseMix = 0, ReleaseClump = 1, ReleaseCrumble = 2, ReleaseGentle = 3 };

struct Params {
    float amount = 1.0f;         // Schneemenge (Leicht 0.45, Mittel 1, Schneesturm 2.6)
    float flakeSize = 1.0f;      // Größenfaktor
    float fallSpeed = 1.0f;      // Geschwindigkeitsfaktor
    float wind = 0.3f;           // 0 = windstill, 1 = kräftig nach rechts, -1 = nach links (Böen kommen automatisch dazu)
    float maxPileDip = 32.0f;    // maximale Schneehöhe in DIP (bei 100 % Skalierung = Pixel), 0 = nichts bleibt liegen
    float fillMinutes = 6.0f;    // so lange dauert es bei Menge 1, bis ein Fenster voll ist
    float crystalShare = 0.07f;  // Anteil Schneekristalle unter den mittleren/nahen Flocken
    int releaseStyle = ReleaseMix;
    bool mouseBlow = false;      // Maus pustet Flocken weg
    // Echtes Wetter
    float rain = 0.0f;           // Regen: 0 = keiner, 0.3 Niesel, 1 mäßig, 2.5 Starkregen
    bool drizzle = false;        // Nieselregen: feine, langsame Tröpfchen
    bool wetSnow = false;        // nasser Schnee: bleibt nicht liegen
    float meltDipPerMin = 0.0f;  // Tauwetter: so viele DIP pro Minute schmilzt liegender Schnee (0 = bleibt liegen)
    float fog = 0.0f;            // Nebel 0..1
    bool thunder = false;        // Gewitter: ab und zu ein Blitz
};

// Eine Kante, auf der Schnee liegen bleiben kann: Fenster-Oberkante oder Boden (Taskleiste/Bildschirmrand).
struct PlatformIn {
    uint64_t id = 0;
    float left = 0, top = 0, right = 0;  // sichtbare Oberkante, monitor-lokal
    float corner = 0;                    // Eckenradius (Windows 11: 8 px bei 100 %)
    bool floor = false;                  // Boden: Ränder sind Wände, Schnee fällt dort nicht herunter
};

struct Sprite {
    float x, y, size, alpha, rot, soft, seed;
    uint32_t kind;
};

// Dreiecksliste für die Schneedecken. dist = Abstand unter der Schneeoberfläche (0 = Oberfläche), height = Dicke.
struct PileVertex {
    float x, y, dist, height;
};

struct Stats {
    int flakes = 0, crystals = 0, clumps = 0, crumbs = 0, puffs = 0, resting = 0, piles = 0;
    int rain = 0, drops = 0, splashes = 0, beads = 0, hanging = 0, fog = 0;
    double water = 0;                    // Wasser auf den Kanten (Tropfen-Einheiten)
    double pileMass = 0, airMass = 0;    // Masse in px² (Fläche)
    double spawnedMass = 0, lostMass = 0, landedMass = 0;
};

class World {
public:
    explicit World(uint64_t seed = 1);

    void configure(int widthPx, int heightPx, float dpiScale);
    void setParams(const Params& p);
    const Params& params() const { return p_; }

    // Komplette Liste der aktuellen Plattformen dieses Monitors. Verschwundene/bewegte Plattformen lassen
    // ihren Schnee fallen, bei reiner Größenänderung nur den Teil ohne Halt.
    void setPlatforms(const std::vector<PlatformIn>& platforms);

    void setCursor(bool inside, float x, float y);
    void setSnowing(bool on) { snowing_ = on; }
    bool snowing() const { return snowing_; }

    void step(float dt);
    void prefill();          // beim Einschalten: Flocken schon über den ganzen Bildschirm verteilt (sanft eingeblendet)
    void shakeAll();         // "Alles abschütteln": alle Fenster-Schneedecken fallen herunter (Boden bleibt)
    void startThaw();        // ausschalten: es hört auf zu schneien, alles taut in ~2 s weg
    bool thawing() const { return thawing_; }
    void resume();           // wieder einschalten (auch mitten im Abtauen)
    void clear();            // alles sofort weg

    bool active() const;     // gibt es etwas zu zeichnen/zu bewegen?
    // Wie oft neu gezeichnet werden muss: -1 = volle Bildrate (es fällt etwas), 0 = nichts bewegt sich (ein Bild
    // reicht), sonst Bilder pro Sekunde für langsame Bewegung (Nebel, rollende Tropfen, Tauen)
    float frameRateHint() const;
    uint64_t version() const { return version_; }   // ändert sich, sobald sich am Bild etwas ändert
    void collectSprites(std::vector<Sprite>& out) const;
    void buildPileMesh(std::vector<PileVertex>& out) const;
    Stats stats() const;

    // für Tests
    float pileElevation(uint64_t id, float x) const;   // Schneehöhe über der (geraden) Kante an x (-1 = keine Plattform)
    float pileHeightAt(uint64_t id, float x) const;    // Schicht-Dicke an x
    float maxPilePx() const { return maxPilePx_; }
    float cellWidth() const { return cell_; }
    bool hasPile(uint64_t id) const;
    float spawnRate() const;                            // sichtbare Flocken pro Sekunde (aktuell)
    float rainRate() const;                             // Regenspuren pro Sekunde (aktuell)
    float windNow() const { return windNow_; }
    int beadCount(uint64_t id) const;                   // Tropfen auf dieser Kante (liegend + hängend)
    float beadWater(uint64_t id) const;                 // Wasser auf dieser Kante (Tropfen-Einheiten)
    void addWaterForTest(uint64_t id, float x, float vol);
    void flashNow();                                    // Blitz sofort (Test)

private:
    struct Particle {
        float x, y, vx, vy;
        float size, alpha, depth, soft;
        float swayPhase, swayFreq, swayAmp, fall;
        float rot, rotSpeed, seed;
        float mass;
        float life, maxLife;
        float fadeIn;
        float water;         // Regen/Tropfen: Wassermenge (Tropfen-Einheiten)
        uint64_t ignoreId;   // von diesem Fenster heruntergefallen: dort nicht wieder landen
        uint64_t restId;     // liegt gerade auf dieser Schneedecke (schmilzt ein)
        uint8_t kind;
        uint8_t resting;
    };
    // Wassertropfen auf einer Fensterkante
    struct Bead {
        float x;        // Mitte (monitor-lokal); hängend: die Ecke (left bzw. right)
        float vol;      // Wassermenge (1 = ein großer Regentropfen)
        float vx;       // rollt gerade (px/s)
        float seed;     // Abreißgröße beim Hängen, Glanzlicht
        int hang;       // -1 liegt auf der Kante, 0/1 hängt an der linken/rechten Ecke
    };
    struct Pile {
        uint64_t id;
        bool floor;
        float left, right, top, corner;
        int n;
        std::vector<float> h;      // Schneedicke je Zelle (px)
        std::vector<float> pend;   // gelandet, sinkt gerade weich ein (px)
        std::vector<float> drop;   // Absenkung der Kante an den runden Ecken (px)
        std::vector<float> mod;    // natürliche Wellen: Anteil der Maximalhöhe (0.84 … 1)
        float spillAcc[2];         // an den Enden überstehender Schnee (px²), rieselt ab und zu herunter
        bool absorb;               // Schnee verschwindet hier (kein Platz bzw. "nichts bleibt liegen")
        bool empty;                // nichts drauf (spart Rechenzeit)
        float settle;              // > 0: Fenster wird gerade bewegt, fängt (noch) keinen Schnee
        std::vector<Bead> beads;   // Wassertropfen (nur Fensterkanten)
        float meltWater;           // Schmelzwasser, das noch zu den Ecken läuft
        bool settling;             // frischer Schnee sinkt noch ein (braucht noch Bilder)
    };

    float rnd();
    float rnd(float a, float b);
    float dip(float v) const { return v * scale_; }

    void recalc();
    Particle makeSnowParticle(float y, bool fadeIn);
    void spawnFlake(float y, bool fadeIn);
    void flushPending();
    bool isAbsorbing(const PlatformIn& in) const;
    float capOf(const Pile& p) const;
    void initShape(Pile& p) const;
    Pile makePile(const PlatformIn& in) const;
    float surfaceAt(const Pile& p, float x) const;
    int cellOf(const Pile& p, float x) const;
    void deposit(Pile& p, float x, float mass, float sigma);
    void depositVirtual(float dt);
    float bandThickness(const Pile& p, bool rightSide) const;
    void settlePile(Pile& p, float dt);
    void trimToCap(Pile& p);
    void releaseCells(Pile& p, int from, int to, float vxCarry);
    void releaseChunk(const Pile& p, int from, int to, float mass, float vxCarry);
    void releaseResting(uint64_t pileId, float x0, float x1);
    void emitCrumb(float x, float y, float mass, float vx, float vy, uint64_t ignoreId = 0);
    void emitClump(float x, float y, float mass, float vx, float vy, uint64_t ignoreId = 0);
    void emitPuffs(float x, float y, float mass);
    bool land(Particle& q, float prevContact);
    void updateWind(float dt);
    // Regen & Wasser
    void spawnRain();
    bool landWater(Particle& q, float prevY);
    void emitDrop(float x, float y, float vol, float vx, float vy, uint64_t ignoreId);
    void emitSplash(float x, float y, int n, float strength);
    float beadRadius(float vol) const;
    int maxBeads(const Pile& p) const;
    // Hängender Tropfen an einer Ecke: außen an der Fensterseite (die Schneeebene liegt hinter den Fenstern)
    float hangX(const Pile& p, int side, float r) const;
    float hangY(const Pile& p, float r, float stretch) const;
    float edgeY(const Pile& p, float x) const;
    void addWater(Pile& p, float x, float vol);
    void addHanging(Pile& p, int side, float vol);
    void stepBeads(Pile& p, float dt, bool raining);
    void releaseBeads(Pile& p, float x0, float x1);
    void meltPile(Pile& p, float dt, float ratePx);
    void stepFog(float dt);
    float flashAlpha() const;

    Params p_;
    int width_ = 0, height_ = 0;
    float scale_ = 1.0f;
    float cell_ = 2.0f, maxPilePx_ = 32.0f;
    float growth_ = 0.0f;          // px/s Dickenzuwachs bei Menge 1
    float gravity_ = 1500.0f;
    uint64_t rng_;
    std::vector<Particle> parts_;
    std::vector<Particle> pending_;   // neu entstandene Teilchen (werden nach der Schleife übernommen)
    std::vector<Pile> piles_;
    float time_ = 0, spawnAcc_ = 0, virtAcc_ = 0;
    int restingCount_ = 0;
    bool snowing_ = true, thawing_ = false;
    float thawLeft_ = 0, thawAlpha_ = 1.0f;
    float windNow_ = 0, gust_ = 0, gustTarget_ = 0, gustTimer_ = 5, gustPhase_ = 0;
    float windPhase1_ = 0, windPhase2_ = 0;
    bool cursorIn_ = false;
    float cx_ = 0, cy_ = 0, cvx_ = 0, cvy_ = 0, pcx_ = 0, pcy_ = 0;
    bool cursorValid_ = false;
    double spawnedMass_ = 0, lostMass_ = 0, landedMass_ = 0;
    float rainAcc_ = 0;
    std::vector<Particle> fog_;        // Nebelschwaden (eigene Liste: langsam, werden nicht gezählt)
    float fogLevel_ = 0;
    float flashT_ = -1, flashNext_ = 15;
    uint64_t version_ = 0;
};

}  // namespace snow
