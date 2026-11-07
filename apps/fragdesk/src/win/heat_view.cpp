#include "heat_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "widgets.h"

using namespace Gdiplus;
using namespace ui;

namespace fdw {

namespace {

constexpr int kGrid = 256;          // Raster der Dichte (Zellen je Seite)
constexpr float kPanelW = 280;      // Auswahl rechts neben der Karte
constexpr size_t kMaxClickable = 2500;   // mehr Punkte: nur zeichnen
constexpr size_t kMaxDrawn = 6000;       // mehr: gar keine Einzelpunkte (nur die Dichte)

struct LayerInfo {
    fd::HeatLayer layer;
    const wchar_t* label;
    uint32_t color;
};
const LayerInfo kLayers[] = {
    {fd::HlPresence, L"Aufenthalt", 0x3BA3FF}, {fd::HlKills, L"Kills", 0x34D399},  {fd::HlDeaths, L"Tode", 0xF87171},
    {fd::HlSmoke, L"Smokes", 0xD7DCE3},       {fd::HlMolotov, L"Mollys", 0xFB923C}, {fd::HlFlash, L"Flashes", 0xFDE047},
    {fd::HlHE, L"HEs", 0xF472B6},
};

bool isNade(uint8_t layer) { return layer >= fd::HlSmoke && layer <= fd::HlHE; }

uint32_t premul(int r, int g, int b, int a) {
    a = std::clamp(a, 0, 255);
    return (uint32_t(a) << 24) | (uint32_t(r * a / 255) << 16) | (uint32_t(g * a / 255) << 8) | uint32_t(b * a / 255);
}

// klassischer Verlauf (blau → türkis → grün → gelb → rot), Deckkraft steigt mit
uint32_t gradient(float v) {
    struct Stop {
        float v;
        int r, g, b, a;
    };
    static const Stop s[] = {{0.0f, 0, 60, 255, 0},     {0.10f, 0, 90, 255, 45},  {0.30f, 0, 200, 255, 105},
                             {0.50f, 40, 230, 120, 150}, {0.75f, 255, 220, 0, 185}, {1.0f, 255, 40, 30, 215}};
    v = std::clamp(v, 0.0f, 1.0f);
    size_t i = 1;
    while (i + 1 < sizeof s / sizeof s[0] && v > s[i].v) ++i;
    const Stop &a = s[i - 1], &b = s[i];
    float t = (v - a.v) / std::max(1e-6f, b.v - a.v);
    auto mixI = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
    return premul(mixI(a.r, b.r), mixI(a.g, b.g), mixI(a.b, b.b), mixI(a.a, b.a));
}

// eine Farbe je Ebene, Deckkraft nach Dichte (wenn mehrere Ebenen an sind)
uint32_t hue(uint32_t rgbv, float v) {
    return premul(int((rgbv >> 16) & 0xFF), int((rgbv >> 8) & 0xFF), int(rgbv & 0xFF), int(20 + 190 * std::clamp(v, 0.0f, 1.0f)));
}

uint32_t over(uint32_t dst, uint32_t src) {
    uint32_t ia = 255 - (src >> 24);
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t s = (src >> sh) & 0xFF, d = (dst >> sh) & 0xFF;
        out |= std::min<uint32_t>(255, s + d * ia / 255) << sh;
    }
    return out;
}

struct ChipItem {
    std::wstring label;
    bool on;
    std::string key;
    std::function<void()> click;
};

// Chips nebeneinander, bei Bedarf umbrochen; liefert die Höhe
float chipFlow(Canvas& c, float x, float y, float w, const std::vector<ChipItem>& items) {
    float cx = x, cy = y, h = 30;
    for (const auto& it : items) {
        float cw = c.chipRowWidth({it.label});
        if (cx > x && cx + cw > x + w) {
            cx = x;
            cy += h + 6;
        }
        c.chip(RectF(cx, cy, cw, h), it.label, it.on, it.key, it.click);
        cx += cw + 6;
    }
    return cy - y + h;
}

void outlinedText(Canvas& c, const std::wstring& s, F f, const Color& col, const RectF& r) {
    c.text(s, f, Color(210, 0, 0, 0), RectF(r.X + 1, r.Y + 1, r.Width, r.Height));
    c.text(s, f, col, r);
}

}  // namespace

std::wstring heatFile(const std::string& checksum) { return joinPath(matchesDir(), widen(checksum) + L".heat"); }

void HeatView::setMap(const std::string& map) {
    radar = fd::radarMap(map, &radarIndex);
    if (!radar) radarIndex = -1;
}

void HeatView::loadMatch(const fd::Match& m, const std::string& me, const std::string& k) {
    key = k;
    set.reset();
    gridKey_.clear();
    setMap(m.map);
    if (!radar) {
        state = State::NoRadar;
        return;
    }
    std::string bytes, err;
    fd::Heat h;
    if (!readFile(heatFile(m.checksum), bytes, 64u << 20) || !fd::parseHeat(bytes, h, err)) {
        if (!err.empty()) logLine(L"Heatmap %ls unbrauchbar: %ls", widen(m.checksum).c_str(), widen(err).c_str());
        state = State::Missing;
        return;
    }
    auto s = std::make_shared<fd::HeatSet>();
    fd::addMatchHeat(*s, fd::heatMatchInfo(m, me, ""), h, *radar, me);
    if (!s->hasMe && !(filter.who & (fd::WhoTeam | fd::WhoEnemy))) filter.who |= fd::WhoTeam | fd::WhoEnemy;   // ohne „Ich“: beide Teams
    set = std::move(s);
    state = State::Ready;
}

void HeatView::loadMany(HWND hwnd, std::vector<fd::HeatMatchInfo> infos, const std::string& map, const std::string& me,
                        const std::string& k, bool now) {
    key = k;
    set.reset();
    gridKey_.clear();
    setMap(map);
    if (!radar) {
        state = State::NoRadar;
        return;
    }
    state = State::Working;
    auto work = [infos = std::move(infos), map, me, k]() {
        auto* e = new HeatEvent();
        e->key = k;
        e->set = std::make_shared<fd::HeatSet>();
        const fd::RadarMap* rm = fd::radarMap(map);
        if (!rm) return e;
        for (const auto& info : infos) {
            std::string bytes, err;
            fd::Heat h;
            if (!readFile(heatFile(info.checksum), bytes, 64u << 20) || !fd::parseHeat(bytes, h, err)) continue;
            fd::addMatchHeat(*e->set, info, h, *rm, me);
        }
        return e;
    };
    if (now) {
        std::unique_ptr<HeatEvent> e(work());
        apply(e.get());
        return;
    }
    std::thread([hwnd, work = std::move(work)]() {
        HeatEvent* e = work();
        if (!PostMessageW(hwnd, WM_APP_HEAT, 0, reinterpret_cast<LPARAM>(e))) delete e;
    }).detach();
}

bool HeatView::apply(HeatEvent* e) {
    if (!e || e->key != key) return false;
    set = e->set;
    state = State::Ready;
    gridKey_.clear();
    return true;
}

void HeatView::release() {
    images_.release();
    grid_[0].reset();
    grid_[1].reset();
    overlay_[0].reset();
    overlay_[1].reset();
    gridKey_.clear();
    overlayKey_.clear();
}

void HeatView::buildGrids() {
    std::fill(std::begin(counts_), std::end(counts_), 0);
    grid_[0].reset();
    grid_[1].reset();
    if (!set || !radar) return;
    int active = 0;
    for (const auto& li : kLayers)
        if (layers & (1u << li.layer)) ++active;
    std::vector<float> g;
    std::vector<uint32_t> px;
    int levels = radar->lower ? 2 : 1;
    for (int lv = 0; lv < levels; ++lv) {
        px.assign(size_t(kGrid) * kGrid, 0);
        bool any = false;
        for (const auto& li : kLayers) {
            if (!(layers & (1u << li.layer))) continue;
            int n = fd::heatDensity(*set, li.layer, filter, lv == 1, kGrid, li.layer == fd::HlPresence ? 2.2f : 3.0f, g);
            counts_[li.layer] += n;
            if (!n) continue;
            any = true;
            for (size_t i = 0; i < px.size(); ++i)
                if (g[i] >= 0.03f) px[i] = over(px[i], active == 1 ? gradient(g[i]) : hue(li.color, g[i]));
        }
        if (!any) continue;
        auto bmp = std::make_unique<Bitmap>(kGrid, kGrid, PixelFormat32bppPARGB);
        BitmapData bd;
        Rect rc(0, 0, kGrid, kGrid);
        if (bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppPARGB, &bd) == Ok) {
            for (int y = 0; y < kGrid; ++y)
                std::memcpy(static_cast<BYTE*>(bd.Scan0) + y * bd.Stride, &px[size_t(y) * kGrid], size_t(kGrid) * 4);
            bmp->UnlockBits(&bd);
            grid_[lv] = std::move(bmp);
        }
    }
}

void HeatView::buildOverlay(const std::vector<const fd::HeatMark*>& list, int px, float scale, int levels) {
    overlay_[0].reset();
    overlay_[1].reset();
    std::unique_ptr<Graphics> g[2];
    for (int lv = 0; lv < levels; ++lv) {
        overlay_[lv] = std::make_unique<Bitmap>(px, px, PixelFormat32bppPARGB);
        g[lv] = std::make_unique<Graphics>(overlay_[lv].get());
        g[lv]->SetSmoothingMode(SmoothingModeAntiAlias);
        g[lv]->SetPixelOffsetMode(PixelOffsetModeHalf);
        g[lv]->Clear(Color(0, 0, 0, 0));
    }
    auto gr = [&](bool lower) -> Graphics* { return g[lower && levels == 2 ? 1 : 0].get(); };
    auto pt = [&](float u, float v) { return PointF(u * float(px), v * float(px)); };
    const float k = scale;   // Größen in DIP → Pixel
    auto disc = [&](Graphics* gg, PointF p, float r, const Color& col) {
        SolidBrush b(col);
        gg->FillEllipse(&b, p.X - r * k, p.Y - r * k, 2 * r * k, 2 * r * k);
    };
    if (throws) {
        for (const auto* m : list) {
            if (!isNade(m->layer) || !m->thrown) continue;
            Color col = rgb(kLayers[m->layer].color);
            PointF a = pt(m->fu, m->fv);
            if (m->fromLower == m->lower) {
                Pen pen(alpha(col, 90), 1.1f * k);
                gr(m->lower)->DrawLine(&pen, a, pt(m->u, m->v));
            }
            Pen ring(alpha(col, 230), 1.3f * k);
            gr(m->fromLower)->DrawEllipse(&ring, a.X - 2.6f * k, a.Y - 2.6f * k, 5.2f * k, 5.2f * k);
        }
    }
    if (marks) {
        for (const auto* m : list) {
            Graphics* gg = gr(m->lower);
            PointF p = pt(m->u, m->v);
            Color col = rgb(kLayers[m->layer].color);
            if (m->layer == fd::HlDeaths) {
                Pen back(Color(200, 0, 0, 0), 3.6f * k), pen(col, 2.0f * k);
                float d = 3.5f * k;
                for (Pen* pn : {&back, &pen}) {
                    gg->DrawLine(pn, p.X - d, p.Y - d, p.X + d, p.Y + d);
                    gg->DrawLine(pn, p.X + d, p.Y - d, p.X - d, p.Y + d);
                }
            } else {
                disc(gg, p, 4.2f, Color(200, 0, 0, 0));
                disc(gg, p, 3.0f, col);
            }
        }
    }
}

float HeatView::panel(Canvas& c, App& app, float x, float y, float w) {
    (void)app;
    float y0 = y;
    auto title = [&](const wchar_t* t) {
        c.text(t, F::SmallSemi, c.th.text2, RectF(x, y, w, 18));
        y += 24;
    };
    title(L"Zeigen");
    std::vector<ChipItem> items;
    for (const auto& li : kLayers) {
        uint32_t bit = 1u << li.layer;
        items.push_back({li.label, (layers & bit) != 0, "hm-l" + std::to_string(li.layer) + (many_ ? "m" : "s"), [this, bit] { layers ^= bit; }});
    }
    y += chipFlow(c, x, y, w, items) + 16;

    title(L"Spieler");
    items.clear();
    bool me = set && set->hasMe;
    auto who = [&](const wchar_t* label, uint8_t bit) {
        items.push_back({label, (filter.who & bit) != 0, "hm-w" + std::to_string(bit) + (many_ ? "m" : "s"), [this, bit] {
                             if ((filter.who & ~bit) || !(filter.who & bit)) filter.who ^= bit;   // eine Auswahl bleibt immer
                         }});
    };
    if (me) who(L"Ich", fd::WhoMe);
    who(me ? L"Team" : L"Team A", fd::WhoTeam);
    who(me ? L"Gegner" : L"Team B", fd::WhoEnemy);
    y += chipFlow(c, x, y, w, items) + 16;

    title(L"Seite");
    items.clear();
    const std::pair<const wchar_t*, uint8_t> sides[] = {{L"Beide", fd::SideT | fd::SideCT}, {L"T", fd::SideT}, {L"CT", fd::SideCT}};
    for (const auto& s : sides) {
        uint8_t v = s.second;
        items.push_back({s.first, filter.sides == v, "hm-s" + std::to_string(v) + (many_ ? "m" : "s"), [this, v] { filter.sides = v; }});
    }
    y += chipFlow(c, x, y, w, items) + 16;

    // Schalter
    auto toggleRow = [&](const wchar_t* label, const wchar_t* sub, bool on, const std::string& k, std::function<void()> click) {
        c.toggle(RectF(x, y + 2, 40, 22), on, k, click);
        c.text(label, F::Body, c.th.text, RectF(x + 52, y, w - 52, 20));
        c.text(sub, F::Tiny, c.th.text3, RectF(x + 52, y + 19, w - 52, 16));
        y += 44;
    };
    toggleRow(L"Punkte", L"einzeln, Klick öffnet die 2D-Ansicht", marks, many_ ? "hm-pm" : "hm-ps", [this] { marks = !marks; });
    toggleRow(L"Wurfpositionen", L"Linie von der Wurfstelle zur Landung", throws, many_ ? "hm-tm" : "hm-ts", [this] { throws = !throws; });
    y += 4;

    // Legende mit Zahlen
    int active = 0;
    for (const auto& li : kLayers)
        if (layers & (1u << li.layer)) ++active;
    if (active) {
        c.line(x, y, x + w, y, c.th.border, 1);
        y += 12;
        if (active == 1) {   // Verlauf
            RectF bar(x, y + 4, w - 90, 8);
            for (int i = 0; i < 40; ++i) {
                uint32_t p = gradient((i + 0.5f) / 40);
                int a = int(p >> 24);
                Color col = a ? Color(BYTE(a), BYTE(((p >> 16) & 0xFF) * 255 / a), BYTE(((p >> 8) & 0xFF) * 255 / a), BYTE((p & 0xFF) * 255 / a))
                              : Color(0, 0, 0, 0);
                c.fill(RectF(bar.X + bar.Width * i / 40, bar.Y, bar.Width / 40 + 0.5f, bar.Height), col);
            }
            c.text(L"wenig", F::Tiny, c.th.text3, RectF(x, y + 14, 60, 14));
            c.text(L"viel", F::Tiny, c.th.text3, RectF(x + w - 90 - 40, y + 14, 40, 14), Align::Right);
            y += 34;
        }
        for (const auto& li : kLayers) {
            if (!(layers & (1u << li.layer))) continue;
            c.circle(x + 6, y + 9, 5, rgb(li.color));
            int n = counts_[li.layer];
            std::wstring v = li.layer != fd::HlPresence ? widen(fd::fmtInt(n))
                             : n < 120                    ? std::to_wstring(n) + L" s"
                                                          : widen(fd::fmtDuration(n));
            c.text(li.label, F::Small, c.th.text, RectF(x + 18, y, w - 110, 18));
            c.text(v, F::SmallSemi, c.th.text2, RectF(x + w - 100, y, 100, 18), Align::Right);
            y += 22;
        }
    }
    return y - y0;
}

float HeatView::draw(Canvas& c, App& app, float x, float y, float w) {
    float y0 = y;
    if (state == State::Working) {
        return emptyState(c, x, y, w, Icon::Clock, L"Heatmap wird geladen …", L"FragDesk liest die Heatmap-Daten der Matches dieser Map.");
    }
    if (state == State::NoRadar) {
        return emptyState(c, x, y, w, Icon::Map, L"Kein Radarbild für diese Karte",
                          L"Die Heatmap braucht das Radarbild der Karte – FragDesk hat es für alle aktuellen offiziellen Karten.");
    }
    if (state != State::Ready || !set || !radar) return 0;
    std::string gk = std::to_string(layers) + "/" + std::to_string(filter.who) + "/" + std::to_string(filter.sides) + "/" +
                     std::to_string(reinterpret_cast<uintptr_t>(set.get()));
    if (gk != gridKey_) {
        buildGrids();
        gridKey_ = gk;
    }
    bool side = w >= 880;
    float pw = side ? kPanelW : w;
    if (!side) y += panel(c, app, x, y, w) + 16;
    float s = std::min(side ? w - pw - 20 : w, 860.0f);
    int levels = radar->lower ? 2 : 1;
    RectF rects[2];
    float cy = y;
    for (int lv = 0; lv < levels; ++lv) {
        RectF area(x, cy, s, s);
        c.round(area, 12, rgb(0x0E131B));
        RectF rr(area.X + 8, area.Y + 8, s - 16, s - 16);
        rects[lv] = rr;
        drawRadarImage(c, images_, radarIndex, lv == 1, rr);
        if (grid_[lv]) {
            GraphicsState st = c.g.Save();
            c.g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
            c.g.DrawImage(grid_[lv].get(), rr);
            c.g.Restore(st);
        }
        if (levels == 2) outlinedText(c, lv == 0 ? L"Obere Ebene" : L"Untere Ebene", F::SmallSemi, c.th.text, RectF(rr.X + 10, rr.Y + 8, 200, 18));
        cy += s + 12;
    }
    auto at = [&](float u, float v, bool lower) {
        const RectF& r = rects[lower && levels == 2 ? 1 : 0];
        return PointF(r.X + u * r.Width, r.Y + v * r.Height);
    };

    // Wurfpositionen und Punkte: einmal in ein Bild in Bildschirmpixeln gezeichnet, danach nur noch kopiert
    std::vector<const fd::HeatMark*> list;
    if (marks || throws)
        for (const auto& m : set->marks)
            if ((layers & (1u << m.layer)) && filter.match(m.who, m.side) && (marks || (isNade(m.layer) && m.thrown))) list.push_back(&m);
    const int px = int(std::lround(rects[0].Width * c.scale));
    if (list.size() > kMaxDrawn) {
        c.text(L"Zu viele Punkte für die Einzelansicht (" + widen(fd::fmtInt(double(list.size()))) +
                   L") – Auswahl eingrenzen, z. B. nur „Ich“, eine Seite oder eine Granatenart",
               F::Tiny, c.th.text3, RectF(x, cy - 4, s, 16));
    } else if (!list.empty()) {
        std::string ok = gridKey_ + "/" + (marks ? "p" : "") + (throws ? "t" : "") + "/" + std::to_string(px);
        if (ok != overlayKey_) {
            overlayKey_ = ok;
            buildOverlay(list, px, c.scale, levels);
        }
        for (int lv = 0; lv < levels; ++lv) {
            if (!overlay_[lv]) continue;
            GraphicsState st = c.g.Save();
            float dx = std::round((rects[lv].X + c.ox()) * c.scale), dy = std::round((rects[lv].Y + c.oy()) * c.scale);
            c.g.ResetTransform();
            c.g.DrawImage(overlay_[lv].get(), dx, dy, REAL(px), REAL(px));
            c.g.Restore(st);
        }
        // anklickbar (Tooltip, Sprung in die 2D-Ansicht)
        if (marks && list.size() <= kMaxClickable) {
            for (const auto* m : list) {
                if (m->match < 0 || size_t(m->match) >= set->matches.size()) continue;
                PointF p = at(m->u, m->v, m->lower);
                std::string cs = set->matches[size_t(m->match)].checksum;
                int round = m->round, tick = m->tick;
                c.region(RectF(p.X - 5, p.Y - 5, 10, 10), hashId("hm-mark-" + cs + std::to_string(tick) + "-" + std::to_string(m->layer)),
                         [&app, cs, round, tick] { app.openReplayAt(cs, round, tick); }, widen(m->tip) + L"\nKlick: Stelle in der 2D-Ansicht");
            }
        } else if (marks) {
            c.text(L"Zu viele Punkte zum Anklicken – Auswahl eingrenzen (z. B. nur „Ich“ oder eine Seite)", F::Tiny, c.th.text3,
                   RectF(x, cy - 4, s, 16));
        }
    }
    float mapH = cy - y;
    if (side) {
        float ph = panel(c, app, x + w - pw, y, pw);
        return std::max(mapH, ph) + (y - y0);
    }
    return cy - y0;
}

}  // namespace fdw
