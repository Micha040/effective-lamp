// efficient lamp – Dateien laden (WinHTTP, nur HTTPS; im Testbetrieb auch http:// und lokale Pfade)
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace lamp {

// progress(geladen, gesamt) – gesamt = 0, wenn unbekannt. Rückgabe false = abbrechen.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

// Abbrechen von einem anderen Thread aus – auch mitten in einem hängenden Lesevorgang
class DownloadCancel {
public:
    DownloadCancel() { InitializeCriticalSection(&cs_); }
    ~DownloadCancel() { DeleteCriticalSection(&cs_); }
    void cancel();
    void reset() { cancelled_ = false; }
    bool cancelled() const { return cancelled_; }
    bool attach(void* request);   // false: schon abgebrochen
    bool detach();                // true: der Aufrufer muss den Request selbst schließen

private:
    CRITICAL_SECTION cs_;
    void* request_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

// Testbetrieb (--catalog): auch http:// und lokale Dateien erlauben. Sonst nur https://.
void allowLocalSources(bool allow);

// Lädt url komplett in den Speicher (höchstens maxBytes). Bei Fehler: false und eine verständliche Meldung.
bool download(const std::wstring& url, std::string& out, std::wstring& error, const Progress& progress = Progress(),
              size_t maxBytes = 64u << 20, DownloadCancel* cancel = nullptr);

}  // namespace lamp
