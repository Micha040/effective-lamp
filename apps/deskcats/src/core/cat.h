// DeskCats – Aussehen (Look), Haltung (Pose) und Umrechnung in Shader-Daten.
// Koordinaten der Katze: Zentimeter, y nach oben, Boden bei y = 0, Blickrichtung +x (gespiegelt, wenn sie nach links schaut).
#pragma once
#include "catdraw.h"

#include <string>

namespace cat {

using hlsl::float2;
using hlsl::float3;

struct Look {
    std::string name;
    float scale = 1.f;                 // Gesamtgröße
    // Körper (cm)
    float bodyLen = 24.f;              // Schulter- bis Hüftgelenk
    float shoulderH = 18.f, hipH = 18.5f;
    float chestR = 8.6f, hipR = 7.8f, bellyR = 6.6f, waistR = 7.6f;
    float neckR0 = 6.2f, neckR1 = 4.6f;
    float headR = 6.0f;
    float muzzleR = 0.44f, muzzleFwd = 0.58f, muzzleDown = 0.32f;     // relativ zum Kopfradius
    float cheekR = 0.5f, cheekFwd = 0.22f, cheekDown = 0.36f, cheekSide = 0.5f;
    float upperArmR = 3.0f, foreArmR = 2.2f, pawR = 1.65f;
    float thighR = 5.0f, shinR = 2.5f, metaR = 1.85f;
    float tailLen = 27.f, tailR0 = 2.5f, tailR1 = 1.5f;
    // Gesicht
    int earType = 0;                   // 0 aufrecht, 1 Faltohr
    float earSize = 1.f;
    float eyeAX = 0.27f, eyeAY = 0.24f, eyeAz = 0.62f, eyeEl = 0.13f;
    float noseSize = 0.85f;
    // Fell
    int pattern = 0;                   // 0 rot getigert, 1 Smoking
    float stripeFreq = 4.2f, stripeContrast = 0.55f, furDetail = 0.08f;
    float rim = 0.25f;
    unsigned pal[16] = {};             // sRGB 0xRRGGBB
};

Look lookFatos();
Look lookKosta();

struct Pose {
    float2 spine[4];                   // Hüfte .. Schulter (Mittellinie des Rumpfs)
    float spineR[4];
    float2 belly0, belly1;
    float bellyR = 0.f;
    float2 neck0, neck1;
    float neckR0 = 0.f, neckR1 = 0.f;
    float2 head;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;   // Kopfdrehung (yaw > 0: zu uns)
    float eyeOpen = 1.f, pupil = 0.35f, squint = 0.f, mouth = 0.f;
    float earBack = 0.f;               // Ohren anlegen 0..1
    float thighEnd = 1.15f;            // Oberschenkel-Ende relativ zum Unterschenkel (Sitzen: dicker)
    // Beine: Schulter/Hüfte, Gelenke, Pfote (Ferse, Zehen)
    float2 fn[4], ff[4];               // vorne nah / fern: Schulter, Ellbogen, Handgelenk, Zehen
    float2 hn[5], hf[5];               // hinten nah / fern: Hüfte, Knie, Sprunggelenk, Ballen, Zehen
    float2 tail[12];
    int tailN = 0;
    int tailFrontFrom = 99;            // ab diesem Glied liegt der Schwanz vor dem Körper
    float2 shadowC;
    float shadowRX = 0.f, shadowRY = 0.f, shadowA = 0.f;
    bool frontOverHead = false;        // nahes Vorderbein vor dem Kopf (Putzen, Pfote vors Gesicht)
    float2 anchor;                     // Haltepunkt (Boden-Mitte; beim Tragen: Nackenfell)
};

// Fertige Haltungen (Standbilder / Ausgangspunkte für Animationen)
Pose poseStand(const Look& lk);
Pose poseSit(const Look& lk);
Pose poseLoaf(const Look& lk, float pawDangle);
Pose poseSleep(const Look& lk, float breath);

// Schwanzkette aus Ansatzpunkt, Startrichtung und Krümmung (rad/cm, negativ = nach oben einrollen bei Blick nach rechts)
void buildTail(Pose& p, float2 base, float2 dir, float len, float curl0, float curl1, int n);
// Schwanz als Kurve durch Kontrollpunkte (gleichmäßig verteilte Glieder)
void buildTailSpline(Pose& p, const float2* ctrl, int count, int n);

// Rahmen um die Katze (cm, ohne Spiegelung)
struct Box {
    float x0, y0, x1, y1;
};
Box poseBounds(const Look& lk, const Pose& p);

// Shader-Daten füllen. originX/Y = Katzen-Koordinate (cm) der linken oberen Fensterecke, pxPerCm = Maßstab.
void buildCB(const Look& lk, const Pose& p, bool facingLeft, float originX, float originY, float pxPerCm, float time,
             float alpha, CatCB& cb);

}  // namespace cat
