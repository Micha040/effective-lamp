// FragDesk – Zeichnen (siehe gfx.h)
#include "gfx.h"

#include <cmath>
#include <mutex>

using namespace Gdiplus;

namespace ui {

Color rgb(uint32_t hex, BYTE a) { return Color(a, BYTE(hex >> 16), BYTE(hex >> 8), BYTE(hex)); }
Color alpha(const Color& c, BYTE a) { return Color(a, c.GetR(), c.GetG(), c.GetB()); }
Color mix(const Color& a, const Color& b, float t) {
    auto m = [t](BYTE x, BYTE y) { return BYTE(std::lround(x + (y - x) * t)); };
    return Color(m(a.GetA(), b.GetA()), m(a.GetR(), b.GetR()), m(a.GetG(), b.GetG()), m(a.GetB(), b.GetB()));
}

Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.bg = rgb(0x0F141B);
        t.side = rgb(0x0B1016);
        t.card = rgb(0x161C25);
        t.card2 = rgb(0x1B222D);
        t.hover = rgb(0x1E2632);
        t.border = rgb(0x242D3A);
        t.text = rgb(0xE9EEF5);
        t.text2 = rgb(0x9AA6B7);
        t.text3 = rgb(0x6C788A);
        t.accent = rgb(0xFFB547);
        t.accentHover = rgb(0xFFC56E);
        t.accentPressed = rgb(0xEFA132);
        t.onAccent = rgb(0x1E1404);
        t.accentText = rgb(0xFFB547);
        t.btn = rgb(0x232B37);
        t.btnHover = rgb(0x2B3542);
        t.btnPressed = rgb(0x1E252F);
        t.success = rgb(0x4ADE80);
        t.danger = rgb(0xF87171);
        t.warning = rgb(0xFBBF24);
        t.track = rgb(0x262F3C);
        t.tSide = rgb(0xEAB45E);
        t.ctSide = rgb(0x6FA8DC);
    } else {
        t.bg = rgb(0xF3F5F9);
        t.side = rgb(0xE9EDF3);
        t.card = rgb(0xFFFFFF);
        t.card2 = rgb(0xF7F9FC);
        t.hover = rgb(0xF0F3F8);
        t.border = rgb(0xE1E6EE);
        t.text = rgb(0x111827);
        t.text2 = rgb(0x536175);
        t.text3 = rgb(0x8592A5);
        t.accent = rgb(0xFFB547);
        t.accentHover = rgb(0xFFC56E);
        t.accentPressed = rgb(0xF0A12F);
        t.onAccent = rgb(0x1E1404);
        t.accentText = rgb(0xB86E00);
        t.btn = rgb(0xEDF0F5);
        t.btnHover = rgb(0xE3E8EF);
        t.btnPressed = rgb(0xD8DEE7);
        t.success = rgb(0x15A34A);
        t.danger = rgb(0xDC2626);
        t.warning = rgb(0xD97706);
        t.track = rgb(0xE5E9F0);
        t.tSide = rgb(0xB7791F);
        t.ctSide = rgb(0x2F6FB0);
    }
    return t;
}

// ---- Schriften ------------------------------------------------------------------------------------------------
namespace {

std::unique_ptr<FontFamily> pickFamily(std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* n : names) {
        auto f = std::make_unique<FontFamily>(n);
        if (f->GetLastStatus() == Ok && f->IsAvailable()) return f;
    }
    return std::unique_ptr<FontFamily>(FontFamily::GenericSansSerif()->Clone());
}

}  // namespace

void Fonts::init() {
    text_ = pickFamily({L"Segoe UI", L"Inter", L"Tahoma", L"Arial"});
    semi_ = pickFamily({L"Segoe UI Semibold", L"Inter SemiBold", L"Inter Semi Bold"});
    WCHAR name[LF_FACESIZE] = {};
    semi_->GetFamilyName(name);
    semiBold_ = !(wcsstr(name, L"Semi") || wcsstr(name, L"semi"));
    if (semiBold_) semi_ = pickFamily({L"Segoe UI", L"Inter", L"Tahoma", L"Arial"});
    int semiStyle = semiBold_ ? FontStyleBold : FontStyleRegular;
    struct Def {
        F f;
        bool semi;
        float size;
    };
    const Def defs[] = {
        {F::Display, true, 26}, {F::H1, true, 20},     {F::H2, true, 16},       {F::H3, true, 14},
        {F::Body, false, 13.5f}, {F::BodySemi, true, 13.5f}, {F::Small, false, 12.5f}, {F::SmallSemi, true, 12.5f},
        {F::Tiny, false, 11.5f}, {F::TinySemi, true, 11.5f}, {F::Num, true, 22}, {F::NumBig, true, 30}, {F::NumHuge, true, 44},
    };
    for (const Def& d : defs)
        fonts_[int(d.f)] = std::make_unique<Font>(d.semi ? semi_.get() : text_.get(), d.size, d.semi ? semiStyle : FontStyleRegular, UnitPixel);
}

// ---- Messcache ------------------------------------------------------------------------------------------------
namespace {

std::mutex gMeasureMu;
std::unordered_map<std::wstring, float>& measureCache() {
    static std::unordered_map<std::wstring, float> c;
    return c;
}

const StringFormat* typographic() {
    static StringFormat* f = [] {
        StringFormat* s = StringFormat::GenericTypographic()->Clone();
        s->SetFormatFlags(s->GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
        return s;
    }();
    return f;
}

}  // namespace

void clearMeasureCache() {
    std::lock_guard<std::mutex> lock(gMeasureMu);
    measureCache().clear();
}

uint32_t hashId(const std::string& key) {
    uint32_t h = 2166136261u;
    for (unsigned char c : key) h = (h ^ c) * 16777619u;
    return h ? h : 1;
}

// ---- Canvas ---------------------------------------------------------------------------------------------------
Canvas::Canvas(Graphics& gr, const Theme& t, Fonts& f, float s, std::vector<Region>& regions, uint32_t hot, uint32_t pressed)
    : g(gr), th(t), fonts(f), scale(s), regions_(regions), hot_(hot), pressed_(pressed) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.ResetTransform();
    g.ScaleTransform(scale, scale);
}

void Canvas::translate(float dx, float dy) {
    ox_ += dx;
    oy_ += dy;
    g.ResetTransform();
    g.ScaleTransform(scale, scale);
    g.TranslateTransform(ox_, oy_);
}

void Canvas::clip(const RectF& r) {
    clipDip_ = RectF(r.X + ox_, r.Y + oy_, r.Width, r.Height);
    clipped_ = true;
    g.SetClip(r);
}

void Canvas::unclip() {
    clipped_ = false;
    g.ResetClip();
}

RECT Canvas::toPx(const RectF& r) const {
    float x0 = r.X + ox_, y0 = r.Y + oy_, x1 = x0 + r.Width, y1 = y0 + r.Height;
    if (clipped_) {
        x0 = std::max(x0, clipDip_.X);
        y0 = std::max(y0, clipDip_.Y);
        x1 = std::min(x1, clipDip_.X + clipDip_.Width);
        y1 = std::min(y1, clipDip_.Y + clipDip_.Height);
    }
    RECT rc;
    rc.left = LONG(std::floor(x0 * scale));
    rc.top = LONG(std::floor(y0 * scale));
    rc.right = LONG(std::ceil(x1 * scale));
    rc.bottom = LONG(std::ceil(y1 * scale));
    return rc;
}

bool Canvas::visible(const RectF& r) const {
    if (!clipped_) return true;
    float y0 = r.Y + oy_, y1 = y0 + r.Height;
    return y1 >= clipDip_.Y && y0 <= clipDip_.Y + clipDip_.Height;
}

void Canvas::fill(const RectF& r, const Color& c) {
    SolidBrush b(c);
    g.FillRectangle(&b, r);
}

static void roundPath(GraphicsPath& p, const RectF& r, float rad) {
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void Canvas::round(const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    roundPath(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void Canvas::roundStroke(const RectF& r, float rad, const Color& c, float w) {
    GraphicsPath p;
    roundPath(p, RectF(r.X + w / 2, r.Y + w / 2, r.Width - w, r.Height - w), rad);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

void Canvas::line(float x1, float y1, float x2, float y2, const Color& c, float w) {
    Pen pen(c, w);
    g.DrawLine(&pen, x1, y1, x2, y2);
}

void Canvas::circle(float cx, float cy, float rad, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
}

void Canvas::ring(float cx, float cy, float rad, const Color& c, float w) {
    Pen pen(c, w);
    g.DrawEllipse(&pen, cx - rad, cy - rad, rad * 2, rad * 2);
}

void Canvas::text(const std::wstring& s, F font, const Color& c, const RectF& r, Align h, bool vcenter, bool wrap) {
    if (s.empty()) return;
    StringFormat fmt(StringFormat::GenericTypographic());
    INT flags = StringFormatFlagsMeasureTrailingSpaces;
    if (!wrap) flags |= StringFormatFlagsNoWrap;
    fmt.SetFormatFlags(flags);
    fmt.SetTrimming(wrap ? StringTrimmingWord : StringTrimmingEllipsisCharacter);
    fmt.SetAlignment(h == Align::Left ? StringAlignmentNear : h == Align::Center ? StringAlignmentCenter : StringAlignmentFar);
    fmt.SetLineAlignment(vcenter ? StringAlignmentCenter : StringAlignmentNear);
    SolidBrush b(c);
    g.DrawString(s.c_str(), INT(s.size()), fonts.get(font), r, &fmt, &b);
}

float Canvas::measure(const std::wstring& s, F font) {
    if (s.empty()) return 0;
    std::wstring key = std::to_wstring(int(font)) + L"\x1f" + s;
    {
        std::lock_guard<std::mutex> lock(gMeasureMu);
        auto it = measureCache().find(key);
        if (it != measureCache().end()) return it->second;
    }
    RectF bounds;
    // ohne Weltmaßstab messen: Ergebnis in DIP
    Matrix m;
    g.GetTransform(&m);
    g.ResetTransform();
    g.MeasureString(s.c_str(), INT(s.size()), fonts.get(font), PointF(0, 0), typographic(), &bounds);
    g.SetTransform(&m);
    std::lock_guard<std::mutex> lock(gMeasureMu);
    if (measureCache().size() > 20000) measureCache().clear();
    measureCache()[key] = bounds.Width;
    return bounds.Width;
}

float Canvas::textHeight(const std::wstring& s, F font, float width) {
    if (s.empty()) return 0;
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetTrimming(StringTrimmingWord);
    RectF bounds;
    Matrix m;
    g.GetTransform(&m);
    g.ResetTransform();
    g.MeasureString(s.c_str(), INT(s.size()), fonts.get(font), RectF(0, 0, width, 10000), &fmt, &bounds);
    g.SetTransform(&m);
    return bounds.Height;
}

void Canvas::dragRegion(const RectF& r, uint32_t id, std::function<void(float, float)> drag, const std::wstring& tipText) {
    RECT rc = toPx(r);
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    // Anteil bezieht sich auf die ganze Fläche, nicht nur den sichtbaren Teil
    RECT full = {LONG((r.X + ox_) * scale), LONG((r.Y + oy_) * scale), LONG((r.X + r.Width + ox_) * scale), LONG((r.Y + r.Height + oy_) * scale)};
    Region reg;
    reg.px = rc;
    reg.id = id;
    reg.tip = tipText;
    reg.hand = true;
    reg.drag = [full, drag](float px, float py) {
        float fx = (px - full.left) / std::max(1.0f, float(full.right - full.left));
        float fy = (py - full.top) / std::max(1.0f, float(full.bottom - full.top));
        drag(std::clamp(fx, 0.0f, 1.0f), std::clamp(fy, 0.0f, 1.0f));
    };
    regions_.push_back(std::move(reg));
}

void Canvas::region(const RectF& r, uint32_t id, std::function<void()> click, const std::wstring& tip, bool hand) {
    RECT rc = toPx(r);
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    Region reg;
    reg.px = rc;
    reg.id = id;
    reg.click = std::move(click);
    reg.tip = tip;
    reg.hand = hand && reg.click != nullptr;
    regions_.push_back(std::move(reg));
}

void Canvas::tip(const RectF& r, const std::wstring& t) {
    if (t.empty()) return;
    char key[96];
    snprintf(key, sizeof key, "tip:%.0f:%.0f:%.0f", r.X + ox_, r.Y + oy_, r.Width);
    region(r, hashId(key), nullptr, t, false);
}

// ---- Symbole --------------------------------------------------------------------------------------------------
void Canvas::icon(Icon ic, const RectF& r, const Color& c, float sw) {
    if (ic == Icon::None) return;
    float k = std::min(r.Width, r.Height) / 24.0f;
    float bx = r.X + (r.Width - 24 * k) / 2, by = r.Y + (r.Height - 24 * k) / 2;
    auto P = [&](float x, float y) { return PointF(bx + x * k, by + y * k); };
    Pen pen(c, sw);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush br(c);
    auto poly = [&](std::initializer_list<PointF> pts, bool closed, bool filled = false) {
        std::vector<PointF> v(pts);
        if (filled) g.FillPolygon(&br, v.data(), INT(v.size()));
        else if (closed) g.DrawPolygon(&pen, v.data(), INT(v.size()));
        else g.DrawLines(&pen, v.data(), INT(v.size()));
    };
    auto ln = [&](float x1, float y1, float x2, float y2) { g.DrawLine(&pen, P(x1, y1), P(x2, y2)); };
    auto circ = [&](float cx, float cy, float rad, bool filled = false) {
        if (filled) g.FillEllipse(&br, bx + (cx - rad) * k, by + (cy - rad) * k, rad * 2 * k, rad * 2 * k);
        else g.DrawEllipse(&pen, bx + (cx - rad) * k, by + (cy - rad) * k, rad * 2 * k, rad * 2 * k);
    };
    auto arc = [&](float cx, float cy, float rad, float start, float sweep) {
        g.DrawArc(&pen, bx + (cx - rad) * k, by + (cy - rad) * k, rad * 2 * k, rad * 2 * k, start, sweep);
    };
    auto rrect = [&](float x, float y, float w, float h, float rad) {
        GraphicsPath p;
        roundPath(p, RectF(bx + x * k, by + y * k, w * k, h * k), rad * k);
        g.DrawPath(&pen, &p);
    };

    switch (ic) {
        case Icon::Home:
            poly({P(3, 11), P(12, 3.5f), P(21, 11)}, false);
            poly({P(5.5f, 9.5f), P(5.5f, 20.5f), P(18.5f, 20.5f), P(18.5f, 9.5f)}, false);
            poly({P(10, 20.5f), P(10, 15), P(14, 15), P(14, 20.5f)}, false);
            break;
        case Icon::List:
            for (float y : {6.f, 12.f, 18.f}) {
                ln(9, y, 20.5f, y);
                circ(4.5f, y, 1.3f, true);
            }
            break;
        case Icon::Stats:
            rrect(3.5f, 12, 4.5f, 8.5f, 1);
            rrect(9.75f, 4, 4.5f, 16.5f, 1);
            rrect(16, 8, 4.5f, 12.5f, 1);
            break;
        case Icon::Film:
            rrect(3, 5, 18, 14, 2.5f);
            poly({P(10, 9), P(15.5f, 12), P(10, 15)}, true, true);
            break;
        case Icon::Gear: {
            std::vector<PointF> pts;
            for (int i = 0; i < 16; ++i) {
                float a = float(i) * 3.14159265f / 8 + 3.14159265f / 16;
                float rad = (i % 2 == 0) ? 9.2f : 7.0f;
                pts.push_back(P(12 + rad * std::cos(a), 12 + rad * std::sin(a)));
            }
            g.DrawPolygon(&pen, pts.data(), INT(pts.size()));
            circ(12, 12, 3);
            break;
        }
        case Icon::Play: poly({P(7, 4.5f), P(19, 12), P(7, 19.5f)}, true, true); break;
        case Icon::Folder: poly({P(3, 6.5f), P(9.5f, 6.5f), P(11.5f, 8.5f), P(21, 8.5f), P(21, 19), P(3, 19)}, true); break;
        case Icon::Plus:
            ln(12, 5, 12, 19);
            ln(5, 12, 19, 12);
            break;
        case Icon::Refresh:
            arc(12, 12, 7.5f, 300, -280);
            poly({P(19.5f, 4), P(19.5f, 8.5f), P(15, 8.5f)}, false);
            break;
        case Icon::Back:
            ln(19.5f, 12, 5, 12);
            poly({P(11, 6), P(5, 12), P(11, 18)}, false);
            break;
        case Icon::Check: poly({P(5, 12.5f), P(10, 17.5f), P(19, 7)}, false); break;
        case Icon::Close:
            ln(6.5f, 6.5f, 17.5f, 17.5f);
            ln(17.5f, 6.5f, 6.5f, 17.5f);
            break;
        case Icon::Info:
            circ(12, 12, 9);
            ln(12, 11, 12, 16.5f);
            circ(12, 7.6f, 1.2f, true);
            break;
        case Icon::Crosshair:
            circ(12, 12, 7);
            ln(12, 2.5f, 12, 7.5f);
            ln(12, 16.5f, 12, 21.5f);
            ln(2.5f, 12, 7.5f, 12);
            ln(16.5f, 12, 21.5f, 12);
            break;
        case Icon::Flash: poly({P(13.5f, 2.5f), P(5, 13.5f), P(11, 13.5f), P(10, 21.5f), P(19, 9.5f), P(13, 9.5f)}, true, true); break;
        case Icon::Bomb:
            circ(10.5f, 14, 6.5f);
            ln(15, 9.5f, 17.5f, 7);
            ln(19, 4, 19.6f, 2.8f);
            ln(20.5f, 6, 21.8f, 5.6f);
            ln(17.5f, 4.5f, 17, 3.2f);
            break;
        case Icon::Trophy: {
            GraphicsPath p;
            p.AddLine(P(7, 4), P(17, 4));
            p.AddLine(P(17, 4), P(17, 9));
            p.AddArc(bx + 7 * k, by + 4 * k, 10 * k, 10 * k, 0, 180);
            p.CloseFigure();
            g.DrawPath(&pen, &p);
            arc(17, 7.5f, 2.6f, -90, 180);
            arc(7, 7.5f, 2.6f, 90, 180);
            ln(12, 14, 12, 18);
            ln(8, 20, 16, 20);
            break;
        }
        case Icon::Clock:
            circ(12, 12, 9);
            poly({P(12, 7), P(12, 12), P(15.5f, 14)}, false);
            break;
        case Icon::Target:
            circ(12, 12, 9);
            circ(12, 12, 5);
            circ(12, 12, 1.5f, true);
            break;
        case Icon::Map:
            poly({P(3, 6), P(9, 4), P(15, 6), P(21, 4), P(21, 18), P(15, 20), P(9, 18), P(3, 20)}, true);
            ln(9, 4, 9, 18);
            ln(15, 6, 15, 20);
            break;
        case Icon::Download:
            ln(12, 4, 12, 15);
            poly({P(7, 10), P(12, 15), P(17, 10)}, false);
            ln(5, 20, 19, 20);
            break;
        case Icon::Warning:
            poly({P(12, 3.5f), P(21.5f, 20), P(2.5f, 20)}, true);
            ln(12, 9.5f, 12, 14);
            circ(12, 17, 1.1f, true);
            break;
        case Icon::Eye: {
            GraphicsPath p;
            p.AddBezier(P(2.5f, 12), P(6, 5.5f), P(18, 5.5f), P(21.5f, 12));
            p.AddBezier(P(21.5f, 12), P(18, 18.5f), P(6, 18.5f), P(2.5f, 12));
            g.DrawPath(&pen, &p);
            circ(12, 12, 3);
            break;
        }
        case Icon::Chevron: poly({P(9, 5.5f), P(15.5f, 12), P(9, 18.5f)}, false); break;
        case Icon::ChevronLeft: poly({P(15, 5.5f), P(8.5f, 12), P(15, 18.5f)}, false); break;
        case Icon::Pause:
            poly({P(6.5f, 5), P(10, 5), P(10, 19), P(6.5f, 19)}, true, true);
            poly({P(14, 5), P(17.5f, 5), P(17.5f, 19), P(14, 19)}, true, true);
            break;
        case Icon::ChevronDown: poly({P(5.5f, 9), P(12, 15.5f), P(18.5f, 9)}, false); break;
        case Icon::ChevronUp: poly({P(5.5f, 15), P(12, 8.5f), P(18.5f, 15)}, false); break;
        case Icon::Star: {
            std::vector<PointF> pts;
            for (int i = 0; i < 10; ++i) {
                float a = -3.14159265f / 2 + float(i) * 3.14159265f / 5;
                float rad = i % 2 == 0 ? 9.5f : 4.2f;
                pts.push_back(P(12 + rad * std::cos(a), 12.5f + rad * std::sin(a)));
            }
            g.FillPolygon(&br, pts.data(), INT(pts.size()));
            break;
        }
        case Icon::External:
            poly({P(11, 5), P(5, 5), P(5, 19), P(19, 19), P(19, 13)}, false);
            poly({P(14, 4), P(20, 4), P(20, 10)}, false);
            ln(20, 4, 11.5f, 12.5f);
            break;
        case Icon::Trash:
            ln(4, 7, 20, 7);
            poly({P(9, 7), P(9, 4.5f), P(15, 4.5f), P(15, 7)}, false);
            poly({P(6.5f, 7), P(7.5f, 20.5f), P(16.5f, 20.5f), P(17.5f, 7)}, false);
            break;
        case Icon::Record: circ(12, 12, 6.5f, true); break;
        case Icon::Users:
            circ(9, 8, 3.4f);
            arc(9, 20, 6.5f, 200, 140);
            circ(16.5f, 9, 2.6f);
            arc(17, 19.5f, 5, 230, 100);
            break;
        case Icon::Shield: {
            GraphicsPath p;
            p.AddLine(P(12, 2.5f), P(20, 5.5f));
            p.AddLine(P(20, 5.5f), P(20, 11.5f));
            p.AddBezier(P(20, 11.5f), P(20, 16.5f), P(16.5f, 19.8f), P(12, 21.5f));
            p.AddBezier(P(12, 21.5f), P(7.5f, 19.8f), P(4, 16.5f), P(4, 11.5f));
            p.AddLine(P(4, 11.5f), P(4, 5.5f));
            p.CloseFigure();
            g.DrawPath(&pen, &p);
            break;
        }
        case Icon::Fire: {
            GraphicsPath p;
            p.AddBezier(P(12, 21.5f), P(6, 21.5f), P(4.5f, 15), P(8.5f, 10.5f));
            p.AddBezier(P(8.5f, 10.5f), P(9, 13), P(10.5f, 13.5f), P(11, 12.5f));
            p.AddBezier(P(11, 12.5f), P(10, 8), P(12.5f, 4.5f), P(14.5f, 2.5f));
            p.AddBezier(P(14.5f, 2.5f), P(15, 7.5f), P(20, 10), P(19, 15.5f));
            p.AddBezier(P(19, 15.5f), P(18.5f, 19.5f), P(15.5f, 21.5f), P(12, 21.5f));
            g.DrawPath(&pen, &p);
            break;
        }
        case Icon::Smoke:
            circ(8, 14, 4.5f);
            circ(15.5f, 13, 5);
            circ(12, 8.5f, 4);
            break;
        case Icon::Coin:
            circ(12, 12, 9);
            ln(12, 6.5f, 12, 17.5f);
            arc(12, 10, 2.8f, 0, -200);
            arc(12, 14, 2.8f, 180, -200);
            break;
        case Icon::Spark:
            poly({P(12, 2.5f), P(14, 10), P(21.5f, 12), P(14, 14), P(12, 21.5f), P(10, 14), P(2.5f, 12), P(10, 10)}, true, true);
            break;
        default: break;
    }
}

// ---- Bausteine ------------------------------------------------------------------------------------------------
float Canvas::buttonWidth(const std::wstring& label, Icon ic) {
    float w = measure(label, F::BodySemi) + 32;
    if (ic != Icon::None) w += label.empty() ? 4 : 22;
    return w;
}

bool Canvas::button(const RectF& r, const std::wstring& label, const std::string& key, std::function<void()> click, Btn style, Icon ic,
                    bool enabled, const std::wstring& tipText) {
    uint32_t id = hashId("btn:" + key);
    bool h = enabled && hot(id), d = enabled && down(id);
    Color bg, fg;
    switch (style) {
        case Btn::Primary:
            bg = d ? th.accentPressed : h ? th.accentHover : th.accent;
            fg = th.onAccent;
            break;
        case Btn::Ghost:
            bg = d ? th.btnPressed : h ? th.btn : alpha(th.btn, 0);
            fg = th.text;
            break;
        case Btn::Danger:
            bg = d ? alpha(th.danger, 70) : h ? alpha(th.danger, 50) : alpha(th.danger, 32);
            fg = th.danger;
            break;
        default:
            bg = d ? th.btnPressed : h ? th.btnHover : th.btn;
            fg = th.text;
    }
    if (!enabled) {
        bg = style == Btn::Ghost ? alpha(th.btn, 0) : th.btn;
        fg = th.text3;
    }
    round(r, std::min(8.0f, r.Height / 2), bg);
    float iconW = ic != Icon::None ? 16 : 0;
    float gap = (ic != Icon::None && !label.empty()) ? 8 : 0;
    float tw = label.empty() ? 0 : measure(label, F::BodySemi);
    float total = iconW + gap + tw;
    float x = r.X + (r.Width - total) / 2;
    if (ic == Icon::Chevron && !label.empty()) {   // „Alle ›“: Pfeil hinter dem Text
        text(label, F::BodySemi, fg, RectF(x, r.Y, tw + 2, r.Height));
        icon(ic, RectF(x + tw + gap, r.Y + (r.Height - 16) / 2, 16, 16), fg, 1.8f);
    } else {
        if (ic != Icon::None) icon(ic, RectF(x, r.Y + (r.Height - 16) / 2, 16, 16), fg, 1.8f);
        if (!label.empty()) text(label, F::BodySemi, fg, RectF(x + iconW + gap, r.Y, tw + 2, r.Height));
    }
    if (enabled) region(r, id, std::move(click), tipText, true);
    else if (!tipText.empty()) tip(r, tipText);
    return h;
}

void Canvas::iconButton(const RectF& r, Icon ic, const std::string& key, std::function<void()> click, const std::wstring& tipText,
                        bool enabled) {
    uint32_t id = hashId("ibtn:" + key);
    bool h = enabled && hot(id), d = enabled && down(id);
    if (h || d) round(r, std::min(8.0f, r.Height / 2), d ? th.btnPressed : th.btnHover);
    float sz = std::min(r.Width, r.Height) * 0.55f;
    icon(ic, RectF(r.X + (r.Width - sz) / 2, r.Y + (r.Height - sz) / 2, sz, sz), enabled ? (h ? th.text : th.text2) : th.text3, 1.8f);
    if (enabled) region(r, id, std::move(click), tipText, true);
    else tip(r, tipText);
}

void Canvas::chip(const RectF& r, const std::wstring& label, bool selected, const std::string& key, std::function<void()> click) {
    uint32_t id = hashId("chip:" + key);
    bool h = hot(id);
    Color bg = selected ? alpha(th.accent, th.dark ? 46 : 70) : h ? th.btnHover : th.btn;
    Color fg = selected ? th.accentText : h ? th.text : th.text2;
    round(r, r.Height / 2, bg);
    text(label, F::SmallSemi, fg, r, Align::Center);
    region(r, id, std::move(click), L"", true);
}

float Canvas::chipRow(float x, float y, const std::vector<std::wstring>& labels, int selected, const std::string& key,
                      std::function<void(int)> pick, float h) {
    float cx = x;
    for (size_t i = 0; i < labels.size(); ++i) {
        float w = measure(labels[i], F::SmallSemi) + 26;
        int idx = int(i);
        chip(RectF(cx, y, w, h), labels[i], idx == selected, key + ":" + std::to_string(i), [pick, idx] { pick(idx); });
        cx += w + 6;
    }
    return cx - x - (labels.empty() ? 0 : 6);
}

float Canvas::chipRowWidth(const std::vector<std::wstring>& labels) {
    float w = 0;
    for (const auto& l : labels) w += measure(l, F::SmallSemi) + 26 + 6;
    return labels.empty() ? 0 : w - 6;
}

float Canvas::tabs(float x, float y, float w, const std::vector<std::wstring>& labels, int selected, const std::string& key,
                   std::function<void(int)> pick) {
    const float h = 42;
    size_t n = labels.size();
    std::vector<float> tw(n);
    float labelsW = 0;
    for (size_t i = 0; i < n; ++i) labelsW += tw[i] = measure(labels[i], F::BodySemi);
    // zu breit: erst enger, dann in mehrere Zeilen
    float pad = 24, gap = 4;
    if (n && labelsW + n * pad + (n - 1) * gap > w) {
        gap = 2;
        pad = std::max(14.0f, (w - labelsW - (n - 1) * gap) / float(n));
    }
    std::vector<int> rowOf(n, 0);
    int rows = 1;
    float cx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (cx > 0 && cx + tw[i] + pad > w) {
            ++rows;
            cx = 0;
        }
        rowOf[i] = rows - 1;
        cx += tw[i] + pad + gap;
    }
    float total = h * float(rows);
    line(x, y + total - 0.5f, x + w, y + total - 0.5f, th.border, 1);
    cx = x;
    int row = 0;
    for (size_t i = 0; i < n; ++i) {
        if (rowOf[i] != row) {
            row = rowOf[i];
            cx = x;
        }
        float ry = y + h * float(row);
        RectF r(cx, ry, tw[i] + pad, h);
        uint32_t id = hashId("tab:" + key + ":" + std::to_string(i));
        bool sel = int(i) == selected, hv = hot(id);
        if (hv && !sel) round(RectF(r.X + 2, r.Y + 5, r.Width - 4, h - 12), 6, th.hover);
        text(labels[i], F::BodySemi, sel ? th.text : hv ? th.text : th.text2, RectF(r.X, r.Y, r.Width, h - 2), Align::Center);
        if (sel) round(RectF(r.X + std::min(10.0f, pad / 2 - 2), ry + h - 3, r.Width - 2 * std::min(10.0f, pad / 2 - 2), 3), 1.5f, th.accent);
        int idx = int(i);
        region(r, id, [pick, idx] { pick(idx); }, L"", true);
        cx += r.Width + gap;
    }
    return total;
}

void Canvas::card(const RectF& r, float rad) {
    round(r, rad, th.card);
    roundStroke(r, rad, th.border, 1);
}

void Canvas::progress(const RectF& r, double p, const Color& c) {
    round(r, r.Height / 2, th.track);
    p = std::max(0.0, std::min(1.0, p));
    if (p > 0) round(RectF(r.X, r.Y, std::max(r.Height, float(r.Width * p)), r.Height), r.Height / 2, c);
}

void Canvas::toggle(const RectF& r, bool on, const std::string& key, std::function<void()> click) {
    uint32_t id = hashId("toggle:" + key);
    bool h = hot(id);
    Color bg = on ? (h ? th.accentHover : th.accent) : (h ? th.btnHover : th.track);
    round(r, r.Height / 2, bg);
    float d = r.Height - 6;
    float x = on ? r.X + r.Width - d - 3 : r.X + 3;
    circle(x + d / 2, r.Y + r.Height / 2, d / 2, on ? th.onAccent : th.text2);
    region(r, id, std::move(click), L"", true);
}

float Canvas::pillWidth(const std::wstring& label, F font) { return measure(label, font) + 14; }

void Canvas::pill(const RectF& r, const std::wstring& label, const Color& bg, const Color& fg, F font) {
    round(r, r.Height / 2, bg);
    text(label, font, fg, r, Align::Center);
}

}  // namespace ui
