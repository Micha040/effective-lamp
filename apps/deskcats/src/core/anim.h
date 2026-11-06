// DeskCats – Bewegungen: aus Grundhaltungen, Gangzyklen und Übergängen entsteht für jedes Einzelbild eine Haltung (Pose).
// Alles in Katzen-Zentimetern, Blick nach +x. Gezeichnet wird mit 12 Bildern pro Sekunde (Pixel-Art-Takt).
#pragma once
#include "cat.h"

#include <cstdint>

namespace cat {

constexpr float kFps = 12.f;

// Was die Katze gerade tut (jede Handlung hat eigene Einzelbilder)
enum class Act : uint8_t {
    Stand,        // stehen (atmen, Schwanz pendelt)
    Walk,         // gehen (Zyklus)
    Run,          // rennen (Zyklus)
    Sit,          // sitzen
    Loaf,         // liegen, Pfoten vorne
    LoafDangle,   // liegen, Pfote hängt über die Kante
    Sleep,        // eingerollt schlafen
    Groom,        // sitzend Pfote lecken und übers Gesicht wischen
    Stretch,      // Strecken (vorne runter, Po hoch)
    Crouch,       // ducken vor dem Sprung
    Leap,         // im Sprung (Flugphase)
    Fall,         // fallen
    Land,         // landen
    Carried,      // hängt am Mauszeiger
    Eat,          // fressen
    Wiggle,       // Po wackeln vor dem Anspringen
    Pounce,       // Anspringen
    Bat,          // mit der Pfote schlagen
    Petted,       // gestreichelt werden
    Yawn,         // gähnen
    Count
};

// Übergang zwischen zwei Grundhaltungen
enum class Posture : uint8_t { Stand, Sit, Loaf, Sleep };

// Zusätzliche Feinheiten, die über jede Handlung gelegt werden
struct Overlay {
    float lookYaw = 0.f;      // Kopf zum Betrachter drehen (-1 .. 1, 0 = wie die Handlung)
    float lookPitch = 0.f;    // Kopf heben/senken (-1 .. 1)
    float blink = 0.f;        // 0 offen .. 1 zu
    float earTwitch = 0.f;    // 0 .. 1
    float tailSway = 0.f;     // Phase des Schwanzpendelns 0 .. 1
    float tailAmp = 1.f;      // Stärke des Pendelns
    float breath = 0.f;       // Atemphase 0 .. 1
};

// Haltung für Bild Nummer `frame` der Handlung (Zyklen wiederholen sich, Einmal-Handlungen bleiben auf dem letzten Bild).
// `param` je nach Handlung: Fall/Leap = Fortschritt 0..1, Carried = Pendeln -1..1, sonst ungenutzt.
Pose actPose(const Look& lk, Act act, int frame, float param, const Overlay& ov);

// Anzahl Einzelbilder einer Handlung (bei Zyklen: Länge eines Zyklus)
int actFrames(Act act);
bool actLoops(Act act);
// Weg pro Bild in Sprite-Pixeln (bei 1,25 px/cm), damit die Pfoten beim Gehen nicht rutschen
int actStepPx(Act act);

// Übergang zwischen Grundhaltungen: Bild `frame` von `transitionFrames(a,b)`
int transitionFrames(Posture a, Posture b);
Pose transitionPose(const Look& lk, Posture a, Posture b, int frame, const Overlay& ov);

// Zwei Haltungen mischen (Beine werden mit gleich langen Knochen nachgeführt)
Pose blendPose(const Look& lk, const Pose& a, const Pose& b, float t);

// Prüfsumme der Haltung (gleich aussehende Bilder -> gleicher Schlüssel, für den Bild-Zwischenspeicher)
uint64_t poseKey(const Pose& p);

}  // namespace cat
