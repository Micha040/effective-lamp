// FragDesk – Bibliothek: ausgewertete Matches, gefundene Demos, Auswertung im Hintergrund
#pragma once
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/model.h"

namespace fdw {

// Ein Match, das FragDesk vom eigenen Server geholt hat (matches\<Prüfsumme>.src, daneben .json, .heat, .replay wie
// sonst): woher die Demo kam und – nach „Demo von Valve laden“ – wo sie auf diesem PC liegt. Solche Matches lädt der
// Abgleich nie hoch (der Server hat sie ja); wertet FragDesk dieselbe Demo hier selbst aus, wird es ein eigenes Match.
struct PulledInfo {
    bool fetched = false;              // der Server hat es selbst bei Valve geholt
    std::string url, shareCode;        // Adresse der Demo bei Valve, Freigabecode des Matches (leer = unbekannt)
    int64_t matchtime = 0, until = 0;  // Spielzeit; Valve hebt die Demo etwa bis hierhin auf (0 = unbekannt)
    std::wstring demo;                 // von Valve geladene Demo auf diesem PC (leer = keine)
    bool gone = false;                 // Valve hat die Demo nicht mehr (beim Laden 404)
};

struct PulledMatch {   // vom Abgleich frisch geholt (ohne Details), für Library::addPulled
    std::unique_ptr<fd::Match> match;
    PulledInfo info;
    bool fresh = true;   // neu auf diesem PC (sonst: neuere Fassung eines schon geholten Matches)
};

struct DemoFile {
    enum class State { New, Queued, Running, Failed };
    std::wstring path;
    int64_t size = 0, mtime = 0;
    std::wstring origin;   // CS2, Downloads, Ordner, Hinzugefügt
    State state = State::New;
    std::wstring step, error;
};

struct AnalysisEvent {
    enum Kind { Step, Done, Failed } kind = Step;
    std::wstring path, text;
    std::unique_ptr<fd::Match> match;   // bei Done (ohne Details)
};

struct LibraryLoad {   // Ergebnis des Ladens im Hintergrund
    std::vector<std::unique_ptr<fd::Match>> matches;
    std::map<std::string, PulledInfo> pulled;   // vom Server geholt (.src)
    int failed = 0;
};

struct ScanResult {
    std::vector<DemoFile> files;
};

class Library {
public:
    ~Library();
    void attach(HWND notify) { hwnd_ = notify; }
    void setFolders(const std::vector<std::pair<std::wstring, std::wstring>>& folders);   // (Ordner, Herkunft)
    void setIgnored(const std::vector<std::string>& ignored) { ignored_ = ignored; }

    // Laden (beim Start). Ohne Fenster (Bildmodus) synchron.
    void loadAsync();
    void loadNow();
    void onLoaded(LibraryLoad* r);
    bool loaded() const { return loaded_; }

    // Demos suchen
    void scanAsync();
    void onScanned(ScanResult* r);
    void addFiles(const std::vector<std::wstring>& files);
    bool isKnown(const std::wstring& path, int64_t size) const;
    static std::string ignoreKey(const DemoFile& d);

    // Auswerten
    void analyze(const std::wstring& path);
    void analyzeAll(bool retryFailed = true);   // automatisch: nur neue Demos, sonst auch fehlgeschlagene noch einmal
    // Beim Auswerten gleich die 2D-Daten (.replay) mit erzeugen – im selben Lauf von fdanalyze, ohne die Demo noch
    // einmal zu lesen. An, solange FragDesk 2D-Daten auf den eigenen Server lädt.
    void setWithReplay(bool on);
    bool busy() const;
    int queued() const;
    void onEvent(AnalysisEvent* e);   // im UI-Thread

    // Matches (neueste zuerst, ohne Details)
    std::vector<std::unique_ptr<fd::Match>> matches;
    int generation = 0;   // zählt jede Änderung an matches (für Zwischenspeicher wie die Heatmap)
    std::vector<DemoFile> demos;   // noch nicht ausgewertet
    std::wstring lastError;        // letzte Fehlermeldung einer Auswertung

    const fd::Match* find(const std::string& checksum) const;
    bool loadDetail(const std::string& checksum, fd::Match& out, std::wstring& err) const;
    bool remove(const std::string& checksum);   // samt Heatmap, 2D-Daten und Markierung „vom Server“
    static std::wstring matchFile(const std::string& checksum);

    // Vom eigenen Server geholte Matches (Abgleich, siehe server_sync.h)
    const PulledInfo* pulled(const std::string& checksum) const;
    int pulledCount() const { return int(pulled_.size()); }
    std::vector<std::string> pulledChecksums() const;
    void addPulled(std::vector<PulledMatch>& batch);   // im UI-Thread: neu einsortieren (einmal sortieren je Stapel)
    // „Demo von Valve laden“ fertig: Demo gehört zu diesem Match (Ansehen, Aufnehmen, Neu auswerten gehen damit)
    void linkDemo(const std::string& checksum, const std::wstring& demo);
    void markDemoGone(const std::string& checksum);   // Valve hat die Demo nicht mehr
    static std::wstring pulledFile(const std::string& checksum);   // matches\<Prüfsumme>.src
    static bool writePulled(const std::string& checksum, const PulledInfo& info);
    static bool readPulled(const std::wstring& path, PulledInfo& info);
    // Name der von Valve geladenen Demo in demosDir(), wie beim Helfer auf dem Server:
    // http://replay183.valve.net/730/0037…_1418282143.dem.bz2 → match730_0037…_1418282143_183.dem.bz2
    static std::wstring downloadName(const std::string& url, const std::string& checksum);

    // Ordner überwachen (Änderungen kommen als WM_APP_FOLDER)
    void startWatching();
    void stopWatching();

private:
    void worker();
    void sortMatches();
    void insertMatch(std::unique_ptr<fd::Match> m);
    void post(UINT msg, void* p);
    void applyLink(fd::Match& m) const;   // geladene Demo eines geholten Matches eintragen

    HWND hwnd_ = nullptr;
    bool loaded_ = false;
    std::map<std::string, PulledInfo> pulled_;
    std::vector<std::pair<std::wstring, std::wstring>> folders_;
    std::vector<std::string> ignored_;

    // Auftragswarteschlange (ein Thread, ein fdanalyze-Prozess zur Zeit)
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::wstring> queue_;
    std::wstring running_;
    bool quit_ = false;
    HANDLE child_ = nullptr;   // laufendes fdanalyze (zum Beenden beim Schließen)
    bool withReplay_ = false;   // mit mu_

    std::thread watcher_;
    HANDLE watchStop_ = nullptr;
};

std::wstring analysisErrorText(const std::string& code);

}  // namespace fdw
