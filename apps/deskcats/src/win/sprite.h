// DeskCats – ein kleines, rahmenloses Fenster pro Katze bzw. Gegenstand (Napf, Knäuel).
// Inhalt per UpdateLayeredWindow (Position und Bild in einem Schritt, keine Grafikkarte nötig).
// Mausklicks kommen nur auf sichtbaren Pixeln an, durchsichtige Stellen sind durchklickbar.
#pragma once
#include "common.h"

#include "../core/pixcat.h"

#include <functional>

namespace app {

class SpriteWindow {
public:
    using MouseFn = std::function<void(int id, UINT msg, POINT screen)>;

    bool create(HINSTANCE inst, int id, MouseFn fn);
    void destroy();
    HWND hwnd() const { return hwnd_; }

    // Bild zeigen (img bereits gespiegelt). left/top: Bildschirm-Pixel der linken oberen Ecke.
    void show(const cat::PixelImage& img, int scale, int left, int top, bool contentChanged);
    void hide();
    bool shown() const { return shown_; }
    RECT rect() const { return RECT{curX_, curY_, curX_ + curW_, curY_ + curH_}; }

    // Stapelreihenfolge
    void zAbove(HWND platform);         // direkt über dem Fenster, auf dem die Katze sitzt
    void zTopmost();                    // über allem (Taskleiste, in der Luft, getragen)
    void zAboveDesktop(HWND desktopTop);
    bool isTopmost() const { return topmost_; }
    // Liegt direkt unter uns (eigene Fenster übersprungen) dieses Fenster? blocker: was stattdessen dazwischen liegt
    bool directlyAbove(HWND platform, const std::function<bool(HWND)>& isOwn, HWND* blocker = nullptr) const;

    // Wo das Fenster in der Stapelreihenfolge zuletzt hingelegt wurde (verwaltet von App::updateZ)
    struct ZState {
        int mode = -1;            // 0 direkt über einem Fenster, 1 ganz oben, 2 über dem Desktop, -1 unbekannt
        uint64_t plat = ~0ull;
        int fails = 0;            // vergebliche Versuche hintereinander
        double retryAt = 0;       // vorher nicht erneut versuchen (kein Hin und Her mit anderen Programmen)
    } z;

private:
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void ensureDib(int w, int h);

    HWND hwnd_ = nullptr;
    int id_ = 0;
    MouseFn mouse_;
    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr, oldBmp_ = nullptr;
    uint32_t* bits_ = nullptr;
    int dibW_ = 0, dibH_ = 0;
    int curX_ = 0, curY_ = 0, curW_ = 0, curH_ = 0;
    bool shown_ = false, topmost_ = false;
};

}  // namespace app
