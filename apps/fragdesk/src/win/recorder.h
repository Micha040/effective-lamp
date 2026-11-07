// FragDesk – Momente in CS2 ansehen und als MP4 aufnehmen.
//
// Ablauf ohne Eingriff ins Spiel: CS2 wird mit -netconport gestartet (offizielle Konsolen-Schnittstelle des
// Spiels) und spielt die Demo ab; FragDesk springt per Konsolenbefehl zu den Abschnitten, schaltet auf den
// Spieler und nimmt das Fenster wie OBS von außen auf (Bild: Desktop Duplication, Ton: Prozess-Loopback,
// Kodierung: Media Foundation H.264/AAC). Es wird keine Datei des Spiels verändert und nichts ins Spiel geladen.
//
// Für Aufnahmen startet CS2 mit eigenem Einstellungsordner (Umgebungsvariable USRLOCALCSGO, wie bei CS Demo
// Manager): Fenstergröße, ausgeblendetes HUD usw. landen nie in deinen echten CS2-Einstellungen.
#pragma once
#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings.h"
#include "steam.h"

namespace fdw {

struct RecordJob {
    std::wstring demo;        // Demo-Datei (gepackt oder nicht)
    std::wstring out;         // Ziel-MP4
    std::wstring title;       // für Meldungen
    std::string match, highlight, map;
    int slot = 0;             // Spieler für spec_player
    std::string steamId;
    double tickrate = 64;
    std::vector<std::pair<int, int>> segments;   // Tick-Bereiche
};

struct RecordEvent {
    enum Kind { Status, ClipDone, Finished, Failed } kind = Status;
    std::wstring text;
    RecordJob job;         // bei ClipDone
    double seconds = 0;    // Länge des Clips
};

class Recorder {
public:
    explicit Recorder(HWND notify);
    ~Recorder();

    bool busy() const { return busy_; }
    std::wstring status() const;
    void cancel();

    // CS2 öffnen und zum Moment springen (ohne Aufnahme, mit deinen normalen CS2-Einstellungen)
    bool watch(const SteamInfo& steam, const RecordJob& job, std::wstring& err);
    // Momente nacheinander aufnehmen
    bool record(const SteamInfo& steam, const RecordSettings& rs, std::vector<RecordJob> jobs, std::wstring& err);

private:
    bool start(const SteamInfo& steam, const RecordSettings& rs, std::vector<RecordJob> jobs, bool recordVideo, std::wstring& err);
    void run(SteamInfo steam, RecordSettings rs, std::vector<RecordJob> jobs, bool recordVideo);
    void post(RecordEvent* e);
    void setStatus(const std::wstring& s);

    HWND hwnd_;
    std::thread thread_;
    std::atomic<bool> busy_{false}, cancel_{false};
    mutable std::mutex mu_;
    std::wstring status_;
};

// Prüft, ob ein Programm läuft (Prozessname per Toolhelp – kein anderes Programm wird geöffnet)
bool isProcessRunning(const wchar_t* exeName);

// Ordner mit den eigenen CS2-Einstellungen für Aufnahmen (%LOCALAPPDATA%\FragDesk\cs2)
std::wstring cs2SandboxDir();

// Umgebung dieses Prozesses mit name=value (für CreateProcess mit CREATE_UNICODE_ENVIRONMENT)
std::vector<wchar_t> environmentWith(const std::wstring& name, const std::wstring& value);

}  // namespace fdw
