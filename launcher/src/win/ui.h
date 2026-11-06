// efficient lamp – das Launcher-Fenster: eigene Zeichnung mit GDI+, hell/dunkel nach Windows-Einstellung,
// Karten für jedes Programm mit Installieren / Aktualisieren / Öffnen und einem Menü für den Rest.
#pragma once
#include <windows.h>

#include <string>

namespace lamp {

struct Options {
    std::wstring catalogUrl;   // leer = GitHub-Release
    bool portable = false;     // nicht selbst installieren
    bool welcome = false;      // gerade installiert
    bool updated = false;      // gerade aktualisiert
    int theme = -1;            // -1 wie Windows, 0 hell, 1 dunkel
    bool demo = false;         // Beispielzustände (für Bildschirmfotos)
    bool installedCopy = false;   // läuft die eingerichtete Kopie? (einmal beim Start ermittelt)
    std::wstring relaunchArgs;    // Schalter für den Neustart nach einem Selbst-Update
    std::wstring renderPng;    // nur in eine PNG-Datei zeichnen und beenden (Tests, README)
    int renderWidth = 760, renderHeight = 600, renderScale = 100;
};

int runLauncher(HINSTANCE inst, const Options& opt);

}  // namespace lamp
