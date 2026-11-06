// DeskCats – die echten Fenster als Welt der Katzen: Oberkanten, Taskleisten, Bildschirmboden, was davor liegt.
// Nur WinEvent-Hooks "außerhalb des Prozesses" (keine DLL in fremden Programmen, Anti-Cheat-freundlich) und
// gelegentliches Aufzählen der Fenster mit Zwischenspeicher.
#pragma once
#include "common.h"
#include "monitors.h"
#include "settings.h"

#include "../core/world.h"

#include <unordered_map>

namespace app {

constexpr uint64_t kTaskbarBase = 0xFFFF000000000000ull;   // Kennung Taskleiste: + Monitor
constexpr uint64_t kFloorBase = 0xFFFE000000000000ull;     // Kennung Bildschirmboden: + Monitor

class Scene {
public:
    bool start();
    void stop();
    // Bei Änderungen (Fenster bewegt, Vordergrund gewechselt …) einmal diese Nachricht schicken, damit gleich ein Takt kommt
    void setNotify(HWND target, UINT msg) {
        notifyWnd_ = target;
        notifyMsg_ = msg;
    }
    void clearNotified() { notified_ = false; }
    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void invalidate() {   // neu einlesen, und zwar gleich (eigene Beobachtung, z. B. Fläche verschwunden)
        dirty_ = true;
        notify();
    }
    bool zChanged() const { return zChanged_; }
    void clearZChanged() { zChanged_ = false; }
    bool dragging() const;
    HWND dragWindow() const { return dragWnd_; }
    // Diese Fenster genauer beobachten (Verschieben per Code, Einrasten): Hook nur für ihre Prozesse
    void watch(const std::vector<HWND>& windows);

    // Fenster einlesen und die Welt bauen. own = eigene Fenster (Katzen, Napf, Knäuel)
    void build(const std::vector<MonitorInfo>& mons, const std::vector<HWND>& own, const Settings& set, cat::World& out);

    HWND hwndOf(uint64_t platform) const;     // Fenster zur Fläche (Taskleiste: ihr Fenster, Boden: nullptr)
    static bool isTaskbar(uint64_t platform) { return (platform & 0xFFFF000000000000ull) == kTaskbarBase; }
    static bool isFloor(uint64_t platform) { return (platform & 0xFFFF000000000000ull) == kFloorBase; }
    HWND desktopTop() const { return desktopTop_; }   // oberstes Desktop-Fenster (Progman/WorkerW): Boden-Katzen darüber
    // Vollbild im Vordergrund? Monitor-Index (in mons) oder -1
    int fullscreenMonitor(const std::vector<MonitorInfo>& mons) const;
    double lastBuildMicros() const { return lastUs_; }
    std::wstring describe() const;

private:
    struct Entry {
        RECT wr;
        RECT vis;
        bool cloaked, ignored, platform, coverer, topmost;
        unsigned gen;
    };
    struct Item {
        HWND hwnd;
        RECT vis;
        bool platform, coverer, maximized;
    };
    static void CALLBACK hookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static BOOL CALLBACK enumProc(HWND h, LPARAM lp);
    void visit(HWND h);
    void classify(HWND h, Entry& e);

    std::vector<HWINEVENTHOOK> hooks_, watchHooks_;
    std::vector<DWORD> watchPids_;
    std::vector<HWND> watched_;
    std::unordered_map<HWND, Entry> cache_;
    std::vector<Item> list_;   // in Stapelreihenfolge, vorderstes zuerst
    const std::vector<HWND>* own_ = nullptr;
    std::unordered_map<uint64_t, HWND> platHwnd_;
    HWND desktopTop_ = nullptr;
    HWND dragWnd_ = nullptr;
    DWORD dragSince_ = 0;
    unsigned gen_ = 0;
    bool dirty_ = true, zChanged_ = true, refreshAll_ = true;
    HWND notifyWnd_ = nullptr;
    UINT notifyMsg_ = 0;
    bool notified_ = false;
    void notify() {
        if (notifyWnd_ && !notified_) {
            notified_ = true;
            PostMessageW(notifyWnd_, notifyMsg_, 0, 0);
        }
    }
    double lastUs_ = 0;
};

}  // namespace app
