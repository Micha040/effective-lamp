// FragDesk – Radarbilder für die 2D-Ansicht: Kartenwerte und Umrechnung von Spielkoordinaten.
// Die Bilder selbst stecken als Ressourcen in FragDesk.exe (res/radars.rc, erzeugt von tools/make_radars.py).
#pragma once

#include <string>

namespace fd {

struct RadarMap {
    const char* name;
    float posX, posY;   // Spielkoordinaten der linken oberen Bildecke
    float span;         // Spielkoordinaten über die ganze Bildbreite (= Bildhöhe)
    float lowerZ;       // darunter gilt die untere Ebene
    bool lower;         // eigenes Bild für die untere Ebene
};

constexpr int kRadarResBase = 300;   // Ressource: kRadarResBase + 2 * Index (+ 1 für die untere Ebene)

// Kartenwerte zum Kartennamen der Demo ("de_mirage", auch "workshop/123/de_mirage"); nullptr, wenn unbekannt
const RadarMap* radarMap(const std::string& map, int* index = nullptr);

// Spielkoordinaten -> 0..1 im Radarbild (x nach rechts, y nach unten)
inline void radarUV(const RadarMap& m, float x, float y, float& u, float& v) {
    u = (x - m.posX) / m.span;
    v = (m.posY - y) / m.span;
}

// liegt die Höhe z auf der unteren Ebene?
inline bool radarLower(const RadarMap& m, float z) { return m.lower && z < m.lowerZ; }

int radarMapCount();

}  // namespace fd
