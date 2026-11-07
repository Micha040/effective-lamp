#include "radar.h"

#include <cctype>
#include <cstring>

namespace fd {

namespace {
const RadarMap kMaps[] = {
#include "radar_maps.inc"
};
}

int radarMapCount() { return int(sizeof kMaps / sizeof kMaps[0]); }

const RadarMap* radarMap(const std::string& map, int* index) {
    std::string name = map;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < radarMapCount(); ++i) {
        if (name == kMaps[i].name) {
            if (index) *index = i;
            return &kMaps[i];
        }
    }
    return nullptr;
}

}  // namespace fd
