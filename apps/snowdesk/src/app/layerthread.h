// SnowDesk – eigener Thread für die Fenster in der Desktop-Ebene.
// Ein Kindfenster in Explorers Desktop koppelt die Eingabe-Warteschlange seines Threads an Explorers Desktop-Thread.
// Damit ein beschäftigter Haupt-Thread (Grafik, Shader, Dateien) den Desktop nie ausbremsen kann – und ein hängender
// Explorer nicht SnowDesk –, gehören diese Fenster einem Thread, der nur Nachrichten verarbeitet.
#pragma once
#include "common.h"

#include <functional>

namespace app {

class LayerThread {
public:
    ~LayerThread() { stop(); }
    bool start();
    void stop(DWORD timeoutMs = 3000);
    // fn auf dem Fenster-Thread ausführen und höchstens timeoutMs warten. false = Zeit abgelaufen (fn läuft später noch;
    // fn darf deshalb nichts vom Stack des Aufrufers benutzen).
    bool call(std::function<void()> fn, DWORD timeoutMs);
    bool busy() const;   // eine frühere Aufgabe hängt noch (z. B. Explorer reagiert nicht)

private:
    struct Task;
    static DWORD WINAPI proc(LPVOID self);
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    HANDLE thread_ = nullptr;
    HWND wnd_ = nullptr;
    HANDLE ready_ = nullptr;
    volatile LONG pending_ = 0;
};

}  // namespace app
