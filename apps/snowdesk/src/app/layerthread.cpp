// SnowDesk – Fenster-Thread für die Desktop-Ebene (siehe layerthread.h)
#include "layerthread.h"

namespace app {

namespace {
const wchar_t* kClass = L"SnowDeskLayerThread";
constexpr UINT WM_RUN = WM_APP + 1;
}  // namespace

struct LayerThread::Task {
    std::function<void()> fn;
    HANDLE done = nullptr;
    volatile LONG refs = 2;   // Aufrufer + Fenster-Thread
    volatile LONG* pending = nullptr;
    void release() {
        if (InterlockedDecrement(&refs) == 0) {
            if (done) CloseHandle(done);
            delete this;
        }
    }
};

LRESULT CALLBACK LayerThread::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_RUN) {
        Task* t = reinterpret_cast<Task*>(lp);
        if (t) {
            if (t->fn) t->fn();
            InterlockedDecrement(t->pending);
            SetEvent(t->done);
            t->release();
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

DWORD WINAPI LayerThread::proc(LPVOID param) {
    auto* self = reinterpret_cast<LayerThread*>(param);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // Nachrichten-Fenster nur für Aufgaben; die Ebenen-Fenster legt DesktopLayer auf diesem Thread an
    self->wnd_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    SetEvent(self->ready_);
    if (!self->wnd_) return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

bool LayerThread::start() {
    if (thread_) return wnd_ != nullptr;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, proc, this, 0, nullptr);
    if (!thread_) return false;
    WaitForSingleObject(ready_, 5000);
    return wnd_ != nullptr;
}

void LayerThread::stop(DWORD timeoutMs) {
    if (!thread_) return;
    if (wnd_) PostMessageW(wnd_, WM_QUIT, 0, 0);
    if (WaitForSingleObject(thread_, timeoutMs) != WAIT_OBJECT_0) {
        // hängt in Explorer fest: nicht ewig warten – beim Beenden räumt Windows den Thread ab
        logLine(L"Fenster-Thread reagiert nicht");
    }
    CloseHandle(thread_);
    thread_ = nullptr;
    wnd_ = nullptr;
    if (ready_) CloseHandle(ready_);
    ready_ = nullptr;
}

bool LayerThread::busy() const { return pending_ > 0; }

bool LayerThread::call(std::function<void()> fn, DWORD timeoutMs) {
    if (!wnd_) return false;
    Task* t = new Task;
    t->fn = std::move(fn);
    t->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    t->pending = &pending_;
    InterlockedIncrement(&pending_);
    if (!PostMessageW(wnd_, WM_RUN, 0, reinterpret_cast<LPARAM>(t))) {
        InterlockedDecrement(&pending_);
        t->release();
        t->release();
        return false;
    }
    bool ok = WaitForSingleObject(t->done, timeoutMs) == WAIT_OBJECT_0;
    t->release();
    return ok;
}

}  // namespace app
