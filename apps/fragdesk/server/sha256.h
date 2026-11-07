// fragdesk-server – SHA-256 (FIPS 180-4), eigene kleine Umsetzung ohne Fremdbibliothek. Damit liegen Schlüssel und
// Einladungscodes nie im Klartext auf der Platte.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fdsrv {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    std::string hex();   // beendet die Berechnung; 64 Kleinbuchstaben

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

std::string sha256Hex(const std::string& data);

}  // namespace fdsrv
