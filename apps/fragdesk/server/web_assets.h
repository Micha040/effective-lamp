// fragdesk-server – die eingebaute Handy-App (apps/fragdesk/web). Die Daten erzeugt tools/make_web.py --cpp beim
// Bauen (build/server/web_assets.cpp), damit der Server als eine einzige Datei läuft.
#pragma once

#include <cstddef>

namespace fdsrv {

struct WebFile {
    const char* path;   // ohne führenden Schrägstrich, z. B. "js/app.js"
    const unsigned char* data;
    size_t size;
};

extern const WebFile kWebFiles[];
extern const size_t kWebFileCount;

}  // namespace fdsrv
