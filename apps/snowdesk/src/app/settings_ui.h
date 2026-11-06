// SnowDesk – Einstellungsfenster (selbst gezeichnet im Windows-11-Stil, GDI+, Änderungen wirken sofort)
#pragma once
#include "settings.h"

#include "../weathercore.h"

#include <functional>
#include <memory>

namespace Gdiplus {
class Font;
class Graphics;
}

namespace app {

class SettingsWindow {
public:
    struct Callbacks {
        std::function<void(const Settings&)> changed;   // nach jeder Änderung (sofort anwenden)
        std::function<void()> shake;                    // "Alles abschütteln"
        std::function<std::wstring()> status;           // Zeile unter dem Titel
        std::function<void(bool)> modalLoop;            // Fenster wird gezogen (eigene Bildaktualisierung nötig)
        std::function<int()> monitorHz;                 // für "Monitor (… Hz)"
        std::function<bool(const std::wstring&)> searchPlace;   // Ortssuche starten (Antwort kommt über placesFound)
        std::function<std::wstring()> weatherText;              // aktuelles Wetter am Ort
    };

    SettingsWindow();
    ~SettingsWindow();
    bool open(const Settings& s, const Callbacks& cb);
    void close();
    bool isOpen() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }
    void refresh(const Settings& s);   // von außen geändert (Kontextmenü)
    void placesFound(bool ok, const std::vector<wx::Place>& places, const std::wstring& error);

    // Bedienelement (Koordinaten in DIP, bezogen auf den gesamten Inhalt)
    struct Ctl {
        int id = 0;
        int type = 0;
        float x = 0, y = 0, w = 0, h = 0;       // ganze Zeile
        float hx = 0, hy = 0, hw = 0, hh = 0;   // Bedienfläche
        std::wstring label;
        std::vector<std::wstring> options;
        std::vector<float> weights;   // relative Breite der Segmente (leer = gleich breit)
        float minV = 0, maxV = 1, step = 1;
    };
    struct Card {
        float x, y, w, h;
        std::wstring title;
    };
    struct Theme;

private:
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void layout();
    void createFonts();
    void loadTheme();
    void applyWindowTheme();
    void paint(HDC dc);
    void drawControl(Gdiplus::Graphics& g, const Ctl& c, bool hovered, bool pressed);
    int hitTest(int px, int py) const;
    static void segmentSpan(const Ctl& c, int i, float& x, float& w);
    float sliderValue(int id) const;
    std::wstring sliderText(int id) const;
    void setSliderFromX(const Ctl& c, float dipX);
    int segmentIndex(int id) const;
    void selectSegment(int id, int index);
    bool toggleValue(int id) const;
    void toggle(int id);
    void changed();
    void positionEdit();
    void syncEditFromSettings();
    void startSearch();
    void choosePlace(const wx::Place& p);
    void scrollTo(float y);
    void ensureBackBuffer(int w, int h);
    void freeBackBuffer();
    int clientHeightPx() const;

    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND placeEdit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    Settings s_;
    Callbacks cb_;
    float scale_ = 1.0f;
    float contentH_ = 800.0f;
    float scrollY_ = 0.0f;
    int hover_ = -1, press_ = -1, drag_ = -1;
    bool updatingEdit_ = false;
    std::vector<Ctl> ctls_;
    std::vector<Card> cards_;
    std::unique_ptr<Theme> theme_;
    std::unique_ptr<Gdiplus::Font> fTitle_, fCard_, fBody_, fSmall_, fBodyBold_;
    float editX_ = 0, editY_ = 0, editW_ = 0, editH_ = 0;   // DIP
    float hintY_ = 0, hintX_ = 0, hintW_ = 0;
    float placeX_ = 0, placeY_ = 0, placeW_ = 0, placeH_ = 0, wxTextY_ = 0;   // DIP
    std::wstring searchNote_;   // Hinweis zur Ortssuche (statt des Wettertexts)
    HDC memDC_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    HGDIOBJ memOld_ = nullptr;
    void* memBits_ = nullptr;
    int memW_ = 0, memH_ = 0;
    HICON iconBig_ = nullptr, iconSmall_ = nullptr;
};

}  // namespace app
