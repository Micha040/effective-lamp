// FragDesk – Hilfen für Aufnahmen mit CS2 (siehe cs2con.h)
#include "cs2con.h"

#include <algorithm>
#include <climits>

namespace fd {

std::string lastUiState(const std::string& text, size_t from) {
    static const char kTok[] = "UI_STATE_";
    std::string state;
    for (size_t pos = from; pos < text.size();) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) break;   // halbe Zeile: kommt beim nächsten Mal ganz
        size_t arrow = text.rfind("->", eol);
        if (arrow != std::string::npos && arrow >= pos) {
            size_t at = text.find(kTok, arrow);
            if (at != std::string::npos && at < eol) {
                size_t b = at + sizeof kTok - 1, e = b;
                while (e < eol && ((text[e] >= 'A' && text[e] <= 'Z') || (text[e] >= '0' && text[e] <= '9') || text[e] == '_')) ++e;
                if (e > b) state = text.substr(b, e - b);
            }
        }
        pos = eol + 1;
    }
    return state;
}

namespace {

// Zeiger mit Spitze (x, y) und Größe size überdeckt area?
bool touches(const IRect& area, int x, int y, int size) {
    return x < area.right && x + size > area.left && y < area.bottom && y + size > area.top;
}

}  // namespace

bool cursorParkPoint(const IRect& area, int x, int y, int size, const std::vector<IRect>& monitors, int& outX, int& outY) {
    if (!touches(area, x, y, size)) return false;
    struct P {
        int x, y;
    };
    // knapp neben jeder Kante (in Zeile bzw. Spalte des Zeigers) und der nächste Punkt jedes Bildschirms
    std::vector<P> cands = {{area.right, y}, {x, area.bottom}, {area.left - size, y}, {x, area.top - size}};
    for (const IRect& m : monitors) {
        int maxX = std::max(m.left, m.right - size), maxY = std::max(m.top, m.bottom - size);
        cands.push_back({std::min(std::max(x, m.left), maxX), std::min(std::max(y, m.top), maxY)});
    }
    long long best = LLONG_MAX;
    for (const P& c : cands) {
        bool onScreen = std::any_of(monitors.begin(), monitors.end(), [&](const IRect& m) { return m.contains(c.x, c.y); });
        if (!onScreen || touches(area, c.x, c.y, size)) continue;
        long long dx = c.x - x, dy = c.y - y, d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            outX = c.x;
            outY = c.y;
        }
    }
    if (best == LLONG_MAX) {   // kein Platz außerhalb: in die Ecke
        outX = area.right - 1;
        outY = area.bottom - 1;
    }
    return true;
}

}  // namespace fd
