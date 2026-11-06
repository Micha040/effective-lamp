// Testhilfe (Windows/Wine): legt Fenster mit Titelleiste an bekannten Stellen an und kann sie per Befehlsdatei bewegen/schließen.
//   fakewins.exe <Steuerdatei>   Zeilen: "move <i> <dx> <dy> <schritte>", "close <i>", "front <i>", "quit"
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        // Titelleiste und Rahmen nachmalen (Wine ohne Fenstermanager zeichnet keine)
        RECT t{0, 0, r.right, 30};
        HBRUSH tb = CreateSolidBrush(RGB(232, 232, 236));
        FillRect(dc, &t, tb);
        DeleteObject(tb);
        wchar_t title[64];
        GetWindowTextW(h, title, 63);
        SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 10, 8, title, lstrlenW(title));
        HBRUSH fr = CreateSolidBrush(RGB(120, 120, 130));
        FrameRect(dc, &r, fr);
        DeleteObject(fr);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) return 0;
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"FakeWin";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    struct R { int x, y, w, h; const wchar_t* t; };
    const R rs[] = {{140, 330, 820, 520, L"Notizen"}, {1050, 210, 640, 560, L"Browser"}, {560, 640, 600, 300, L"Musik"}};
    std::vector<HWND> ws;
    for (const R& r : rs) ws.push_back(CreateWindowExW(0, L"FakeWin", r.t, WS_OVERLAPPEDWINDOW | WS_VISIBLE, r.x, r.y, r.w, r.h, nullptr, nullptr, inst, nullptr));
    std::wstring ctl = cmd ? cmd : L"";
    long done = 0;
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(30);
        FILE* f = _wfopen(ctl.c_str(), L"r");
        if (!f) continue;
        char line[256];
        long n = 0;
        while (fgets(line, sizeof line, f)) {
            if (++n <= done) continue;
            done = n;
            int i = 0, dx = 0, dy = 0, steps = 1;
            if (sscanf(line, "move %d %d %d %d", &i, &dx, &dy, &steps) >= 3 && i >= 0 && i < (int)ws.size()) {
                RECT r;
                GetWindowRect(ws[i], &r);
                for (int s = 1; s <= steps; ++s) {
                    SetWindowPos(ws[i], nullptr, r.left + dx * s / steps, r.top + dy * s / steps, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    MSG m2;
                    while (PeekMessageW(&m2, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m2);
                    Sleep(16);
                }
            } else if (sscanf(line, "close %d", &i) == 1 && i >= 0 && i < (int)ws.size()) {
                DestroyWindow(ws[i]);
            } else if (sscanf(line, "front %d", &i) == 1 && i >= 0 && i < (int)ws.size()) {
                SetForegroundWindow(ws[i]);
                SetWindowPos(ws[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            } else if (strncmp(line, "quit", 4) == 0) {
                fclose(f);
                return 0;
            }
        }
        fclose(f);
    }
}
