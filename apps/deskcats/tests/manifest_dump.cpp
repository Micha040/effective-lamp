// Schreibt das Paket-Manifest (wie es --pack-menu verwendet) nach manifest.xml
#include "../src/win/pack.h"
#include <cstdio>
int wmain() {
    std::string m = app::buildManifest();
    FILE* f = fopen("manifest.xml", "wb");
    if (!f) return 1;
    fwrite(m.data(), 1, m.size(), f);
    fclose(f);
    return 0;
}
