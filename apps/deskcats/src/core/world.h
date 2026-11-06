// DeskCats – die Welt der Katzen: waagerechte Flächen (Fenster-Oberkanten, Taskleisten, Bildschirmboden) in
// Bildschirm-Pixeln (y nach unten). Wird vom Windows-Teil aus den echten Fenstern gebaut, in Tests von Hand.
#pragma once
#include <cstdint>
#include <vector>

namespace cat {

enum class PlatKind : uint8_t { Window, Taskbar, Floor };

struct Span {
    int x0, x1;
};

struct Platform {
    uint64_t id = 0;            // Fenster-Handle oder feste Kennung (Taskleiste/Boden je Monitor)
    PlatKind kind = PlatKind::Window;
    int x0 = 0, x1 = 0, y = 0;  // Oberkante
    int monitor = 0;
    int z = 0;                  // Stapelreihenfolge (0 = ganz vorne), Taskleiste/Boden: -1
    std::vector<Span> visible;  // Stücke, die nicht von Fenstern davor verdeckt sind
    int width() const { return x1 - x0; }
    bool visibleAt(int x) const;
    int visibleLength() const;
};

struct Monitor {
    int x0, y0, x1, y1;         // ganzer Monitor
    int scale;                  // Vergrößerung der Pixel-Art (ganzzahlig)
};

struct World {
    std::vector<Platform> plats;
    std::vector<Monitor> monitors;

    const Platform* find(uint64_t id) const;
    int monitorAt(int x, int y) const;   // -1 wenn außerhalb
    int scaleAt(int x, int y) const;
    // Höchste Fläche unterhalb (oder auf Höhe von) y bei x, auf die man fällt. nullptr = keine.
    const Platform* landingBelow(double x, double yFrom, double yTo, uint64_t exclude = 0) const;
    // Fläche, die am nächsten unter einem Punkt liegt (für "Rufen")
    const Platform* nearestBelow(int x, int y, int maxDy) const;
};

// Sprungvermögen in Bildschirm-Pixeln
struct Reach {
    int up, down, side;
};

// Nächster Schritt auf dem Weg von (from, x) zur Fläche `to`: entweder zu Fuß bis takeoffX (gleiche Fläche oder
// angrenzend) oder ein Sprung von takeoffX auf `next` bei landX. Liefert false, wenn `to` unerreichbar ist.
struct Hop {
    uint64_t next = 0;
    int takeoffX = 0, landX = 0;
    bool walk = false;          // angrenzende Fläche auf gleicher Höhe: einfach hinübergehen
    bool edge = false;          // am Bildschirmrand auf den Boden des Nachbarmonitors wechseln (Höhe egal)
};
// Wege über Bildschirmränder nur, wenn es anders nicht geht (Monitore mit sehr unterschiedlicher Unterkante)
bool planHop(const World& w, const Platform& from, int x, const Platform& to, int toX, const Reach& r, int catHalf, Hop& out);

// Böden (Taskleiste/Unterkante) zweier nebeneinander liegender Monitore: gemeinsame Kante bei edgeX
bool edgeLink(const World& w, const Platform& a, const Platform& b, int& edgeX);

// Kann von `a` aus `b` angesprungen werden? (Absprung-/Landepunkte möglichst nah an x)
bool jumpPoints(const Platform& a, const Platform& b, int x, const Reach& r, int catHalf, int& takeoff, int& land);

}  // namespace cat
