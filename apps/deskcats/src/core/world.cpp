// DeskCats – Welt (siehe world.h)
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <unordered_map>

namespace cat {

bool Platform::visibleAt(int x) const {
    for (const Span& s : visible)
        if (x >= s.x0 && x <= s.x1) return true;
    return false;
}

int Platform::visibleLength() const {
    int n = 0;
    for (const Span& s : visible) n += std::max(0, s.x1 - s.x0);
    return n;
}

const Platform* World::find(uint64_t id) const {
    for (const Platform& p : plats)
        if (p.id == id) return &p;
    return nullptr;
}

int World::monitorAt(int x, int y) const {
    for (size_t i = 0; i < monitors.size(); ++i) {
        const Monitor& m = monitors[i];
        if (x >= m.x0 && x < m.x1 && y >= m.y0 && y < m.y1) return (int)i;
    }
    return -1;
}

int World::scaleAt(int x, int y) const {
    int m = monitorAt(x, y);
    if (m >= 0) return monitors[m].scale;
    // nächstgelegener Monitor
    long best = -1;
    int sc = 3;
    for (const Monitor& mo : monitors) {
        long dx = x < mo.x0 ? mo.x0 - x : (x >= mo.x1 ? x - mo.x1 + 1 : 0);
        long dy = y < mo.y0 ? mo.y0 - y : (y >= mo.y1 ? y - mo.y1 + 1 : 0);
        long d = dx + dy;
        if (best < 0 || d < best) {
            best = d;
            sc = mo.scale;
        }
    }
    return sc;
}

const Platform* World::landingBelow(double x, double yFrom, double yTo, uint64_t exclude) const {
    const Platform* best = nullptr;
    int ix = (int)std::lround(x);
    for (const Platform& p : plats) {
        if (p.id == exclude) continue;
        if (ix < p.x0 || ix > p.x1) continue;
        if (p.y <= yFrom || p.y > yTo) continue;
        // vorne liegende Fläche gewinnt bei gleicher Höhe
        if (!best || p.y < best->y || (p.y == best->y && p.z < best->z)) best = &p;
    }
    return best;
}

const Platform* World::nearestBelow(int x, int y, int maxDy) const {
    const Platform* best = nullptr;
    for (const Platform& p : plats) {
        if (x < p.x0 || x > p.x1) continue;
        int dy = p.y - y;
        if (dy < -40 || dy > maxDy) continue;
        if (!best || std::abs(dy) < std::abs(best->y - y)) best = &p;
    }
    return best;
}

namespace {

bool adjacent(const Platform& a, const Platform& b) {
    return std::abs(a.y - b.y) <= 3 && a.x1 >= b.x0 - 2 && b.x1 >= a.x0 - 2;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool jumpPoints(const Platform& a, const Platform& b, int x, const Reach& r, int catHalf, int& takeoff, int& land) {
    int dy = b.y - a.y;
    if (dy < -r.up || dy > r.down) return false;
    int lb0 = b.x0 + catHalf, lb1 = b.x1 - catHalf;
    if (lb0 > lb1) return false;   // zu schmal zum Landen
    int ta0 = a.x0 + catHalf / 2, ta1 = a.x1 - catHalf / 2;
    if (ta0 > ta1) ta0 = ta1 = (a.x0 + a.x1) / 2;
    // Kandidaten: nächster Punkt zu x, und je sichtbares Stück der nächste Punkt
    std::vector<int> cands;
    cands.push_back(clampi(x, lb0, lb1));
    for (const Span& s : b.visible) {
        int s0 = std::max(s.x0 + catHalf / 2, lb0), s1 = std::min(s.x1 - catHalf / 2, lb1);
        if (s0 <= s1) cands.push_back(clampi(x, s0, s1));
    }
    bool found = false;
    long bestCost = 0;
    for (int c : cands) {
        int t = clampi(c, ta0, ta1);
        int side = std::abs(t - c);
        if (side > r.side) continue;
        // senkrecht nach oben geht nicht durch das eigene Fenster hindurch, aber an der Kante vorbei schon
        long cost = side + std::abs(t - x) / 2 + (b.visibleAt(c) ? 0 : 600);
        if (!found || cost < bestCost) {
            found = true;
            bestCost = cost;
            takeoff = t;
            land = c;
        }
    }
    return found;
}

bool edgeLink(const World& w, const Platform& a, const Platform& b, int& edgeX) {
    if (a.kind == PlatKind::Window || b.kind == PlatKind::Window || a.monitor == b.monitor) return false;
    if (a.monitor < 0 || b.monitor < 0 || a.monitor >= (int)w.monitors.size() || b.monitor >= (int)w.monitors.size()) return false;
    const Monitor& ma = w.monitors[(size_t)a.monitor];
    const Monitor& mb = w.monitors[(size_t)b.monitor];
    // nebeneinander (gemeinsame senkrechte Kante) und senkrecht deutlich überlappend
    if (std::min(ma.y1, mb.y1) - std::max(ma.y0, mb.y0) < 200) return false;
    if (std::abs(ma.x1 - mb.x0) <= 2 && a.x1 >= ma.x1 - 2 && b.x0 <= mb.x0 + 2) {
        edgeX = ma.x1;
        return true;
    }
    if (std::abs(mb.x1 - ma.x0) <= 2 && b.x1 >= mb.x1 - 2 && a.x0 <= ma.x0 + 2) {
        edgeX = ma.x0;
        return true;
    }
    return false;
}

namespace {

// Breitensuche über Flächen (Kanten: nebeneinander auf gleicher Höhe, erreichbarer Sprung, ggf. Monitorrand).
// Liefert den ersten Schritt nach `from` (Index) oder -1.
int firstStep(const World& w, size_t from, size_t to, const Reach& r, int catHalf, bool edges) {
    const size_t n = w.plats.size();
    std::vector<int> prev(n, -1);
    std::vector<char> seen(n, 0);
    std::deque<size_t> q;
    q.push_back(from);
    seen[from] = 1;
    int dummyT, dummyL, ex;
    while (!q.empty()) {
        size_t c = q.front();
        q.pop_front();
        if (c == to) break;
        const Platform& pc = w.plats[c];
        int cx = (pc.x0 + pc.x1) / 2;
        for (size_t k = 0; k < n; ++k) {
            if (seen[k]) continue;
            const Platform& pk = w.plats[k];
            if (adjacent(pc, pk) || jumpPoints(pc, pk, cx, r, catHalf, dummyT, dummyL) || (edges && edgeLink(w, pc, pk, ex))) {
                seen[k] = 1;
                prev[k] = (int)c;
                q.push_back(k);
            }
        }
    }
    if (!seen[to]) return -1;
    size_t step = to;
    while (prev[step] >= 0 && (size_t)prev[step] != from) step = (size_t)prev[step];
    return (int)step;
}

}  // namespace

bool planHop(const World& w, const Platform& from, int x, const Platform& to, int toX, const Reach& r, int catHalf, Hop& out) {
    out = Hop();
    if (from.id == to.id) {
        out.next = to.id;
        out.walk = true;
        out.takeoffX = out.landX = toX;
        return true;
    }
    const size_t n = w.plats.size();
    size_t iF = n, iT = n;
    for (size_t i = 0; i < n; ++i) {
        if (w.plats[i].id == from.id) iF = i;
        if (w.plats[i].id == to.id) iT = i;
    }
    if (iF == n || iT == n) return false;
    int step = firstStep(w, iF, iT, r, catHalf, false);
    if (step < 0) step = firstStep(w, iF, iT, r, catHalf, true);   // nur über einen Monitorrand erreichbar
    if (step < 0) return false;
    const Platform& nx = w.plats[(size_t)step];
    out.next = nx.id;
    int ex = 0;
    if (!adjacent(from, nx) && !jumpPoints(from, nx, x, r, catHalf, out.takeoffX, out.landX) && edgeLink(w, from, nx, ex)) {
        // am Rand ganz auf dem eigenen Monitor ankommen, auf dem anderen ganz drüben weitergehen (kein halber Körper
        // in falscher Höhe auf dem Nachbarmonitor)
        out.edge = true;
        const bool right = ex >= from.x1 - 2;
        out.takeoffX = right ? ex - catHalf : ex + catHalf;
        out.landX = right ? ex + catHalf : ex - catHalf;
        return true;
    }
    if (adjacent(from, nx)) {
        out.walk = true;
        // Übergangspunkt: Mitte der Überlappung bzw. Stoßkante
        int o0 = std::max(from.x0, nx.x0), o1 = std::min(from.x1, nx.x1);
        out.takeoffX = out.landX = o0 <= o1 ? (o0 + o1) / 2 : (from.x1 < nx.x0 ? from.x1 : from.x0);
        return true;
    }
    (void)toX;   // Sprungziel: der Punkt auf `next`, der am nächsten liegt (jumpPoints)
    out.walk = false;
    return jumpPoints(from, nx, x, r, catHalf, out.takeoffX, out.landX);
}

}  // namespace cat
