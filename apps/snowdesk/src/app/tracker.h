// SnowDesk – Fenster beobachten: welche Oberkanten tragen Schnee, ist ein Monitor komplett verdeckt?
// Nur WinEvent-Hooks "außerhalb des Prozesses" (keine DLL in fremden Programmen, Anti-Cheat-freundlich) und
// gelegentliches Aufzählen der Fenster mit Zwischenspeicher.
#pragma once
#include "common.h"
#include "monitors.h"

#include "../snowcore.h"

#include <unordered_map>

namespace app {

struct TrackedWindow {
    HWND hwnd;
    RECT vis;          // sichtbarer Rahmen (ohne unsichtbare Ränder)
    bool platform;     // Schnee bleibt auf der Oberkante liegen
    bool coverer;      // verdeckt den Hintergrund (für "Monitor komplett verdeckt")
    bool maximized;
};

class WindowTracker {
public:
    bool start();
    void stop();
    void markDirty() { dirty_ = true; }
    void requestRefresh() { refreshAll_ = true; }
    bool dirty() const { return dirty_; }
    // Fenster wird gerade gezogen (mit Zeitgrenze, falls das Ende-Ereignis einmal verloren geht)
    bool dragging() const { return dragging_ && GetTickCount64() - dragStart_ < 20000; }

    // Fenster aufzählen. ignore = eigene Ebenen-Fenster.
    void scan(const std::vector<HWND>& ignore);
    const std::vector<TrackedWindow>& windows() const { return list_; }
    void platformsFor(const MonitorInfo& m, std::vector<snow::PlatformIn>& out) const;
    bool covered(const MonitorInfo& m) const;
    double lastScanMicros() const { return lastScanUs_; }
    std::wstring describeWindows() const;

private:
    struct Entry {
        RECT wr;
        RECT vis;
        bool cloaked;
        bool ignored;
        bool platform;
        bool coverer;
        unsigned gen;
    };
    static void CALLBACK hookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static BOOL CALLBACK enumProc(HWND h, LPARAM lp);
    void visit(HWND h);
    void classify(HWND h, Entry& e);

    std::vector<HWINEVENTHOOK> hooks_;
    std::unordered_map<HWND, Entry> cache_;
    std::vector<TrackedWindow> list_;
    const std::vector<HWND>* ignore_ = nullptr;
    unsigned gen_ = 0;
    bool dirty_ = true, dragging_ = false, refreshAll_ = true;
    ULONGLONG dragStart_ = 0;
    double lastScanUs_ = 0;
};

}  // namespace app
