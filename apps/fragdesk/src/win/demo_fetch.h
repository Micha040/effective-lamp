// FragDesk – „Demo von Valve laden“: die Demo eines vom eigenen Server geholten Matches selbst bei Valve holen
// (http://replayNNN.valve.net/730/…_….dem.bz2 – die Adresse kennt der Server, solange Valve die Demo aufhebt, etwa einen
// Monat). Ein eigener Thread mit niedriger Priorität lädt nach demosDir()\<Name>.part (ein Abbruch schadet nicht, es geht
// an der Stelle weiter), erst vollständig und geprüft heißt die Datei richtig. Danach gehört die Demo zum Match
// (Library::linkDemo): Ansehen und Aufnehmen in CS2 und Neu auswerten gehen wie bei jeder Demo auf dem PC – sie bleibt
// gepackt und wird wie jede .dem.bz2 erst fürs Ansehen entpackt.
//
// Rücksicht aufs Spiel: Pause, solange CS2 läuft (nur der Prozessname per Toolhelp), danach geht es von selbst weiter.
// Grenzen: höchstens 1 GB, 30 Minuten (ohne Pausen), 60 s ohne Daten. Immer nur eine Demo zur Zeit; der Stand kommt als
// WM_APP_DEMOFETCH zurück (höchstens alle 0,4 s).
#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "../core/model.h"

namespace fdw {

class App;
struct PulledInfo;

struct DemoFetchState {
    enum State { Idle, Running, Paused, Failed } state = Idle;
    std::string checksum;           // Match, dessen Demo lädt (bzw. zuletzt nicht ging)
    int64_t done = 0, total = -1;   // Bytes (total −1 = unbekannt)
    std::wstring error;             // Failed: deutscher Grund
};

class DemoFetch {
public:
    explicit DemoFetch(App& app);
    ~DemoFetch();

    void start(const std::string& checksum);   // geht es nicht, sagt FragDesk warum (Hinweis)
    void cancel();                             // „Abbrechen“: aufhören, angefangene Datei weg
    void stop();                               // beim Beenden
    const DemoFetchState& state() const;
    bool busy() const;                         // lädt oder wartet (Pause)

    LRESULT onMessage(WPARAM wp, LPARAM lp);   // WM_APP_DEMOFETCH (UI-Thread)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Bis wann hebt Valve die Demo etwa auf? Vom Server (Spielzeit + 30 Tage), sonst aus dem Datum des Matches; 0 = unbekannt
int64_t demoUntil(const PulledInfo& p, const fd::Match& m);

}  // namespace fdw
