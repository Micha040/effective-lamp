// FragDesk – Bausteine (siehe widgets.h)
#include "widgets.h"

#include <algorithm>

#include "../core/format.h"
#include "common.h"

using namespace Gdiplus;

namespace ui {

static bool hasColor(const Color& c) { return c.GetA() != 0; }

// ---- Tabelle --------------------------------------------------------------------------------------------------
void sortRows(std::vector<Row>& rows, int col, bool desc) {
    if (col < 0) return;
    std::stable_sort(rows.begin(), rows.end(), [col, desc](const Row& a, const Row& b) {
        if (col >= int(a.cells.size()) || col >= int(b.cells.size())) return false;
        const Cell& x = a.cells[col];
        const Cell& y = b.cells[col];
        bool xn = std::isnan(x.sort), yn = std::isnan(y.sort);
        if (!xn || !yn) {
            if (xn != yn) return yn;   // Werte vor leeren Zellen
            return desc ? x.sort > y.sort : x.sort < y.sort;
        }
        int c = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, x.text.c_str(), -1, y.text.c_str(), -1) - 2;
        return desc ? c > 0 : c < 0;
    });
}

float table(Canvas& c, float x, float y, float w, const std::vector<Col>& cols, const std::vector<Row>& rows, const TableOpts& o) {
    // Spaltenbreiten: feste Spalten mindestens so breit wie ihr Titel (samt Platz für den Sortierpfeil),
    // damit bei anderen Schriften (oder Skalierung) kein Kopf abgeschnitten wird – das geht vom flexiblen Rest ab
    std::vector<float> fixedW(cols.size(), 0);
    float fixed = 0, flex = 0;
    for (size_t i = 0; i < cols.size(); ++i) {
        const Col& col = cols[i];
        if (col.width > 0) {
            float need = col.title.empty() ? 0 : c.measure(col.title, F::TinySemi) + 14 + (o.onSort ? 12 : 0);
            fixedW[i] = std::max(col.width, need);
            fixed += fixedW[i];
        } else {
            flex += -col.width;
        }
    }
    const float pad = 12;
    float rest = std::max(0.0f, w - fixed - pad * 2);
    std::vector<float> xs, ws;
    float cx = x + pad;
    for (size_t i = 0; i < cols.size(); ++i) {
        float cw = cols[i].width > 0 ? fixedW[i] : (flex > 0 ? rest * (-cols[i].width) / flex : 0);
        xs.push_back(cx);
        ws.push_back(cw);
        cx += cw;
    }
    float cy = y;
    if (o.header) {
        for (size_t i = 0; i < cols.size(); ++i) {
            RectF hr(xs[i], cy, ws[i], o.headH);
            std::wstring title = cols[i].title;
            bool sorted = o.onSort && int(i) == o.sortCol;
            Color col = sorted ? c.th.text : c.th.text3;
            float tx = xs[i] + 6, tw = ws[i] - 12;
            // Sortierpfeil immer hinter dem Titel; rechtsbündige Titel rücken dafür etwas nach links
            bool right = cols[i].align == Align::Right;
            c.text(title, F::TinySemi, col, RectF(tx, cy, sorted && right ? tw - 12 : tw, o.headH), cols[i].align);
            if (sorted) {
                float mw = c.measure(title, F::TinySemi);
                float ax = right ? tx + tw - 10 : cols[i].align == Align::Center ? tx + (tw + mw) / 2 + 2 : tx + mw + 3;
                c.icon(o.sortDesc ? Icon::ChevronDown : Icon::ChevronUp, RectF(ax, cy + o.headH / 2 - 5, 10, 10), col, 1.6f);
            }
            if (o.onSort && !cols[i].title.empty()) {
                int idx = int(i);
                auto fn = o.onSort;
                c.region(hr, hashId("th:" + o.key + ":" + std::to_string(i)), [fn, idx] { fn(idx); }, cols[i].tip, true);
            } else if (!cols[i].tip.empty()) c.tip(hr, cols[i].tip);
        }
        cy += o.headH;
        c.line(x, cy - 0.5f, x + w, cy - 0.5f, c.th.border, 1);
    }
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const Row& row = rows[ri];
        RectF rr(x, cy, w, o.rowH);
        if (!c.visible(rr)) {
            cy += o.rowH;
            continue;
        }
        uint32_t id = row.click ? hashId("tr:" + o.key + ":" + (row.key.empty() ? std::to_string(ri) : row.key)) : 0;
        if (row.highlight) c.round(RectF(x + 2, cy + 2, w - 4, o.rowH - 4), 8, alpha(c.th.accent, c.th.dark ? 22 : 36));
        else if (c.hot(id)) c.round(RectF(x + 2, cy + 2, w - 4, o.rowH - 4), 8, c.th.hover);
        if (hasColor(row.accentBar)) c.round(RectF(x + 3, cy + 9, 3, o.rowH - 18), 1.5f, row.accentBar);
        for (size_t i = 0; i < cols.size() && i < row.cells.size(); ++i) {
            const Cell& cell = row.cells[i];
            float tx = xs[i] + 6, tw = ws[i] - 12;
            Color col = hasColor(cell.color) ? cell.color : c.th.text;
            float lineH = cell.sub.empty() ? o.rowH : o.rowH * 0.58f;
            if (hasColor(cell.pill)) {
                float pw = c.pillWidth(cell.text);
                float pxx = cols[i].align == Align::Right ? tx + tw - pw : cols[i].align == Align::Center ? tx + (tw - pw) / 2 : tx;
                c.pill(RectF(pxx, cy + (o.rowH - 22) / 2, pw, 22), cell.text, cell.pill, col);
            } else {
                float ix = tx;
                if (hasColor(cell.dot)) {
                    c.round(RectF(tx, cy + lineH / 2 - 5 + (cell.sub.empty() ? 0 : 2), 10, 10), 3, cell.dot);
                    ix += 18;
                }
                if (cell.icon != Icon::None) {
                    c.icon(cell.icon, RectF(ix, cy + lineH / 2 - 8 + (cell.sub.empty() ? 0 : 2), 16, 16),
                           hasColor(cell.iconColor) ? cell.iconColor : c.th.text2, 1.7f);
                    ix += 22;
                }
                c.text(cell.text, cell.font, col, RectF(ix, cy + (cell.sub.empty() ? 0 : 3), tx + tw - ix, lineH), cols[i].align);
                if (!cell.sub.empty())
                    c.text(cell.sub, F::Tiny, c.th.text3, RectF(ix, cy + lineH - 2, tx + tw - ix, o.rowH - lineH - 4), cols[i].align);
            }
            if (!cell.tip.empty()) c.tip(RectF(xs[i], cy, ws[i], o.rowH), cell.tip);
        }
        if (row.click) c.region(rr, id, row.click, L"", true);
        cy += o.rowH;
        if (ri + 1 < rows.size() && !row.highlight) c.line(x + pad, cy - 0.5f, x + w - pad, cy - 0.5f, alpha(c.th.border, 150), 1);
    }
    return cy - y;
}

// ---- Kennzahlen -----------------------------------------------------------------------------------------------
Color ratingColor(const Theme& th, double r) {
    if (std::isnan(r)) return th.text3;
    if (r >= 1.10) return th.success;
    if (r < 0.90) return th.danger;
    return th.text;
}

void statCard(Canvas& c, const RectF& r, const std::wstring& label, const std::wstring& value, const std::wstring& sub, const std::wstring& tip,
              Color valueColor, Icon ic) {
    c.card(r);
    float x = r.X + 16;
    if (ic != Icon::None) c.icon(ic, RectF(r.X + r.Width - 34, r.Y + 14, 18, 18), c.th.text3, 1.7f);
    c.text(label, F::SmallSemi, c.th.text2, RectF(x, r.Y + 12, r.Width - 32 - (ic != Icon::None ? 20 : 0), 18));
    c.text(value, F::Num, hasColor(valueColor) ? valueColor : c.th.text, RectF(x, r.Y + 32, r.Width - 32, 32));
    if (!sub.empty()) c.text(sub, F::Tiny, c.th.text3, RectF(x, r.Y + r.Height - 26, r.Width - 32, 16));
    if (!tip.empty()) c.tip(r, tip);
}

float statTable(Canvas& c, float x, float y, float w, const std::vector<const char*>& ids, const fd::Counters& all, const fd::Counters& t,
                const fd::Counters& ct, bool sides, const std::string& key) {
    const float rowH = 34;
    float valW = sides ? 104 : 120;
    float labelW = w - (sides ? valW * 3 : valW) - 24;
    float cy = y;
    // Kopf
    c.text(L"Kennzahl", F::TinySemi, c.th.text3, RectF(x + 12, cy, labelW, 28));
    float vx = x + 12 + labelW;
    c.text(L"Gesamt", F::TinySemi, c.th.text3, RectF(vx, cy, valW, 28), Align::Right);
    if (sides) {
        c.text(L"als T", F::TinySemi, c.th.tSide, RectF(vx + valW, cy, valW, 28), Align::Right);
        c.text(L"als CT", F::TinySemi, c.th.ctSide, RectF(vx + valW * 2, cy, valW, 28), Align::Right);
    }
    cy += 28;
    c.line(x, cy - 0.5f, x + w, cy - 0.5f, c.th.border, 1);
    for (size_t i = 0; i < ids.size(); ++i) {
        const fd::Stat* s = fd::findStat(ids[i]);
        if (!s) continue;
        RectF rr(x, cy, w, rowH);
        uint32_t id = hashId("stat:" + key + ":" + ids[i]);
        if (c.hot(id)) c.round(RectF(x + 2, cy + 2, w - 4, rowH - 4), 7, c.th.hover);
        std::wstring label = fdw::widen(s->def->label);
        c.text(label, F::Body, c.th.text, RectF(x + 12, cy, labelW, rowH));
        if (*s->def->hint) {
            float lw = c.measure(label, F::Body);
            if (lw + 22 < labelW) c.icon(Icon::Info, RectF(x + 12 + lw + 7, cy + rowH / 2 - 6.5f, 13, 13), c.th.text3, 1.4f);
        }
        auto val = [&](const fd::Counters& cnt) { return fdw::widen(fd::formatStat(s->expr.eval(cnt), s->def->fmt)); };
        c.text(val(all), F::BodySemi, c.th.text, RectF(vx, cy, valW, rowH), Align::Right);
        if (sides) {
            bool whole = fd::statSideless(*s);   // nur fürs ganze Match
            c.text(whole ? L"–" : val(t), F::Body, whole ? c.th.text3 : c.th.text2, RectF(vx + valW, cy, valW, rowH), Align::Right);
            c.text(whole ? L"–" : val(ct), F::Body, whole ? c.th.text3 : c.th.text2, RectF(vx + valW * 2, cy, valW, rowH), Align::Right);
        }
        c.region(rr, id, nullptr, fdw::widen(s->def->hint), false);
        cy += rowH;
        if (i + 1 < ids.size()) c.line(x + 12, cy - 0.5f, x + w - 12, cy - 0.5f, alpha(c.th.border, 140), 1);
    }
    return cy - y;
}

// ---- Liniendiagramm -------------------------------------------------------------------------------------------
static double niceStep(double range, int target) {
    if (range <= 0) return 1;
    double raw = range / std::max(1, target);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double n = raw / mag;
    double step = n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10;
    return step * mag;
}

void lineChart(Canvas& c, const RectF& r, const std::vector<ChartPoint>& pts, const LineChartOpts& o) {
    if (pts.empty()) {
        c.text(L"Noch keine Daten", F::Small, c.th.text3, r, Align::Center);
        return;
    }
    double lo = 1e300, hi = -1e300;
    for (const auto& p : pts)
        if (!std::isnan(p.y)) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
        }
    if (lo > hi) {
        c.text(L"Noch keine Daten", F::Small, c.th.text3, r, Align::Center);
        return;
    }
    if (!std::isnan(o.ref)) {
        lo = std::min(lo, o.ref);
        hi = std::max(hi, o.ref);
    }
    if (!std::isnan(o.yMin)) lo = std::min(lo, o.yMin);
    if (!std::isnan(o.yMax)) hi = std::max(hi, o.yMax);
    double span = hi - lo;
    if (span <= 0) span = std::max(1.0, std::fabs(hi) * 0.1);
    double step = niceStep(span * 1.15, 4);
    double y0 = std::floor((lo - span * 0.08) / step) * step, y1 = std::ceil((hi + span * 0.08) / step) * step;
    if (lo >= 0 && y0 < 0) y0 = 0;   // nichts Negatives anzeigen, wenn es keine negativen Werte gibt
    if (!std::isnan(o.yCap) && hi <= o.yCap && y1 > o.yCap) y1 = o.yCap;
    if (y1 - y0 < step) y1 = y0 + step;

    const float axisW = 54, bottom = 22;
    RectF plot(r.X + axisW, r.Y + 6, r.Width - axisW - 8, r.Height - bottom - 6);
    auto Y = [&](double v) { return float(plot.Y + plot.Height - (v - y0) / (y1 - y0) * plot.Height); };
    size_t n = pts.size();
    auto X = [&](size_t i) { return n == 1 ? plot.X + plot.Width / 2 : float(plot.X + plot.Width * double(i) / double(n - 1)); };

    // Farbbänder
    for (size_t b = 0; b < o.bands.size(); ++b) {
        double from = o.bands[b].first, to = b + 1 < o.bands.size() ? o.bands[b + 1].first : 1e18;
        if (to <= y0 || from >= y1) continue;
        float top = Y(std::min(to, y1)), bot = Y(std::max(from, y0));
        c.fill(RectF(plot.X, top, plot.Width, bot - top), alpha(o.bands[b].second, c.th.dark ? 20 : 26));
    }
    // Raster
    for (double v = y0; v <= y1 + step * 0.01; v += step) {
        float yy = Y(v);
        c.line(plot.X, yy, plot.X + plot.Width, yy, alpha(c.th.border, 170), 1);
        std::wstring lbl = o.fmtY ? o.fmtY(v) : fdw::widen(fd::fmtDec(v, step < 1 ? 2 : 0));
        c.text(lbl, F::Tiny, c.th.text3, RectF(r.X, yy - 9, axisW - 8, 18), Align::Right);
    }
    if (!std::isnan(o.ref)) {
        float yy = Y(o.ref);
        Pen pen(alpha(c.th.text2, 160), 1.2f);
        REAL dash[] = {4, 4};
        pen.SetDashPattern(dash, 2);
        c.g.DrawLine(&pen, plot.X, yy, plot.X + plot.Width, yy);
    }
    Color lineCol = hasColor(o.line) ? o.line : c.th.accent;
    // Fläche
    std::vector<PointF> poly;
    for (size_t i = 0; i < n; ++i)
        if (!std::isnan(pts[i].y)) poly.push_back(PointF(X(i), Y(pts[i].y)));
    if (o.area && poly.size() >= 2) {
        std::vector<PointF> area = poly;
        area.push_back(PointF(poly.back().X, plot.Y + plot.Height));
        area.push_back(PointF(poly.front().X, plot.Y + plot.Height));
        LinearGradientBrush grad(PointF(0, plot.Y), PointF(0, plot.Y + plot.Height), alpha(lineCol, c.th.dark ? 70 : 60), alpha(lineCol, 0));
        c.g.FillPolygon(&grad, area.data(), INT(area.size()));
    }
    if (poly.size() >= 2) {
        Pen pen(lineCol, 2.2f);
        pen.SetLineJoin(LineJoinRound);
        c.g.DrawLines(&pen, poly.data(), INT(poly.size()));
    }
    // gleitender Durchschnitt
    if (o.movingAvg > 1 && n >= size_t(o.movingAvg)) {
        std::vector<PointF> avg;
        for (size_t i = 0; i < n; ++i) {
            double sum = 0;
            int cnt = 0;
            for (size_t j = (i + 1 >= size_t(o.movingAvg) ? i + 1 - o.movingAvg : 0); j <= i; ++j)
                if (!std::isnan(pts[j].y)) {
                    sum += pts[j].y;
                    ++cnt;
                }
            if (cnt) avg.push_back(PointF(X(i), Y(sum / cnt)));
        }
        if (avg.size() >= 2) {
            Pen pen(alpha(c.th.text, 150), 1.6f);
            REAL dash[] = {3, 3};
            pen.SetDashPattern(dash, 2);
            c.g.DrawLines(&pen, avg.data(), INT(avg.size()));
        }
    }
    // Punkte und Beschriftung
    float labelEvery = std::max(1.0f, std::ceil(float(n) * 64.0f / std::max(1.0f, plot.Width)));
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(pts[i].y)) continue;
        float px = X(i), py = Y(pts[i].y);
        uint32_t id = hashId("pt:" + o.key + ":" + std::to_string(i));
        Color pc = o.pointColor ? o.pointColor(pts[i].y) : lineCol;
        bool h = c.hot(id);
        float rad = n > 60 ? (h ? 4.5f : 2.2f) : (h ? 5.5f : 3.6f);
        c.circle(px, py, rad + 1.6f, c.th.card);
        c.circle(px, py, rad, pc);
        float hw = std::max(6.0f, std::min(14.0f, plot.Width / float(std::max<size_t>(1, n)) / 2));
        c.region(RectF(px - hw, plot.Y, hw * 2, plot.Height), id, pts[i].click, pts[i].tip, pts[i].click != nullptr);
        // Beschriftung: regelmäßig und immer am Ende, aber nie zu dicht vor der letzten
        if (!pts[i].label.empty() && (i % size_t(labelEvery) == 0 || i + 1 == n) && (i + 1 == n || X(n - 1) - px >= 60))
            c.text(pts[i].label, F::Tiny, c.th.text3, RectF(px - 40, plot.Y + plot.Height + 4, 80, 16), Align::Center);
    }
}

// ---- Balken ---------------------------------------------------------------------------------------------------
float barList(Canvas& c, float x, float y, float w, const std::vector<Bar>& bars, double maxV, float rowH, float labelW) {
    float cy = y;
    const float valW = 92;
    for (size_t i = 0; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        RectF rr(x, cy, w, rowH);
        uint32_t id = hashId("bar:" + std::to_string(int(x)) + ":" + std::to_string(int(y)) + ":" + std::to_string(i));
        if (c.hot(id)) c.round(RectF(x - 6, cy + 1, w + 12, rowH - 2), 7, c.th.hover);
        c.text(b.label, F::Body, c.th.text, RectF(x, cy, labelW - 8, b.sub.empty() ? rowH : rowH * 0.62f));
        if (!b.sub.empty()) c.text(b.sub, F::Tiny, c.th.text3, RectF(x, cy + rowH * 0.52f, labelW - 8, rowH * 0.42f), Align::Left, false);
        float bx = x + labelW, bw = w - labelW - valW;
        float bh = 8;
        RectF track(bx, cy + (rowH - bh) / 2, bw, bh);
        c.round(track, bh / 2, c.th.track);
        double frac = maxV > 0 ? std::max(0.0, std::min(1.0, b.v / maxV)) : 0;
        if (frac > 0) c.round(RectF(bx, track.Y, std::max(bh, float(bw * frac)), bh), bh / 2, hasColor(b.color) ? b.color : c.th.accent);
        c.text(b.value, F::BodySemi, c.th.text, RectF(bx + bw, cy, valW, rowH), Align::Right);
        c.region(rr, id, b.click, b.tip, b.click != nullptr);
        cy += rowH;
    }
    return cy - y;
}

// ---- Trefferbild ----------------------------------------------------------------------------------------------
void hitFigure(Canvas& c, const RectF& r, double head, double neck, double chest, double stomach, double arms, double legs) {
    // Figur 100 x 200 Einheiten, mittig in r
    float k = std::min(r.Width * 0.45f / 100.0f, r.Height / 210.0f);
    float fx = r.X + 16, fy = r.Y + (r.Height - 200 * k) / 2;
    auto R = [&](float x, float y, float w, float h) { return RectF(fx + x * k, fy + y * k, w * k, h * k); };
    double mx = std::max({head, neck, chest, stomach, arms, legs, 1.0});
    auto col = [&](double v) {
        float t = float(std::isnan(v) ? 0 : v / mx);
        return mix(c.th.track, c.th.accent, 0.15f + 0.85f * t);
    };
    // Kopf
    {
        SolidBrush b(col(head));
        RectF h = R(36, 0, 28, 30);
        c.g.FillEllipse(&b, h);
    }
    c.round(R(44, 30, 12, 8), 2 * k, col(neck));
    c.round(R(28, 39, 44, 42), 10 * k, col(chest));
    c.round(R(30, 83, 40, 34), 8 * k, col(stomach));
    c.round(R(10, 41, 15, 70), 7 * k, col(arms));
    c.round(R(75, 41, 15, 70), 7 * k, col(arms));
    c.round(R(30, 119, 18, 80), 8 * k, col(legs));
    c.round(R(52, 119, 18, 80), 8 * k, col(legs));
    // Beschriftung rechts
    struct L {
        const wchar_t* name;
        double v;
        float y;
    };
    L labels[] = {{L"Kopf", head, 15}, {L"Hals", neck, 34}, {L"Brust", chest, 60}, {L"Bauch", stomach, 100}, {L"Arme", arms, 76}, {L"Beine", legs, 160}};
    float lx = fx + 110 * k;
    std::sort(std::begin(labels), std::end(labels), [](const L& a, const L& b) { return a.y < b.y; });
    float lastY = -1e9f;
    for (const auto& l : labels) {
        float yy = std::max(fy + l.y * k - 10, lastY + 22);
        lastY = yy;
        c.line(fx + 92 * k, fy + l.y * k, lx - 6, yy + 10, alpha(c.th.text3, 120), 1);
        c.text(l.name, F::Small, c.th.text2, RectF(lx, yy, 70, 20));
        c.text(fdw::widen(fd::fmtPct(l.v, 1)), F::BodySemi, c.th.text, RectF(lx + 64, yy, 80, 20));
    }
}

// ---- Rundenleiste ---------------------------------------------------------------------------------------------
void roundStrip(Canvas& c, const RectF& r, const std::vector<RoundMark>& rounds, const std::string& key) {
    if (rounds.empty()) return;
    size_t n = rounds.size();
    int halves = 0;
    for (const auto& m : rounds)
        if (m.half) ++halves;
    float gap = 3, halfGap = 10;
    float cw = (r.Width - gap * (n - 1) - halfGap * halves) / float(n);
    cw = std::min(cw, 34.0f);
    float x = r.X;
    for (size_t i = 0; i < n; ++i) {
        const RoundMark& m = rounds[i];
        if (m.half) x += halfGap;
        RectF cell(x, r.Y, cw, r.Height);
        uint32_t id = hashId("rs:" + key + ":" + std::to_string(i));
        Color base = m.won ? c.th.success : c.th.danger;
        c.round(cell, 5, alpha(base, c.hot(id) ? 90 : (c.th.dark ? 46 : 60)));
        c.round(RectF(cell.X, cell.Y + cell.Height - 4, cell.Width, 4), 2, alpha(m.side == "T" ? c.th.tSide : c.th.ctSide, 200));
        // Kills als Punkte: bis 3 in einer Reihe, 4 und 5 in zwei Reihen (3 + Rest)
        int k = std::min(m.kills, 5);
        float dr = std::min(3.2f, cw / 9);
        for (int row = 0, done = 0; done < k; ++row) {
            int inRow = std::min(3, k - done);
            float total = inRow * dr * 2 + (inRow - 1) * 2;
            float dx = cell.X + (cw - total) / 2 + dr;
            for (int j = 0; j < inRow; ++j) {
                c.circle(dx, cell.Y + 11 + row * (dr * 2 + 2), dr, c.th.text);
                dx += dr * 2 + 2;
            }
            done += inRow;
        }
        if (m.clutchWon) c.icon(Icon::Star, RectF(cell.X + (cw - 12) / 2, cell.Y + cell.Height - 20, 12, 12), c.th.accent);
        else if (m.dead) c.icon(Icon::Close, RectF(cell.X + (cw - 10) / 2, cell.Y + cell.Height - 19, 10, 10), alpha(c.th.text2, 200), 1.6f);
        c.text(std::to_wstring(m.n), F::Tiny, c.th.text3, RectF(cell.X - 4, cell.Y + cell.Height + 2, cw + 8, 14), Align::Center);
        c.region(RectF(cell.X, cell.Y, cell.Width, cell.Height + 16), id, m.click, m.tip, m.click != nullptr);
        x += cw + gap;
    }
}

float emptyState(Canvas& c, float x, float y, float w, Icon ic, const std::wstring& title, const std::wstring& text) {
    float cy = y + 28;
    c.circle(x + w / 2, cy + 26, 30, c.th.btn);
    c.icon(ic, RectF(x + w / 2 - 15, cy + 11, 30, 30), c.th.text2, 1.8f);
    cy += 72;
    c.text(title, F::H2, c.th.text, RectF(x, cy, w, 26), Align::Center);
    cy += 30;
    float tw = std::min(w - 40, 520.0f);
    float th = c.textHeight(text, F::Body, tw) + 4;
    c.text(text, F::Body, c.th.text2, RectF(x + (w - tw) / 2, cy, tw, th), Align::Center, false, true);
    return cy + th + 20 - y;
}

float sectionTitle(Canvas& c, float x, float y, float w, const std::wstring& title, const std::wstring& sub) {
    c.text(title, F::H2, c.th.text, RectF(x, y, w, 24));
    if (!sub.empty()) {
        c.text(sub, F::Small, c.th.text3, RectF(x, y + 24, w, 18));
        return 48;
    }
    return 32;
}

}  // namespace ui
