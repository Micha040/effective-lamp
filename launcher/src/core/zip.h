// efficient lamp – ZIP-Archive lesen und entpacken (nur "stored" und "deflate", ohne ZIP64/Verschlüsselung).
// Die Pakete sind klein (< 2 MB), deshalb arbeitet alles im Speicher. Eigenes Inflate nach RFC 1951,
// damit der Launcher weder tar.exe noch PowerShell zum Entpacken braucht.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zip {

struct Entry {
    std::string name;        // UTF-8, mit '/' als Trenner, z. B. "DeskCats/Setup.ps1"
    uint16_t method = 0;     // 0 = stored, 8 = deflate
    uint16_t flags = 0;
    uint32_t crc = 0;
    uint32_t compSize = 0, size = 0;
    uint32_t localOffset = 0;
    bool isDir = false;
};

bool list(const std::string& archive, std::vector<Entry>& out, std::string& err);
bool read(const std::string& archive, const Entry& e, std::string& out, std::string& err);   // prüft CRC-32

// Name sicher? (kein "..", kein absoluter Pfad, kein Laufwerk) – Schutz vor "Zip Slip"
bool safeName(const std::string& name);

uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);
bool inflateRaw(const uint8_t* src, size_t len, std::string& out, size_t maxOut, std::string& err);

}  // namespace zip
