// efficient lamp – Aufträge nacheinander abarbeiten: Paket laden, prüfen (SHA-256), entpacken, Setup ausführen.
// Läuft in einem eigenen Thread; Fortschritt kommt als WM_APP_JOB (JobEvent*) beim Fenster an.
#pragma once
#include <windows.h>

#include <atomic>
#include <deque>
#include <string>

#include "../core/catalog.h"
#include "net.h"

namespace lamp {

enum class JobKind { Install, Uninstall, OpenScript };

enum class JobPhase { Queued, Download, Extract, Setup, Finished };

struct JobEvent {
    std::string appId;
    JobKind kind = JobKind::Install;
    JobPhase phase = JobPhase::Queued;
    double progress = -1;   // 0..1, -1 = unbestimmt
    bool ok = true;         // nur bei Finished
    std::wstring message;   // Fehlertext (bei Finished und !ok)
};

class Jobs {
public:
    Jobs();
    ~Jobs();
    void start(HWND notify);
    void add(JobKind kind, const cat::App& app);
    bool pending(const std::string& appId) const;   // wartet oder läuft
    bool busy() const;
    bool setupRunning() const;                       // gerade ist ein Setup-Fenster offen
    bool cancel(const std::string& appId);           // aus der Warteschlange nehmen oder Download abbrechen
    bool stop(DWORD waitMs);                         // beim Beenden: Downloads abbrechen, Setups laufen weiter.
                                                     // false: der Thread läuft noch – dann das Objekt nicht freigeben

    static std::wstring packageRoot(const std::string& appId);

private:
    struct Job {
        JobKind kind;
        cat::App app;
    };
    static DWORD WINAPI threadMain(LPVOID self);
    void loop();
    void run(const Job& job);
    bool ensurePackage(const Job& job, std::wstring& dir, std::wstring& err);
    void post(const Job& job, JobPhase phase, double progress = -1, bool ok = true, const std::wstring& msg = std::wstring());

    HWND hwnd_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;
    mutable CRITICAL_SECTION cs_;
    std::deque<Job> queue_;
    std::string current_;
    DownloadCancel cancel_;                          // Abbruch des laufenden Downloads
    std::atomic<bool> quit_{false};
    std::atomic<bool> setupRunning_{false};          // nur unter cs_ setzen
};

}  // namespace lamp
