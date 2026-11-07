// FragDesk – Attrappe von CS2 für Ablauftests unter Wine (oder Windows ohne CS2).
//
//   cs2.exe <Startoptionen>   zeigt ein Fenster "Counter-Strike 2", spielt nach kurzer Zeit die Demo aus +playdemo ab,
//                             öffnet die Konsole auf -netconport, protokolliert jeden Befehl, endet bei "quit"
//   steam.exe --idle          wartet nur (damit FragDesk "Steam läuft" sieht)
//
// Wie das echte CS2 unter Windows öffnet die Attrappe die Konsole nur mit -tools (Workshop-Tools-Modus); ohne
// -noassetbrowser erscheint dann zusätzlich ein großes Fenster "Asset Browser". Mit -condebug landet die Ausgabe
// auch in game\csgo\console.log. FAKECS2_IGNORE_PLAYDEMO=1: +playdemo beim Start wird übergangen (CS2 bleibt im
// Hauptmenü, bis "playdemo" über die Konsole kommt). FAKECS2_NO_CONSOLE=1: nie eine Konsole (wie ohne Workshop Tools).
//
// Das Protokoll landet in der Datei aus der Umgebungsvariable FAKECS2_LOG (sonst fake_cs2.log im Arbeitsordner):
//   ARGS <Befehlszeile> / ENV USRLOCALCSGO=<Wert> / CMD <Befehl> / QUIT / KEINE KONSOLE …
#include <winsock2.h>
#include <ws2tcpip.h>
// winsock2 vor windows.h
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>

static std::wstring g_log, g_conlog;
static SOCKET g_client = INVALID_SOCKET;
static std::mutex g_mu;
static bool g_demoStarted = false;

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

// Ausgabe der Konsole: console.log (bei -condebug) und an einen verbundenen netcon-Client
static void out(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_conlog.empty())
        if (FILE* f = _wfopen(g_conlog.c_str(), L"ab")) {
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);
        }
    if (g_client != INVALID_SOCKET) send(g_client, text.data(), int(text.size()), 0);
}

static void playDemo() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_demoStarted) return;
        g_demoStarted = true;
    }
    std::thread([] {
        Sleep(800);
        out("ChangeGameUIState: CSGO_GAME_UI_STATE_MAINMENU -> CSGO_GAME_UI_STATE_LOADINGSCREEN\n");
        Sleep(1500);
        out("ChangeGameUIState: CSGO_GAME_UI_STATE_LOADINGSCREEN -> CSGO_GAME_UI_STATE_INGAME\n");
    }).detach();
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
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_client = c;
    }
    std::string buf;
    char b[1024];
    for (;;) {
        int n = recv(c, b, sizeof b, 0);
        if (n <= 0) {
            logLine(L"GETRENNT");
            std::lock_guard<std::mutex> lock(g_mu);
            g_client = INVALID_SOCKET;
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
            out("] " + line + "\n");
            if (line.rfind("playdemo ", 0) == 0) playDemo();
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
        wchar_t title[128];
        GetWindowTextW(h, title, 128);
        std::wstring text = std::wstring(L"Attrappe: ") + title;
        DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    bool tools = false, noAssetBrowser = false, condebug = false, playdemo = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        bool hasNext = i + 1 < argc;
        if (a == L"-netconport" && hasNext) port = _wtoi(argv[i + 1]);
        if (a == L"-width" && hasNext) width = _wtoi(argv[i + 1]);
        if (a == L"-height" && hasNext) height = _wtoi(argv[i + 1]);
        if (a == L"-tools") tools = true;
        if (a == L"-noassetbrowser") noAssetBrowser = true;
        if (a == L"-condebug") condebug = true;
        if (a == L"+playdemo" && hasNext) playdemo = true;
    }
    if (condebug) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        for (int up = 0; up < 3; ++up) dir = dir.substr(0, dir.find_last_of(L'\\'));   // ...\game\bin\win64\cs2.exe -> ...\game
        g_conlog = dir + L"\\csgo\\console.log";
        DeleteFileW(g_conlog.c_str());
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
    if (tools && !noAssetBrowser) {
        logLine(L"ASSET BROWSER (ohne -noassetbrowser)");
        CreateWindowW(L"FakeCS2", L"Asset Browser", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 1700, 1000, nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (env(L"FAKECS2_NO_CONSOLE") == L"1") tools = false;
    if (port && tools) std::thread(console, port).detach();
    else if (port) logLine(L"KEINE KONSOLE (ohne -tools öffnet CS2 unter Windows keine netcon-Konsole)");
    bool ignorePlaydemo = env(L"FAKECS2_IGNORE_PLAYDEMO") == L"1";
    std::thread([playdemo, ignorePlaydemo, port, tools] {
        Sleep(1200);
        out("ChangeGameUIState: CSGO_GAME_UI_STATE_INVALID -> CSGO_GAME_UI_STATE_MAINMENU\n");
        if (port && !tools) out("NetCon: socket failed (WSANOTINITIALISED)\n");
        if (playdemo && !ignorePlaydemo) playDemo();
    }).detach();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
