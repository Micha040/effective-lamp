// FragDesk – Zeichnen mit GDI+: Farben, Schriften, Symbole und eine kleine Sofort-Oberfläche
// (jede Zeichnung meldet ihre klickbaren Flächen; Eingaben werden außerhalb des Zeichnens ausgeführt).
#pragma once
#include <windows.h>
// gdiplus.h braucht min/max
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

using Gdiplus::Color;
using Gdiplus::RectF;

Color rgb(uint32_t hex, BYTE a = 255);
Color alpha(const Color& c, BYTE a);
Color mix(const Color& a, const Color& b, float t);

struct Theme {
    bool dark = true;
    Color bg, side, card, card2, hover, border, text, text2, text3;
    Color accent, accentHover, accentPressed, onAccent, accentText;
    Color btn, btnHover, btnPressed, success, danger, warning, track, tSide, ctSide;
};
Theme makeTheme(bool dark);

enum class F { Display, H1, H2, H3, Body, BodySemi, Small, SmallSemi, Tiny, TinySemi, Num, NumBig, NumHuge, Count };

class Fonts {
public:
    void init();
    Gdiplus::Font* get(F f) { return fonts_[int(f)].get(); }

private:
    std::unique_ptr<Gdiplus::FontFamily> text_, semi_;
    bool semiBold_ = false;
    std::unique_ptr<Gdiplus::Font> fonts_[int(F::Count)];
};

enum class Icon {
    None, Home, List, Stats, Film, Gear, Play, Folder, Plus, Refresh, Back, Check, Close, Info, Crosshair, Flash, Bomb,
    Trophy, Clock, Target, Map, Download, Warning, Eye, Chevron, ChevronDown, ChevronUp, Star, External, Trash, Record, Users, Shield,
    Fire, Smoke, Coin, Spark
};

enum class Align { Left, Center, Right };

struct Region {
    RECT px{};          // Client-Pixel
    uint32_t id = 0;
    std::function<void()> click;
    std::wstring tip;
    bool hand = false;
};

uint32_t hashId(const std::string& key);

enum class Btn { Secondary, Primary, Ghost, Danger };

class Canvas {
public:
    Canvas(Gdiplus::Graphics& g, const Theme& th, Fonts& fonts, float scale, std::vector<Region>& regions, uint32_t hot,
           uint32_t pressed);

    Gdiplus::Graphics& g;
    const Theme& th;
    Fonts& fonts;
    float scale;

    // Verschiebung für Scrollbereiche (in DIP) und Schnittfläche für Klickflächen
    void translate(float dx, float dy);
    void clip(const RectF& r);   // in aktuellen Koordinaten
    void unclip();
    float ox() const { return ox_; }
    float oy() const { return oy_; }

    // Zeichnen (alle Maße in DIP)
    void fill(const RectF& r, const Color& c);
    void round(const RectF& r, float rad, const Color& c);
    void roundStroke(const RectF& r, float rad, const Color& c, float w = 1);
    void line(float x1, float y1, float x2, float y2, const Color& c, float w = 1);
    void circle(float cx, float cy, float rad, const Color& c);
    void ring(float cx, float cy, float rad, const Color& c, float w);
    void text(const std::wstring& s, F font, const Color& c, const RectF& r, Align h = Align::Left, bool vcenter = true,
              bool wrap = false);
    float measure(const std::wstring& s, F font);
    float textHeight(const std::wstring& s, F font, float width);   // umbrochen
    void icon(Icon i, const RectF& r, const Color& c, float stroke = 1.6f);

    // Klickflächen
    bool hot(uint32_t id) const { return id && id == hot_; }
    bool down(uint32_t id) const { return id && id == pressed_ && id == hot_; }
    void region(const RectF& r, uint32_t id, std::function<void()> click, const std::wstring& tip = L"", bool hand = true);
    void tip(const RectF& r, const std::wstring& tip);
    bool visible(const RectF& r) const;   // liegt (teilweise) in der Schnittfläche?

    // Bausteine
    bool button(const RectF& r, const std::wstring& label, const std::string& key, std::function<void()> click, Btn style = Btn::Secondary,
                Icon icon = Icon::None, bool enabled = true, const std::wstring& tip = L"");
    void iconButton(const RectF& r, Icon icon, const std::string& key, std::function<void()> click, const std::wstring& tip,
                    bool enabled = true);
    float buttonWidth(const std::wstring& label, Icon icon = Icon::None);
    void chip(const RectF& r, const std::wstring& label, bool selected, const std::string& key, std::function<void()> click);
    float chipRow(float x, float y, const std::vector<std::wstring>& labels, int selected, const std::string& key,
                  std::function<void(int)> pick, float h = 30);   // liefert die Breite
    float chipRowWidth(const std::vector<std::wstring>& labels);
    float tabs(float x, float y, float w, const std::vector<std::wstring>& labels, int selected, const std::string& key,
               std::function<void(int)> pick);   // liefert die Höhe
    void card(const RectF& r, float rad = 12);
    void progress(const RectF& r, double p, const Color& c);
    void toggle(const RectF& r, bool on, const std::string& key, std::function<void()> click);
    void pill(const RectF& r, const std::wstring& label, const Color& bg, const Color& fg, F font = F::TinySemi);
    float pillWidth(const std::wstring& label, F font = F::TinySemi);
    void dot(float cx, float cy, float rad, const Color& c) { circle(cx, cy, rad, c); }

private:
    std::vector<Region>& regions_;
    uint32_t hot_, pressed_;
    float ox_ = 0, oy_ = 0;
    bool clipped_ = false;
    RectF clipDip_;
    RECT toPx(const RectF& r) const;
};

// Gemeinsamer Messcache (Schrift, Text) -> Breite
void clearMeasureCache();

}  // namespace ui
