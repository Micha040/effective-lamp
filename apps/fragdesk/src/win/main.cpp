// FragDesk – Einstiegspunkt: Befehlszeile, nur eine Instanz, GDI+ und COM
//
//   FragDesk.exe                         normal starten (läuft schon eins, kommt dessen Fenster nach vorne)
//   FragDesk.exe <Demo> …               Demos auswerten lassen (z. B. „Öffnen mit“)
//   --theme dark|light                   Farbschema erzwingen
//   --render <bild.png> [--page dashboard|matches|match|stats|highlights|settings|replay] [--tab N] [--match N]
//                       [--at <s>]  (replay: --tab = Runde, --at = Sekunden nach der Kaufphase)
//                       [--heat presence,kills,deaths,smoke,molly,flash,he,throws,points,me,team,enemy,t,ct]  (Heatmap-Auswahl)
//            [--size 1440x900] [--scale 100] [--scroll Y] [--data <Ordner>] [--me <SteamID>] [--select N]
//                                        Seite ohne Fenster als Bild zeichnen (Tests, README)
#include <objbase.h>
#include <shellapi.h>

#include <cstdlib>
#include <string>

#include "common.h"
#include "gfx.h"
#include "window.h"

using namespace fdw;

namespace {

HANDLE g_instance = nullptr;   // "nur eine FragDesk"-Sperre

Page pageFromName(const std::wstring& s) {
    if (s == L"matches") return Page::Matches;
    if (s == L"match") return Page::Match;
    if (s == L"stats") return Page::Stats;
    if (s == L"highlights") return Page::Highlights;
    if (s == L"settings") return Page::Settings;
    if (s == L"replay") return Page::Replay;
    return Page::Dashboard;
}

}  // namespace

// Beim Schließen des Fensters sofort freigeben: Das Beenden (z. B. CS2 nach einer abgebrochenen Aufnahme schließen)
// kann noch ein paar Sekunden dauern, ein neuer Start soll trotzdem gleich ein Fenster bekommen
void fdw::releaseInstanceLock() {
    if (!g_instance) return;
    ReleaseMutex(g_instance);
    CloseHandle(g_instance);
    g_instance = nullptr;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    Options opt;
    std::vector<std::wstring> demos;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--theme") opt.theme = next();
        else if (a == L"--render") opt.renderPath = next();
        else if (a == L"--page") opt.page = pageFromName(next());
        else if (a == L"--tab") opt.tab = _wtoi(next().c_str());
        else if (a == L"--at") opt.replayAt = _wtof(next().c_str());
        else if (a == L"--heat") opt.heat = next();
        else if (a == L"--match") opt.matchIndex = _wtoi(next().c_str());
        else if (a == L"--select") opt.selectCount = _wtoi(next().c_str());
        else if (a == L"--scale") opt.renderScale = std::max(50, std::min(300, _wtoi(next().c_str())));
        else if (a == L"--scroll") opt.renderScroll = float(_wtof(next().c_str()));
        else if (a == L"--me") opt.me = next();
        else if (a == L"--data") SetEnvironmentVariableW(L"FRAGDESK_DATA", next().c_str());
        else if (a == L"--size") {
            std::wstring s = next();
            size_t x = s.find(L'x');
            if (x != std::wstring::npos) {
                opt.renderWidth = std::max(640, _wtoi(s.substr(0, x).c_str()));
                opt.renderHeight = std::max(400, _wtoi(s.substr(x + 1).c_str()));
            }
        } else if (!a.empty() && a[0] != L'-') demos.push_back(a);
    }
    LocalFree(argv);

    // Nur eine Instanz: Demos an die laufende weiterreichen
    if (opt.renderPath.empty()) {
        g_instance = CreateMutexW(nullptr, TRUE, L"Local\\FragDesk");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = FindWindowW(L"FragDeskWindow", nullptr);
            if (other) {
                for (const auto& d : demos) {
                    COPYDATASTRUCT cd = {1, DWORD((d.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(d.c_str())};
                    SendMessageW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd));
                }
                if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
                SetForegroundWindow(other);
            }
            return 0;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);
    opt.demos = demos;
    int rc = runApp(inst, opt);
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    releaseInstanceLock();
    return rc;
}
