// FragDesk – Handy-App über den eigenen Server: verbinden (Einladung), im Hintergrund abgleichen, Handy koppeln,
// Freunde einladen. Spezifikation: FragDesk-Server v1, Abschnitt „FragDesk (Windows)“.
//
// Abgleich in einem eigenen Thread mit niedriger Priorität (auch für die Festplatte): /sync/state holen, zuerst löschen,
// was in FragDesk gelöscht wurde (macht Platz), dann fehlende Matches (.json, .heat) und Videos in Stücken à 4 MB
// (fortsetzbar), Vorschaubild und „done“. Angestoßen beim Start, nach jeder Auswertung, Aufnahme oder Löschung und alle
// 15 Minuten. Pause, solange CS2 läuft (nur der Prozessname per Toolhelp) oder FragDesk aufnimmt; bei Fehlern neuer
// Versuch mit wachsender Wartezeit. Der UI-Thread gibt dem Thread einen Schnappschuss mit; Ergebnisse kommen als
// WM_APP_SYNC zurück. Die Aktionen der Einstellungen (verbinden, koppeln, einladen, abmelden) laufen in kurzen eigenen
// Threads, damit sie nicht hinter einem großen Video warten.
#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace fdw {

class App;

// Stand des Abgleichs für die Anzeige
struct SyncStatus {
    enum State { Idle, Running, PausedCs2, PausedRecording, Offline, Full, AuthFailed };
    State state = Idle;
    std::wstring error;        // Offline, Full: Text (vom Server oder Netzwerkfehler)
    int64_t retryAt = 0;       // Offline: nächster Versuch (Sekunden seit 1970)
    bool counted = false;      // Zahlen vom Server schon da
    int matchesDone = 0, matchesTotal = 0, clipsDone = 0, clipsTotal = 0;
    std::wstring current;      // Running: was gerade hochgeladen wird
    int64_t used = -1, limit = 0;   // Speicher in Bytes (−1 = unbekannt; limit 0 = unbegrenzt)
};

class ServerSync {
public:
    explicit ServerSync(App& app);
    ~ServerSync();

    // Einstellungen übernehmen (Modus, Konto): Abgleich an oder aus, Zeitgeber für alle 15 Minuten (UI-Thread)
    void apply();
    bool active() const;   // Modus „Eigener Server“ und verbunden
    // Abgleich anstoßen: Schnappschuss der Matches und Videos jetzt, Abgleich nach delayMs (mehrere Anstöße werden einer).
    // manual: sofort, auch während einer Wartezeit nach Fehlern
    void request(int delayMs = 3000, bool manual = false);
    void setRecording(bool on);   // FragDesk nimmt auf: Pause
    // In FragDesk gelöscht: beim nächsten Abgleich auch auf dem Server löschen (merkt es sich bis dahin in settings.json).
    // Nur das wird auf dem Server gelöscht – was in FragDesk bloß fehlt (neuer PC), bleibt dort.
    void matchDeleted(const std::string& checksum);
    void clipDeleted(const std::wstring& path);
    void stop();                  // beim Beenden

    // Befehle aus den Einstellungen
    void connectFromClipboard();   // „Einladung einfügen“
    void disconnect();             // „Trennen“: abmelden, Adresse und Schlüssel vergessen
    void startPairing();           // „Handy verbinden“: Code vom Server
    void stopPairing();
    void invite();                 // „Freund einladen“ (Admin): Link in die Zwischenablage

    // Anzeige
    const SyncStatus& status() const;
    bool connecting() const;
    bool pairingBusy() const;
    bool inviteBusy() const;
    std::string pairingCode() const;   // leer = keine Kopplung offen
    int64_t pairingUntil() const;      // Sekunden seit 1970
    std::string pairingUrl() const;    // Adresse für den QR-Code (mit ?pair=)
    std::string pairingBase() const;   // Adresse der Handy-App ohne Code
    std::wstring serverLabel() const;  // Server für die Anzeige (ohne https://)

    LRESULT onMessage(WPARAM wp, LPARAM lp);   // WM_APP_SYNC (UI-Thread)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    void forget(const std::string& key);
};

// Einladungslink „https://host/#invite=<Code>“ zerlegen: Server-Adresse (geprüft, mit "/" am Ende) und Code
bool parseInvite(const std::wstring& text, std::string& server, std::string& code);

}  // namespace fdw
