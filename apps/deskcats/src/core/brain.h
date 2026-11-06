// DeskCats – Verhalten einer Katze: Ruhen, Herumlaufen, Springen, Fallen, Fressen, Spielen, Schlafen, Getragen werden.
// Reine Logik ohne Windows (testbar). Positionen in Bildschirm-Pixeln, y nach unten.
// Sparsam: Solange nichts passiert, meldet tick() lange Pausen bis zum nächsten Aufruf zurück.
#pragma once
#include "anim.h"
#include "world.h"

#include <cstdint>
#include <random>

namespace cat {

enum class Command : uint8_t { Feed, Play, Call, Sleep, Wake };

struct Personality {
    float idleMin, idleMax;          // Sekunden bis zur nächsten eigenen Entscheidung
    float sleepMin, sleepMax;        // Schlafdauer (Sekunden)
    float energyUse;                 // Energieverbrauch pro Minute wach (0..1)
    float wanderOther;               // Lust auf andere Flächen (0..1)
    float runChance;                 // bei weiten Wegen rennen
    float playful;                   // Spieltrieb (0..1)
    float groomChance;
    float dangleChance;              // im Liegen Pfote über die Kante
    int reachUp, reachDown, reachSide;   // Sprungvermögen in Sprite-Pixeln (wird mit der Vergrößerung multipliziert)
    int halfLen;                     // halbe Körperlänge in Sprite-Pixeln
    int headAhead;                   // Kopf (beim Fressen) vor dem Nullpunkt, Sprite-Pixel
    int callSide;                    // beim Rufen: links (-1) oder rechts (+1) vom Mauszeiger
};
Personality personalityFatos();
Personality personalityKosta();

// Napf oder Spielzeug
struct Item {
    bool active = false;
    int kind = 0;                    // 0 Napf, 1 Wollknäuel
    uint64_t platform = 0;
    double x = 0, y = 0;             // Boden-Mitte (Bildschirm-Pixel)
    double vx = 0, vy = 0;
    bool airborne = false;
    double food = 0;                 // Napf: Füllung 0..1
    double since = 0;
    float spin = 0;                  // Knäuel: Drehung (für das Bild)
};
// Bewegung eines Gegenstands (rollen, fallen, liegen bleiben). Gibt true zurück, wenn er sich bewegt hat.
bool stepItem(Item& it, const World& w, double dt);

struct Senses {
    double now = 0;                  // Sekunden
    const World* world = nullptr;
    bool cursorValid = false;
    int cursorX = 0, cursorY = 0;
    Item* bowl = nullptr;
    Item* toy = nullptr;
    bool otherPresent = false;       // die andere Katze
    double otherX = 0, otherY = 0;
    uint64_t otherPlat = 0;
    bool otherSleeping = false;
    int otherBowlSide = 0;           // -1/+1: Seite am Napf, die die andere schon belegt
};

// Was gezeichnet wird
struct View {
    Act act = Act::Stand;
    int frame = 0;
    float param = 0.f;
    Overlay ov;
    bool transition = false;
    Posture from = Posture::Stand, to = Posture::Stand;
    double x = 0, y = 0;             // Bildschirm-Pixel des Pose-Nullpunkts (beim Tragen: Haltepunkt)
    bool facingLeft = false;
    uint64_t platform = 0;           // 0: in der Luft oder getragen
    bool topmost = false;            // in der Luft/getragen: über allen Fenstern
};

class CatBrain {
public:
    CatBrain(const Personality& p, uint32_t seed);
    void place(const World& w, const Platform& plat, double x, Posture posture = Posture::Sit);

    // Einen Schritt weiterrechnen. Rückgabe: Sekunden bis zum nächsten Aufruf.
    double tick(const Senses& s);
    void command(Command c, const Senses& s);

    // Maus
    bool grab(int cx, int cy, const Senses& s);
    void drag(int cx, int cy, const Senses& s);
    void release(const Senses& s);
    void pet(const Senses& s);

    const View& view() const { return view_; }
    bool sleeping() const;
    bool busyWithItem() const;
    int bowlSide() const { return bowlSide_; }
    double x() const { return x_; }
    double y() const { return y_; }
    uint64_t platform() const { return plat_; }
    // Schlag auf das Spielzeug (Geschwindigkeit in px/s); wird einmal abgeholt
    bool takeBat(double& vx, double& vy);

private:
    enum class Mode : uint8_t { Idle, Trans, Walk, Crouch, Air, Land, Carried, Eat, Play, Petted, Flourish };
    enum class Goal : uint8_t { None, Wander, Bowl, Toy, Call, Other, SleepSpot };
    enum class PlayStep : uint8_t { Approach, Wiggle, Pounce, Bat };

    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }
    bool chance(float p) { return rnd(0.f, 1.f) < p; }
    int scale(const Senses& s) const;

    void followPlatform(const Senses& s);
    bool onPlatform(const Senses& s, const Platform*& p) const;
    void setPlatform(const Platform& p);
    void startFall(double vx, double vy);
    void startJump(const Senses& s, const Platform& to, int landX);
    void land(const Senses& s, const Platform& p);
    void goPosture(Posture target, Mode after);
    void beginTransition(Posture a, Posture b);
    void walkTo(double tx, bool run);
    void pursue(const Senses& s);
    void arrive(const Senses& s);
    void decide(const Senses& s);
    void wanderTarget(const Senses& s);
    void startIdle(const Senses& s, Posture p);
    void updateIdleExtras(const Senses& s);
    void lookAtCursor(const Senses& s);
    void fillView(const Senses& s);
    double nextDelay(const Senses& s) const;
    bool itemTarget(const Senses& s, const Item* it, uint64_t& plat, int& x);

    Personality P_;
    std::mt19937 rng_;
    View view_;

    // Lage
    double x_ = 0, y_ = 0;
    bool left_ = false;
    uint64_t plat_ = 0;
    int platX0_ = 0, platW_ = 0, platY_ = 0;

    // Zustand
    Mode mode_ = Mode::Idle;
    Posture posture_ = Posture::Sit;
    bool dangle_ = false;
    float dangleAmt_ = 0.f;
    Act act_ = Act::Sit;
    int frame_ = 0;
    double lastTick_ = -1;
    float energy_ = 0.8f;

    // Übergang
    Posture tFrom_ = Posture::Stand, tTo_ = Posture::Stand, tFinal_ = Posture::Stand;
    int tFrame_ = 0;
    Mode afterTrans_ = Mode::Idle;

    // Gehen
    double targetX_ = 0;
    bool running_ = false;
    bool jumpAfterWalk_ = false;
    uint64_t jumpTo_ = 0;
    int jumpLandX_ = 0;
    int turnFrames_ = 0;

    // Ziel
    Goal goal_ = Goal::None;
    uint64_t goalPlat_ = 0;
    int goalX_ = 0;
    double goalUntil_ = 0;
    int replans_ = 0;

    // Flug
    bool falling_ = false;
    double vx_ = 0, vy_ = 0, airT_ = 0, airDur_ = 0;
    double ax0_ = 0, ay0_ = 0, ax1_ = 0, ay1_ = 0, avy0_ = 0;
    uint64_t airTo_ = 0;
    float leapPitch_ = 0.f;
    int crouchFrames_ = 3;

    // Ruhe-Details
    double nextDecision_ = 0, nextBlink_ = 0, nextTail_ = 0, nextEar_ = 0, wakeAt_ = 0, lookUntil_ = 0, nextLookAround_ = 0;
    int blinkStep_ = -1, earStep_ = -1;
    double tailUntil_ = 0;
    float tailPhase_ = 0.f;
    float lookYaw_ = 0.f, lookPitch_ = 0.f, idleYaw_ = 0.f;
    float breath_ = 0.f;
    double nextBreath_ = 0;
    bool justWoke_ = false;
    Act flourish_ = Act::Groom;

    // Napf / Spiel / Streicheln
    double eatUntil_ = 0, pettedUntil_ = 0, playUntil_ = 0, stepUntil_ = 0;
    PlayStep playStep_ = PlayStep::Approach;
    int bowlSide_ = 0;
    bool batPending_ = false;
    double batVx_ = 0, batVy_ = 0;
    bool batDone_ = false;
    bool pounced_ = false;

    int depth_ = 0;
    int pendingCmd_ = -1;            // Befehl, der nach der Landung ausgeführt wird

    // Tragen
    double carryX_ = 0, carryY_ = 0, carryVx_ = 0, carryVy_ = 0, lastDrag_ = 0;
    float swing_ = 0.f;
};

}  // namespace cat
