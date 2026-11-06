// SnowDesk – Fenster in der Desktop-Ebene (eins pro Monitor), in die der Renderer per DirectComposition zeichnet.
//
// Windows 11 24H2/25H2 ("raised desktop"): Progman (WS_EX_NOREDIRECTIONBITMAP) enthält SHELLDLL_DefView (Symbole,
// geschichtet, fast durchsichtig) und darunter WorkerW (Hintergrundbild bzw. Wallpaper Engine).
//   LayerBehind:    eigenes geschichtetes Kindfenster von Progman zwischen DefView und WorkerW (Microsoft-Empfehlung)
//   LayerWallpaper: Kindfenster von WorkerW, oberhalb des Hintergrunds (Alternative, falls 1 nicht sichtbar ist)
//   LayerFront:     durchklickbares Fenster direkt über dem Desktop (Schnee liegt dann vor den Symbolen)
// Ältere Windows-Versionen: Kindfenster des WorkerW hinter den Symbolen (klassischer Weg).
#pragma once
#include "common.h"
#include "monitors.h"

namespace app {

enum LayerMode : int { LayerNone = -1, LayerBehind = 0, LayerWallpaper = 1, LayerFront = 2, LayerClassic = 3 };

const wchar_t* layerName(int mode);

class DesktopLayer {
public:
    // Legt die Fenster an. wanted = LayerBehind/LayerWallpaper/LayerFront; klappt das nicht, wird der nächste Weg
    // probiert (Ergebnis siehe mode()). hostWnd bekommt WM_APP_LAYERLOST, wenn Explorer die Fenster zerstört.
    bool attach(int wanted, const std::vector<MonitorInfo>& mons, HWND hostWnd);
    void detach();
    int mode() const { return mode_; }
    size_t count() const { return wins_.size(); }
    HWND window(size_t i) const { return i < wins_.size() ? wins_[i] : nullptr; }
    bool owns(HWND h) const;
    // Regelmäßige Prüfung: Eltern-/Z-Reihenfolge reparieren. false = neu anlegen nötig.
    bool check();
    std::wstring diagnostics() const;

private:
    bool attachBehind(const std::vector<MonitorInfo>& mons, bool wallpaperChild);
    bool attachClassic(const std::vector<MonitorInfo>& mons);
    bool attachFront(const std::vector<MonitorInfo>& mons);
    HWND createLayerWindow(bool layered, bool noRedirection);
    void place(HWND w, const MonitorInfo& m, HWND parent, HWND insertAfter);

    HWND host_ = nullptr;
    HWND progman_ = nullptr, defView_ = nullptr, workerW_ = nullptr, parent_ = nullptr;
    std::vector<HWND> wins_;
    std::vector<MonitorInfo> mons_;
    int mode_ = LayerNone;
};

constexpr UINT WM_APP_LAYERLOST = WM_APP + 21;

}  // namespace app
