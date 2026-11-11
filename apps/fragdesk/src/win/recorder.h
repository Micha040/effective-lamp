// FragDesk – Momente in CS2 ansehen und als MP4 aufnehmen.
//
// Ablauf ohne Eingriff ins Spiel: CS2 wird mit -netconport gestartet (offizielle Konsolen-Schnittstelle des
// Spiels) und spielt die Demo ab. Unter Windows öffnet CS2 diese Konsole nur im Workshop-Tools-Modus (-tools,
// kostenloser DLC; ohne ihn schlägt socket() mit WSANOTINITIALISED fehl, siehe ValveSoftware/csgo-osx-linux#3603),
// deshalb startet FragDesk CS2 mit -tools -noassetbrowser. FragDesk springt per Konsolenbefehl zu den Abschnitten, schaltet auf den
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
    bool recordsVideo() const { return busy_ && video_; }   // nimmt gerade auf (nicht bloß Ansehen)
    std::wstring status() const;
    void cancel();
    // Du holst FragDesk während der Aufnahme zurück: CS2 nicht mehr obenauf halten (UI-Thread)
    void releaseTopmost();

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
    std::atomic<bool> video_{false};        // der laufende Auftrag nimmt Videos auf (sonst: nur Ansehen)
    std::atomic<bool> keepTopmost_{true};   // false, sobald du FragDesk zurückgeholt hast
    std::atomic<HWND> topmost_{nullptr};    // CS2-Fenster, das gerade obenauf gehalten wird
    mutable std::mutex mu_;
    std::wstring status_;
};

// Prüft, ob ein Programm läuft (Prozessname per Toolhelp – kein anderes Programm wird geöffnet)
bool isProcessRunning(const wchar_t* exeName);

// Performance-Modus (Einstellungen › Counter-Strike 2) für alle Threads: Abgleich, „Demo von Valve laden“ und Handy-App
// lesen ihn bei jeder Prüfung neu. Setzt der UI-Thread beim Start und beim Umschalten (App::applyCs2Mode).
void setCs2Mode(fd::Cs2Mode mode);
fd::Cs2Mode cs2Mode();

// FragDesk nimmt gerade Videos auf (mit eigenem CS2): Dann warten „Demo von Valve laden“ und Handy-App in jedem
// Performance-Modus (der Abgleich ohnehin), damit die Aufnahme nicht ruckelt. Setzt der UI-Thread
// (App::recordingChanged). Startet FragDesk CS2 nur zum Ansehen, zählt dieses CS2 wie deins.
void setRecordingActive(bool on);
bool recordingActive();

// Ordner mit den eigenen CS2-Einstellungen für Aufnahmen (%LOCALAPPDATA%\FragDesk\cs2)
std::wstring cs2SandboxDir();

// Umgebung dieses Prozesses mit name=value (für CreateProcess mit CREATE_UNICODE_ENVIRONMENT)
std::vector<wchar_t> environmentWith(const std::wstring& name, const std::wstring& value);

}  // namespace fdw
