// FragDesk – Attrappe von CS2 für Ablauftests unter Wine (oder Windows ohne CS2).
//
//   cs2.exe <Startoptionen>   zeigt ein Fenster "Counter-Strike 2", öffnet die Konsole auf -netconport,
//                             meldet nach kurzer Zeit "Demo geladen", protokolliert jeden Befehl, endet bei "quit"
//   steam.exe --idle          wartet nur (damit FragDesk "Steam läuft" sieht)
//
// Das Protokoll landet in der Datei aus der Umgebungsvariable FAKECS2_LOG (sonst fake_cs2.log im Arbeitsordner):
//   ARGS <Befehlszeile> / ENV USRLOCALCSGO=<Wert> / CMD <Befehl> / QUIT
#include <winsock2.h>
#include <ws2tcpip.h>
// winsock2 vor windows.h
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <thread>

static std::wstring g_log;

static void logLine(const std::wstring& text) {
    FILE* f = _wfopen(g_log.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"%ls\n", text.c_str());
    fclose(f);
}

static std::wstring env(const wchar_t* name) {
    wchar_t buf[2048];
    DWORD n = GetEnvironmentVariableW(name, buf, 2048);
    return n && n < 2048 ? std::wstring(buf, n) : std::wstring();
}

static void console(int port) {
    Sleep(1500);   // CS2 braucht eine Weile, bis die Konsole offen ist
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(u_short(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(ls, 1) != 0) {
        logLine(L"FEHLER Port " + std::to_wstring(port));
        ExitProcess(3);
    }
    SOCKET c = accept(ls, nullptr, nullptr);
    Sleep(1000);
    const char* hello = "Loading demo...\nChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_STATE_INGAME\n";
    send(c, hello, int(strlen(hello)), 0);
    std::string buf;
    char b[1024];
    for (;;) {
        int n = recv(c, b, sizeof b, 0);
        if (n <= 0) {
            logLine(L"GETRENNT");
            return;   // Konsole zu, "CS2" läuft weiter (wie beim Ansehen)
        }
        buf.append(b, size_t(n));
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            int len = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), int(line.size()), nullptr, 0);
            std::wstring w(size_t(len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, line.c_str(), int(line.size()), &w[0], len);
            logLine(L"CMD " + w);
            std::string echo = "] " + line + "\n";
            send(c, echo.c_str(), int(echo.size()), 0);
            if (line == "quit") {
                logLine(L"QUIT");
                ExitProcess(0);
            }
        }
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH br = CreateSolidBrush(RGB(40, 60, 90));
        FillRect(dc, &r, br);
        DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 200, 80));
        DrawTextW(dc, L"Attrappe von CS2", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t** argv) {
    g_log = env(L"FAKECS2_LOG");
    if (g_log.empty()) g_log = L"fake_cs2.log";
    if (argc > 1 && wcscmp(argv[1], L"--idle") == 0) {
        Sleep(INFINITE);
        return 0;
    }
    logLine(std::wstring(L"ARGS ") + GetCommandLineW());
    logLine(L"ENV USRLOCALCSGO=" + env(L"USRLOCALCSGO"));
    int port = 0, width = 800, height = 450;
    for (int i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"-netconport") == 0) port = _wtoi(argv[i + 1]);
        if (wcscmp(argv[i], L"-width") == 0) width = _wtoi(argv[i + 1]);
        if (wcscmp(argv[i], L"-height") == 0) height = _wtoi(argv[i + 1]);
    }
    if (!port) {
        logLine(L"FEHLER kein -netconport");
        return 2;
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FakeCS2";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND h = CreateWindowW(L"FakeCS2", L"Counter-Strike 2", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                           wc.hInstance, nullptr);
    if (!h) return 4;
    std::thread(console, port).detach();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
