// efficient lamp – Dateien laden (WinHTTP, HTTPS; für Tests auch http:// und lokale Pfade)
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace lamp {

// progress(geladen, gesamt) – gesamt = 0, wenn unbekannt. Rückgabe false = abbrechen.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

// Lädt url komplett in den Speicher (höchstens maxBytes). Bei Fehler: false und eine verständliche Meldung.
bool download(const std::wstring& url, std::string& out, std::wstring& error, const Progress& progress = Progress(),
              size_t maxBytes = 64u << 20);

}  // namespace lamp
