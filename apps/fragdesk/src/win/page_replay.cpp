// FragDesk – 2D-Ansicht einer Runde: Karte von oben mit Spielern, Blickrichtung, Granaten, Kills und Bombe
#include <algorithm>
#include <cmath>
#include <cwchar>

#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "widgets.h"

namespace fdw {

using namespace Gdiplus;
using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }

constexpr float kPanelW = 296;   // Liste rechts
constexpr float kSmokeRadius = 144, kFireRadius = 110, kFlashRadius = 220, kHeRadius = 350;   // Spieleinheiten

// Karte -> Bildschirm (DIP)
struct Geo {
    const fd::RadarMap* map = nullptr;
    RectF upper, lower;
    bool twoLevels = false;
    PointF at(float x, float y, float z) const {
        bool low = twoLevels && fd::radarLower(*map, z);
        const RectF& r = low ? lower : upper;
        float u, v;
        fd::radarUV(*map, x, y, u, v);
        return PointF(r.X + u * r.Width, r.Y + v * r.Height);
    }
    PointF at(const fd::Vec3s& p) const { return at(p.x, p.y, p.z); }
    float px(float units) const { return units / map->span * upper.Width; }
};

Color sideColor(const Canvas& c, bool ct) { return ct ? c.th.ctSide : c.th.tSide; }

Color grenadeColor(uint8_t kind) {
    switch (kind) {
        case fd::GrSmoke: return rgb(0xC9D1DC);
        case fd::GrFlash: return rgb(0xFFF3B0);
        case fd::GrHE: return rgb(0xF0634F);
        case fd::GrMolotov:
        case fd::GrIncendiary: return rgb(0xFF8A2A);
        default: return rgb(0x9BC48C);
    }
}

std::wstring clockText(double secs) {
    int s = int(std::floor(std::max(0.0, secs)));
    wchar_t b[16];
    swprintf(b, 16, L"%d:%02d", s / 60, s % 60);
    return b;
}

const std::string& nameOf(const fd::Replay& r, uint8_t i) {
    static const std::string unknown = "?";
    return i < r.players.size() ? r.players[i].name : unknown;
}

std::wstring shortName(const std::string& n, size_t max = 14) {
    std::wstring w = W(n);
    return w.size() > max ? w.substr(0, max - 1) + L"…" : w;
}

// Text mit dunklem Schatten (auf der Karte lesbar)
void outlined(Canvas& c, const std::wstring& s, F f, const Color& col, const RectF& r, Align a = Align::Center) {
    c.text(s, f, Color(210, 0, 0, 0), RectF(r.X + 1, r.Y + 1, r.Width, r.Height), a);
    c.text(s, f, col, r, a);
}

void fillCircle(Canvas& c, const PointF& p, float rad, const Color& col) { c.circle(p.X, p.Y, rad, col); }

// Seite (CT?) eines Spielers in der Runde laut Spielerliste
bool isCT(const ReplayView& rv, uint8_t player, bool fallback) {
    for (const auto& e : rv.roster)
        if (e.player == player) return e.ct;
    return fallback;
}

// ---- Kopf: Zurück, Titel, Runde wechseln ---------------------------------------------------------------------
float header(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    ReplayView& rv = app.replay;
    c.button(RectF(x - 8, y, 104, 32), L"Zurück", "rp-back", [&app] { app.go(Page::Match); }, Btn::Ghost, Icon::Back);
    std::wstring title = L"2D-Ansicht · " + W(fd::mapName(m.map));
    c.text(title, F::H2, c.th.text, RectF(x + 112, y, w - 480, 32));
    // Runde wechseln
    int count = int(m.rounds.size()), idx = -1;
    for (int i = 0; i < count; ++i)
        if (m.rounds[i].n == rv.round) idx = i;
    float rx = x + w - 230;
    bool ready = rv.state == ReplayView::State::Ready;
    c.iconButton(RectF(rx, y, 32, 32), Icon::ChevronLeft, "rp-prev",
                 [&app] {
                     if (app.detail) app.replay.stepRound(*app.detail, -1);
                 },
                 L"Vorige Runde (Pfeiltaste hoch)", ready && idx > 0);
    if (ready && idx >= 0)
        c.text(L"Runde " + std::to_wstring(rv.round) + L" von " + std::to_wstring(count), F::BodySemi, c.th.text, RectF(rx + 34, y, 162, 32),
               Align::Center);
    c.iconButton(RectF(rx + 198, y, 32, 32), Icon::Chevron, "rp-next",
                 [&app] {
                     if (app.detail) app.replay.stepRound(*app.detail, 1);
                 },
                 L"Nächste Runde (Pfeiltaste runter)", ready && idx >= 0 && idx + 1 < count);
    return 40;
}

// ---- Rundenleiste ---------------------------------------------------------------------------------------------
float roundBar(Canvas& c, App& app, const fd::Match& m, float x, float y, float w) {
    ReplayView& rv = app.replay;
    int count = int(m.rounds.size());
    if (!count) return 0;
    // Lücke bei jedem Seitenwechsel (Halbzeit, Verlängerung – passt auch für Wingman)
    float gap = 3, halfGap = 8;
    std::vector<bool> switched(count, false);
    int halves = 0;
    for (int i = 1; i < count; ++i)
        if (!m.rounds[i].sideA.empty() && m.rounds[i].sideA != m.rounds[i - 1].sideA) switched[i] = true, ++halves;
    float bw = std::min(30.0f, (w - (count - 1) * gap - halves * halfGap) / count);
    float h = 24, cx = x;
    for (int i = 0; i < count; ++i) {
        const fd::RoundInfo& r = m.rounds[i];
        if (switched[i]) cx += halfGap;
        RectF rr(cx, y, bw, h);
        bool sel = r.n == rv.round;
        bool ctWon = r.winnerSide == "CT";
        Color col = sideColor(c, ctWon);
        uint32_t id = hashId("rp-round-" + std::to_string(r.n));
        c.round(rr, 4, alpha(col, sel ? 255 : c.hot(id) ? 200 : 120));
        if (sel) c.roundStroke(RectF(rr.X - 1.5f, rr.Y - 1.5f, rr.Width + 3, rr.Height + 3), 5, c.th.text, 1.5f);
        if (bw >= 18) c.text(std::to_wstring(r.n), F::TinySemi, sel ? rgb(0x0E131B) : c.th.text, rr, Align::Center);
        int n = r.n;
        std::wstring tip = L"Runde " + std::to_wstring(r.n) + L": " + (ctWon ? L"CT" : L"T") + L" gewinnt – " + W(fd::reasonName(r.reason));
        c.region(rr, id, [&app, n] {
            if (app.detail) app.replay.setRound(*app.detail, n);
        }, tip);
        cx += bw + gap;
    }
    return h + 14;
}

// ---- Karte ----------------------------------------------------------------------------------------------------
void mapPanel(Canvas& c, App& app, const RectF& area) {
    ReplayView& rv = app.replay;
    const fd::Replay& r = *rv.data;
    const double t = rv.tick, tr = r.tickrate;
    c.round(area, 12, rgb(0x0E131B));
    const fd::RadarMap& map = rv.map();
    Geo geo;
    geo.map = &map;
    geo.twoLevels = map.lower;
    RectF a(area.X + 10, area.Y + 10, area.Width - 20, area.Height - 20);
    if (geo.twoLevels) {
        float side = std::min(a.Height, (a.Width - 12) / 2), stack = std::min(a.Width, (a.Height - 12) / 2);
        if (side >= stack) {
            float x0 = a.X + (a.Width - (side * 2 + 12)) / 2, y0 = a.Y + (a.Height - side) / 2;
            geo.upper = RectF(x0, y0, side, side);
            geo.lower = RectF(x0 + side + 12, y0, side, side);
        } else {
            float x0 = a.X + (a.Width - stack) / 2, y0 = a.Y + (a.Height - (stack * 2 + 12)) / 2;
            geo.upper = RectF(x0, y0, stack, stack);
            geo.lower = RectF(x0, y0 + stack + 12, stack, stack);
        }
    } else {
        float s = std::min(a.Width, a.Height);
        geo.upper = RectF(a.X + (a.Width - s) / 2, a.Y + (a.Height - s) / 2, s, s);
    }

    // Radarbild in Bildschirmpixeln (vorbereitet, deshalb ohne Skalieren gezeichnet)
    auto image = [&](bool lower, const RectF& rr) {
        int px = int(std::lround(rr.Width * c.scale));
        Bitmap* bmp = rv.radarBitmap(lower, px);
        if (!bmp) {   // ohne Bild: Raster
            Pen pen(Color(26, 255, 255, 255), 1);
            for (int i = 0; i <= 8; ++i) {
                float gx = rr.X + rr.Width * i / 8, gy = rr.Y + rr.Height * i / 8;
                c.g.DrawLine(&pen, gx, rr.Y, gx, rr.Y + rr.Height);
                c.g.DrawLine(&pen, rr.X, gy, rr.X + rr.Width, gy);
            }
            return;
        }
        GraphicsState st = c.g.Save();
        float dx = std::round((rr.X + c.ox()) * c.scale), dy = std::round((rr.Y + c.oy()) * c.scale);
        c.g.ResetTransform();
        c.g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        c.g.DrawImage(bmp, dx, dy, REAL(px), REAL(px));
        c.g.Restore(st);
    };
    image(false, geo.upper);
    if (geo.twoLevels) {
        image(true, geo.lower);
        outlined(c, L"OBEN", F::TinySemi, c.th.text2, RectF(geo.upper.X + 8, geo.upper.Y + 6, 80, 16), Align::Left);
        outlined(c, L"UNTEN", F::TinySemi, c.th.text2, RectF(geo.lower.X + 8, geo.lower.Y + 6, 80, 16), Align::Left);
    }
    if (!rv.radar)
        outlined(c, L"Kein Radarbild für diese Karte – Positionen auf Raster", F::Tiny, c.th.text3,
                 RectF(geo.upper.X + 8, geo.upper.Y + geo.upper.Height - 22, geo.upper.Width - 16, 16), Align::Left);
    // alles Weitere bleibt in der Kartenfläche (die Schnittfläche der Seite bleibt erhalten)
    GraphicsState clipState = c.g.Save();
    c.g.IntersectClip(area);

    // Wirkung der Granaten
    for (const auto& e : r.effects) {
        if (e.start > t) break;
        if (e.start < rv.startTick) continue;   // aus der Runde davor (beim Neustart verschwinden alle Granaten)
        double dur = e.end > e.start ? (e.end - e.start) / tr : e.kind == fd::FxSmoke ? 20 : e.kind == fd::FxFire ? 7 : e.kind == fd::FxDecoy ? 15 : 0.6;
        double age = (t - e.start) / tr;
        if (e.kind == fd::FxFlash || e.kind == fd::FxHE) dur = 0.6;
        if (age > dur || e.start > rv.endTick) continue;
        PointF p = geo.at(e.pos);
        switch (e.kind) {
            case fd::FxSmoke: {
                double fade = std::min({1.0, age / 0.8 + 0.3, (dur - age) / 1.5});
                float rad = geo.px(kSmokeRadius) * float(std::min(1.0, 0.55 + age / 1.2));
                fillCircle(c, p, rad, Color(BYTE(170 * fade), 196, 203, 212));
                c.ring(p.X, p.Y, rad, Color(BYTE(200 * fade), 230, 235, 242), 1.2f);
                break;
            }
            case fd::FxFire: {
                double fade = std::min({1.0, age / 0.5 + 0.2, (dur - age) / 1.0});
                Color fill(BYTE(120 * fade), 255, 120, 40), edge(BYTE(220 * fade), 255, 150, 60);
                if (e.hull.size() >= 3) {
                    std::vector<PointF> pts;
                    for (const auto& h : e.hull) pts.push_back(geo.at(h.first, h.second, e.pos.z));
                    SolidBrush b(fill);
                    Pen pen(edge, 1.2f);
                    c.g.FillPolygon(&b, pts.data(), INT(pts.size()));
                    c.g.DrawPolygon(&pen, pts.data(), INT(pts.size()));
                } else {
                    fillCircle(c, p, geo.px(kFireRadius), fill);
                }
                break;
            }
            case fd::FxFlash: {
                double k = age / dur;
                fillCircle(c, p, geo.px(kFlashRadius) * float(0.3 + 0.7 * k), Color(BYTE(190 * (1 - k)), 255, 250, 225));
                break;
            }
            case fd::FxHE: {
                double k = age / dur;
                c.ring(p.X, p.Y, geo.px(kHeRadius) * float(0.2 + 0.8 * k), Color(BYTE(230 * (1 - k)), 255, 110, 70), 3.0f);
                fillCircle(c, p, geo.px(90), Color(BYTE(160 * (1 - k)), 255, 140, 80));
                break;
            }
            default:
                c.ring(p.X, p.Y, 5, Color(200, 155, 196, 140), 1.5f);
                break;
        }
    }

    // Granaten im Flug mit Spur
    for (const auto& g : r.tracks) {
        if (g.points.empty() || g.points.front().first > t || g.points.back().first + tr * 0.15 < t) continue;
        if (g.points.back().first < rv.startTick) continue;
        Color col = grenadeColor(g.kind);
        std::vector<PointF> pts;
        PointF head = geo.at(g.points.front().second);
        for (size_t i = 0; i < g.points.size(); ++i) {
            const auto& pt = g.points[i];
            if (pt.first > t) {   // Kopf zwischen zwei Punkten
                const auto& prev = g.points[i - 1];
                float k = float((t - prev.first) / std::max(1, pt.first - prev.first));
                PointF a0 = geo.at(prev.second), a1 = geo.at(pt.second);
                head = PointF(a0.X + (a1.X - a0.X) * k, a0.Y + (a1.Y - a0.Y) * k);
                pts.push_back(head);
                break;
            }
            head = geo.at(pt.second);
            pts.push_back(head);
        }
        if (pts.size() >= 2) {
            Pen pen(alpha(col, 150), 1.4f);
            c.g.DrawLines(&pen, pts.data(), INT(pts.size()));
        }
        fillCircle(c, head, 3.6f, col);
        c.ring(head.X, head.Y, 3.6f, Color(160, 0, 0, 0), 1);
    }

    // Bombe (liegt oder gelegt) laut Frame
    if (!r.frames.empty()) {
        const fd::ReplayFrame& f = r.frames[fd::frameAt(r, t)];
        if (f.bomb && f.tick >= rv.startTick) {
            PointF p = geo.at(f.bombPos);
            if (f.bomb == 2) {
                double pulse = 0.5 + 0.5 * std::sin(t / tr * 6.0);
                c.ring(p.X, p.Y, 9 + float(pulse) * 4, Color(BYTE(120 + 100 * pulse), 255, 70, 60), 2);
                c.round(RectF(p.X - 7, p.Y - 5, 14, 10), 2, rgb(0xE5483B));
            } else {
                c.round(RectF(p.X - 6, p.Y - 4.5f, 12, 9), 2, rgb(0xE8A23A));
            }
            outlined(c, L"C4", F::TinySemi, rgb(0xFFFFFF), RectF(p.X - 20, p.Y + 6, 40, 14));
        }
    }
    for (const auto& b : r.bomb) {   // Explosion
        if (b.kind != fd::BombExploded || b.tick > t || b.tick < rv.startTick) continue;
        double age = (t - b.tick) / tr;
        if (age > 1.2) continue;
        PointF p = geo.at(b.pos);
        fillCircle(c, p, geo.px(600) * float(age / 1.2), Color(BYTE(170 * (1 - age / 1.2)), 255, 160, 60));
    }

    const float rad = std::max(6.0f, geo.upper.Width / 90);   // Spielerpunkt

    // Tode dieser Runde: Kreuz, kurz danach eine Linie vom Schützen
    const float xs = rad * 0.8f;
    auto lo = std::lower_bound(r.kills.begin(), r.kills.end(), rv.startTick, [](const fd::ReplayKill& k, int v) { return k.tick < v; });
    for (auto it = lo; it != r.kills.end() && it->tick <= t; ++it) {
        if (it->tick > rv.endTick) break;
        PointF v = geo.at(it->victimPos);
        bool ct = isCT(rv, it->victim, false);
        double age = (t - it->tick) / tr;
        if (age < 1.5 && it->killer != it->victim && it->killer != fd::kNoPlayer) {
            PointF k = geo.at(it->killerPos);
            Pen pen(Color(BYTE(200 * (1 - age / 1.5)), 255, 255, 255), 1.3f);
            c.g.DrawLine(&pen, k, v);
        }
        Color col = alpha(sideColor(c, ct), age < 3 ? 255 : 160);
        Pen pen(col, 2.2f);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        c.g.DrawLine(&pen, v.X - xs, v.Y - xs, v.X + xs, v.Y + xs);
        c.g.DrawLine(&pen, v.X + xs, v.Y - xs, v.X - xs, v.Y + xs);
    }

    // Spieler
    std::vector<fd::PlayerAt> ps;
    if (t >= rv.startTick - tr) fd::playersAt(r, t, ps);
    // wer hat gerade geschossen?
    bool shot[256] = {};
    {
        double from = t - 0.12 * tr;
        auto s0 = std::lower_bound(r.shots.begin(), r.shots.end(), from, [](const fd::ReplayShot& s, double v) { return s.tick < v; });
        for (auto it = s0; it != r.shots.end() && it->tick <= t; ++it) shot[it->player] = true;
    }
    for (const auto& p : ps) {
        if (p.player >= r.players.size()) continue;
        PointF pos = geo.at(p.x, p.y, p.z);
        bool ct = (p.flags & fd::RpCT) != 0;
        Color col = sideColor(c, ct);
        std::string key = ReplayView::playerKey(r.players[p.player]);
        bool focus = !rv.follow.empty() && key == rv.follow;
        bool me = !app.me.empty() && r.players[p.player].steamId == app.me;
        // Blickrichtung: Spielwinkel gegen den Uhrzeigersinn ab Osten, GDI+ im Uhrzeigersinn
        float cone = rad * 4.2f;
        SolidBrush coneBrush(alpha(col, focus ? 150 : 105));
        c.g.FillPie(&coneBrush, RectF(pos.X - cone, pos.Y - cone, cone * 2, cone * 2), -p.yaw - 22, 44);
        if (shot[p.player]) {
            float yawRad = -p.yaw * 3.14159265f / 180.0f;
            Pen tracer(Color(230, 255, 236, 140), 1.6f);
            c.g.DrawLine(&tracer, pos.X + std::cos(yawRad) * rad, pos.Y + std::sin(yawRad) * rad, pos.X + std::cos(yawRad) * rad * 8,
                         pos.Y + std::sin(yawRad) * rad * 8);
        }
        if (p.flags & fd::RpFlashed) fillCircle(c, pos, rad + 5, Color(150, 255, 255, 255));
        if (focus || me) c.ring(pos.X, pos.Y, rad + 3.5f, me && !focus ? alpha(c.th.text, 200) : rgb(0xFFFFFF), 1.6f);
        fillCircle(c, pos, rad, col);
        // HP als Ring um den Punkt: voll = 100, der Rest dunkel
        {
            Pen back(Color(190, 0, 0, 0), 2.0f);
            c.g.DrawEllipse(&back, pos.X - rad, pos.Y - rad, rad * 2, rad * 2);
            Pen hp(p.hp > 50 ? rgb(0xEAF2FF) : p.hp > 20 ? rgb(0xF5C04A) : rgb(0xF0634F), 2.0f);
            float sweep = 360.0f * std::min<int>(p.hp, 100) / 100.0f;
            if (sweep > 0) c.g.DrawArc(&hp, pos.X - rad, pos.Y - rad, rad * 2, rad * 2, -90.0f, sweep);
        }
        if (p.flags & fd::RpBomb) c.round(RectF(pos.X + rad - 2, pos.Y - rad - 4, 8, 6), 1.5f, rgb(0xE5483B));
        if (p.flags & (fd::RpDefusing | fd::RpPlanting)) {
            double pulse = 0.5 + 0.5 * std::sin(t / tr * 8.0);
            c.ring(pos.X, pos.Y, rad + 6 + float(pulse) * 3, Color(220, 255, 210, 90), 1.6f);
        }
        if (rv.names) outlined(c, shortName(r.players[p.player].name, 12), F::TinySemi, focus ? rgb(0xFFFFFF) : rgb(0xE6EAF0),
                               RectF(pos.X - 60, pos.Y + rad + 1, 120, 14));
        std::wstring tip = W(r.players[p.player].name) + L"\n" + std::to_wstring(p.hp) + L" HP · " + W(fd::replayWeapon(r, p.weapon));
        if (p.flags & fd::RpBomb) tip += L" · trägt die Bombe";
        c.region(RectF(pos.X - rad - 2, pos.Y - rad - 2, rad * 2 + 4, rad * 2 + 4), hashId("rp-dot-" + key), [&app, key] {
            app.replay.follow = app.replay.follow == key ? std::string() : key;
        }, tip);
    }
    c.g.Restore(clipState);
    if (ps.empty() && t >= rv.freezeTick)
        outlined(c, L"Für diesen Zeitpunkt gibt es keine Positionsdaten", F::Small, c.th.text2,
                 RectF(area.X, area.Y + area.Height / 2 - 10, area.Width, 20));
}

// ---- Liste rechts: Teams und Kills ----------------------------------------------------------------------------
void sidePanel(Canvas& c, App& app, const RectF& area) {
    ReplayView& rv = app.replay;
    const fd::Replay& r = *rv.data;
    const double t = rv.tick;
    c.card(area);
    std::vector<fd::PlayerAt> ps;
    if (t >= rv.startTick - r.tickrate) fd::playersAt(r, t, ps);
    float y = area.Y + 12, x = area.X + 14, w = area.Width - 28;
    // Zeilen der Spieler: zweizeilig (Name, Waffe), wenn der Platz reicht, sonst einzeilig – darunter sollen noch ein paar Kills passen
    const float fixed = 12 + 2 * 26 + 2 * 8 + 10 + 24 + 4 * 21 + 8;
    float per = rv.roster.empty() ? 40 : (area.Height - fixed) / float(rv.roster.size());
    const bool twoLines = per >= 40;
    const float step = twoLines ? 40 : std::clamp(per, 24.0f, 40.0f);
    for (int side = 0; side < 2; ++side) {
        bool ct = side == 0;
        int alive = 0, total = 0;
        for (const auto& e : rv.roster)
            if (e.ct == ct) {
                ++total;
                if (std::any_of(ps.begin(), ps.end(), [&](const fd::PlayerAt& p) { return p.player == e.player; })) ++alive;
            }
        Color col = sideColor(c, ct);
        c.circle(x + 5, y + 10, 5, col);
        c.text(ct ? L"Counter-Terroristen" : L"Terroristen", F::SmallSemi, c.th.text, RectF(x + 16, y, w - 60, 20));
        c.text(std::to_wstring(alive) + L"/" + std::to_wstring(total), F::SmallSemi, c.th.text2, RectF(x + w - 50, y, 50, 20), Align::Right);
        y += 26;
        for (const auto& e : rv.roster) {
            if (e.ct != ct || e.player >= r.players.size()) continue;
            const fd::PlayerAt* p = nullptr;
            for (const auto& q : ps)
                if (q.player == e.player) p = &q;
            std::string key = ReplayView::playerKey(r.players[e.player]);
            bool focus = rv.follow == key;
            RectF row(x - 6, y - 2, w + 12, step - 2);
            uint32_t id = hashId("rp-row-" + key);
            if (focus) c.round(row, 8, alpha(col, 40));
            else if (c.hot(id)) c.round(row, 8, c.th.hover);
            Color nameCol = p ? c.th.text : c.th.text3;
            bool me = !app.me.empty() && r.players[e.player].steamId == app.me;
            std::wstring name = shortName(r.players[e.player].name, 18) + (me ? L" (du)" : L"") + (p ? L"" : L"  †");
            std::wstring wep;
            if (p) {
                wep = W(fd::replayWeapon(r, p->weapon));
                if (p->flags & fd::RpBomb) wep += L" · C4";
                if (p->flags & fd::RpKit) wep += L" · Kit";
            }
            float lineY = twoLines ? y : row.Y + (row.Height - 18) / 2;   // Zeile mit Name und HP
            c.text(name, F::SmallSemi, nameCol, RectF(x, lineY, w - 70, 18));
            if (p) {
                if (twoLines) c.text(wep, F::Tiny, c.th.text2, RectF(x, y + 17, w - 70, 16));
                RectF bar(x + w - 64, lineY + 7, 40, 5);
                c.round(bar, 2.5f, c.th.track);
                if (p->hp > 0) c.round(RectF(bar.X, bar.Y, bar.Width * std::min<int>(p->hp, 100) / 100.0f, bar.Height), 2.5f, p->hp > 20 ? col : c.th.danger);
                c.text(std::to_wstring(p->hp), F::TinySemi, c.th.text2, RectF(x + w - 22, lineY, 22, 18), Align::Right);
            }
            std::wstring tip = focus ? L"Hervorhebung aufheben" : L"Auf der Karte hervorheben";
            if (!twoLines && !wep.empty()) tip = wep + L"\n" + tip;
            c.region(row, id, [&app, key] { app.replay.follow = app.replay.follow == key ? std::string() : key; }, tip);
            y += step;
        }
        y += 8;
    }
    // Kills dieser Runde bis jetzt, der Reihe nach (passt nicht alles, die neuesten); Klick springt kurz davor
    c.line(x, y, x + w, y, c.th.border, 1);
    y += 10;
    c.text(L"Kills", F::SmallSemi, c.th.text, RectF(x, y, w, 20));
    y += 24;
    std::vector<const fd::ReplayKill*> ks;
    for (const auto& k : r.kills)
        if (k.tick >= rv.startTick && k.tick <= t && k.tick <= rv.endTick) ks.push_back(&k);
    if (ks.empty()) c.text(L"Noch keine", F::Small, c.th.text3, RectF(x, y, w, 18));
    const float rowH = 21;
    size_t fit = size_t(std::max(0.0f, (area.Y + area.Height - 8 - y) / rowH));
    for (size_t i = ks.size() > fit ? ks.size() - fit : 0; i < ks.size(); ++i) {
        const fd::ReplayKill& k = *ks[i];
        bool kct = isCT(rv, k.killer, false), vct = isCT(rv, k.victim, true);
        std::wstring when = k.tick < rv.freezeTick ? L"–" : clockText((k.tick - rv.freezeTick) / r.tickrate);
        std::wstring killer = k.killer == fd::kNoPlayer || k.killer == k.victim ? L"" : shortName(nameOf(r, k.killer), 11);
        std::wstring victim = shortName(nameOf(r, k.victim), 11);
        std::wstring wep = W(fd::replayWeapon(r, k.weapon)) + ((k.flags & fd::KfHeadshot) ? L" HS" : L"");
        RectF row(x - 6, y - 1, w + 12, rowH);
        uint32_t id = hashId("rp-kill-" + std::to_string(k.tick) + "-" + std::to_string(k.victim));
        if (c.hot(id)) c.round(row, 6, c.th.hover);
        float tx = x + 34, tw = w - 34;
        c.text(when, F::Tiny, c.th.text3, RectF(x, y, 32, 18));
        float kw = killer.empty() ? 0 : std::min(tw * 0.36f, c.measure(killer, F::TinySemi) + 2);
        float ww = std::min(tw * 0.34f, c.measure(wep, F::Tiny) + 2);
        if (!killer.empty()) c.text(killer, F::TinySemi, sideColor(c, kct), RectF(tx, y, kw, 18));
        c.text(wep, F::Tiny, c.th.text2, RectF(tx + kw + 5, y, ww, 18));
        c.text(victim, F::TinySemi, sideColor(c, vct), RectF(tx + kw + ww + 10, y, std::max(0.0f, tw - kw - ww - 10), 18));
        double to = k.tick - 2.0 * r.tickrate;
        c.region(row, id, [&app, to] { app.replay.seek(to); }, L"Zu diesem Kill springen (2 Sekunden davor)");
        y += rowH;
    }
}

// ---- Zeitleiste -----------------------------------------------------------------------------------------------
void timeline(Canvas& c, App& app, const RectF& area) {
    ReplayView& rv = app.replay;
    const fd::Replay& r = *rv.data;
    const double tr = r.tickrate;
    c.card(area);
    float x = area.X + 12, cy = area.Y + area.Height / 2;
    // Abspielen
    {
        RectF b(x, cy - 20, 40, 40);
        uint32_t id = hashId("rp-play");
        c.round(b, 20, c.hot(id) ? c.th.accentHover : c.th.accent);
        c.icon(rv.playing ? Icon::Pause : Icon::Play, RectF(b.X + 11, b.Y + 11, 18, 18), c.th.onAccent, 1.8f);
        c.region(b, id, [&app] { app.replay.togglePlay(); }, rv.playing ? L"Anhalten (Leertaste)" : L"Abspielen (Leertaste)");
        x += 52;
    }
    // Uhr
    {
        bool buy = rv.tick < rv.freezeTick;
        std::wstring time = buy ? L"Kaufzeit" : clockText((rv.tick - rv.freezeTick) / tr);
        c.text(time, F::H3, c.th.text, RectF(x, cy - 20, 96, 24));
        // gelegte Bombe: Zeit bis zur Explosion (40 s wie in Premier, Wettkampf und FACEIT)
        int plant = -1;
        for (const auto& b : r.bomb) {
            if (b.tick < rv.startTick || b.tick > rv.tick) continue;
            plant = b.kind == fd::BombPlanted ? b.tick : -1;
        }
        if (plant >= 0 && rv.tick - plant < 40 * tr) {
            c.text(L"C4 · noch " + clockText(40 - (rv.tick - plant) / tr + 0.999), F::TinySemi, rgb(0xF0634F), RectF(x, cy + 3, 120, 16));
        } else {
            c.text(buy ? L"Runde " + std::to_wstring(rv.round) : L"nach der Kaufzeit", F::Tiny, c.th.text2, RectF(x, cy + 3, 120, 16));
        }
        x += 126;
    }
    // Tempo
    float speedW = c.chipRowWidth({L"½×", L"1×", L"2×", L"4×"});
    float sx = area.X + area.Width - 12 - speedW;
    int sel = rv.speed < 0.75 ? 0 : rv.speed < 1.5 ? 1 : rv.speed < 3 ? 2 : 3;
    c.chipRow(sx, cy - 14, {L"½×", L"1×", L"2×", L"4×"}, sel, "rp-speed", [&app](int i) {
        static const double v[] = {0.5, 1, 2, 4};
        app.replay.speed = v[std::clamp(i, 0, 3)];
    }, 28);
    // Namen auf der Karte ein/aus
    float nw = c.chipRowWidth({L"Namen"}), nx = sx - 16 - nw;
    c.chip(RectF(nx, cy - 14, nw, 28), L"Namen", rv.names, "rp-names", [&app] { app.replay.names = !app.replay.names; });
    // Leiste
    float x0 = x + 6, x1 = nx - 20;
    if (x1 - x0 < 60) return;
    double span = std::max(1, rv.endTick - rv.startTick);
    auto xAt = [&](double tick) { return x0 + float((tick - rv.startTick) / span) * (x1 - x0); };
    RectF track(x0, cy - 3, x1 - x0, 6);
    c.round(track, 3, c.th.track);
    float now = xAt(std::clamp(rv.tick, double(rv.startTick), double(rv.endTick)));
    if (now - x0 > 1) c.round(RectF(x0, cy - 3, now - x0, 6), 3, alpha(c.th.accent, 200));
    // Ende der Kaufzeit
    float fx = xAt(rv.freezeTick);
    c.line(fx, cy - 10, fx, cy + 10, alpha(c.th.text3, 180), 1.2f);
    // Ereignisse: Striche oben (Kills), Rauten unten (Bombe); Klick springt kurz davor
    struct Mark {
        RectF r;
        int tick;
        std::wstring tip;
    };
    std::vector<Mark> marks;
    for (const auto& k : r.kills) {
        if (k.tick < rv.startTick || k.tick > rv.endTick) continue;
        float kx = xAt(k.tick);
        Color col = sideColor(c, isCT(rv, k.killer, !isCT(rv, k.victim, true)));
        c.round(RectF(kx - 1.5f, cy - 14, 3, 8), 1.5f, col);
        bool world = k.killer == fd::kNoPlayer || k.killer == k.victim;
        std::wstring what = world ? W(nameOf(r, k.victim)) + L" stirbt" : W(nameOf(r, k.killer)) + L" tötet " + W(nameOf(r, k.victim));
        std::wstring wep = W(fd::replayWeapon(r, k.weapon));
        if (!wep.empty() || (k.flags & fd::KfHeadshot))
            what += L" (" + wep + ((k.flags & fd::KfHeadshot) ? (wep.empty() ? L"Headshot" : L", Headshot") : L"") + L")";
        marks.push_back({RectF(kx - 4, cy - 17, 8, 12), k.tick, clockText((k.tick - rv.freezeTick) / tr) + L"  " + what});
    }
    for (const auto& b : r.bomb) {
        if (b.tick < rv.startTick || b.tick > rv.endTick) continue;
        float bx = xAt(b.tick);
        Color col = b.kind == fd::BombPlanted ? rgb(0xE5483B) : b.kind == fd::BombDefused ? c.th.ctSide : rgb(0xE8A23A);
        PointF pts[] = {PointF(bx, cy + 6), PointF(bx + 5, cy + 11), PointF(bx, cy + 16), PointF(bx - 5, cy + 11)};
        SolidBrush br(col);
        c.g.FillPolygon(&br, pts, 4);
        std::wstring what = b.kind == fd::BombPlanted ? L"Bombe gelegt" : b.kind == fd::BombDefused ? L"Bombe entschärft" : L"Bombe explodiert";
        if (b.player != fd::kNoPlayer && b.kind != fd::BombExploded) what += L" von " + W(nameOf(r, b.player));
        marks.push_back({RectF(bx - 6, cy + 5, 12, 12), b.tick, clockText((b.tick - rv.freezeTick) / tr) + L"  " + what});
    }
    c.circle(now, cy, 7, c.th.text);
    c.circle(now, cy, 4, c.th.accent);
    c.dragRegion(RectF(x0 - 6, cy - 16, x1 - x0 + 12, 32), hashId("rp-scrub"), [&app, x0, x1](float fx, float) {
        ReplayView& v = app.replay;
        // fx bezieht sich auf die Fläche inklusive 6 DIP Rand links und rechts
        float w = x1 - x0 + 12;
        double k = std::clamp((fx * w - 6) / (x1 - x0), 0.0f, 1.0f);
        v.seek(v.startTick + k * (v.endTick - v.startTick));
    }, L"Ziehen oder klicken, um zu springen (Pfeiltasten links/rechts: 5 s)");
    // nach der Leiste anmelden, damit sie über ihr liegen
    for (const auto& mk : marks) {
        double to = mk.tick - 2.0 * tr;
        c.region(mk.r, hashId("rp-mark-" + std::to_string(mk.tick) + "-" + std::to_string(int(mk.r.Y))), [&app, to] { app.replay.seek(to); },
                 mk.tip + L"\nKlick: 2 Sekunden davor ansehen");
    }
}

}  // namespace

float drawReplay(Canvas& c, App& app, float w, float h) {
    ReplayView& rv = app.replay;
    const float pad = 24;
    float x = pad, cw = w - pad * 2, y = 18;
    if (!app.detail) {
        c.button(RectF(x - 8, y, 104, 32), L"Zurück", "rp-back", [&app] { app.go(Page::Matches); }, Btn::Ghost, Icon::Back);
        emptyState(c, x, y + 60, cw, Icon::Warning, L"Kein Match geöffnet", L"Öffne ein Match und wähle dort „2D-Ansicht“.");
        return h;
    }
    const fd::Match& m = *app.detail;
    y += header(c, app, m, x, y, cw);
    if (rv.state == ReplayView::State::Working) {
        emptyState(c, x, y + 40, cw, Icon::Clock, L"2D-Daten werden erstellt …",
                   L"FragDesk liest die Demo einmal komplett ein (Positionen, Granaten, Kills). Das dauert ein paar Sekunden und passiert "
                   L"pro Match nur einmal.");
        return h;
    }
    if (rv.state != ReplayView::State::Ready || !rv.data) {
        float eh = emptyState(c, x, y + 40, cw, Icon::Warning, L"2D-Ansicht nicht möglich", rv.error.empty() ? L"Unbekannter Fehler" : rv.error);
        c.button(RectF(x + (cw - 170) / 2, y + 40 + eh + 4, 170, 36), L"Erneut versuchen", "rp-retry", [&app] {
            if (app.detail) app.replay.open(app.hwnd, *app.detail, app.replay.round);
        }, Btn::Secondary, Icon::Refresh);
        return h;
    }
    y += roundBar(c, app, m, x, y, cw);
    float bottomH = 64, gap = 14;
    float mainH = std::max(200.0f, h - y - bottomH - gap - 18);
    float panelW = cw >= 900 ? kPanelW : 250;
    RectF mapArea(x, y, cw - panelW - gap, mainH);
    RectF side(x + cw - panelW, y, panelW, mainH);
    mapPanel(c, app, mapArea);
    sidePanel(c, app, side);
    timeline(c, app, RectF(x, y + mainH + gap, cw, bottomH));
    c.text(L"Leertaste: Abspielen · Pfeiltasten: 5 s vor/zurück, Runde wechseln · Spieler anklicken: hervorheben", F::Tiny, c.th.text3,
           RectF(x, y + mainH + gap + bottomH + 2, cw, 16));
    return h;
}

}  // namespace fdw
