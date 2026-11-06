// DeskCats – Katzen auf dem Desktop
//
//   DeskCats.exe                 starten bzw. Einstellungen öffnen, wenn es schon läuft
//   DeskCats.exe --autostart     still starten (Anmeldung)
//   DeskCats.exe --cmd <Befehl>  feed | play | call | sleep | wake | show | hide | toggle | settings | reload | exit
//   DeskCats.exe --pack-menu <Ordner>   Paket für das Windows-11-Kontextmenü bauen (Setup)
//   DeskCats.exe --selftest <s>  starten, nach s Sekunden Statistik ins Log schreiben und beenden
#include "app.h"
#include "pack.h"

#include <shellapi.h>

#include <cwchar>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cmd;
    bool autostart = false;
    int selftest = 0;
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--autostart") autostart = true;
        else if (a == L"--cmd" && i + 1 < argc) cmd = argv[++i];
        else if (a == L"--selftest" && i + 1 < argc) selftest = _wtoi(argv[++i]);
        else if (a == L"--pack-menu" && i + 1 < argc) {
            app::logInit();
            int rc = app::packMenu(argv[i + 1]);
            LocalFree(argv);
            return rc;
        }
    }
    if (argv) LocalFree(argv);
    // Läuft schon? Dann nur den Befehl weitergeben (ohne Befehl: Einstellungen öffnen)
    if (!cmd.empty() && cmd != L"show" && app::sendToRunning(cmd)) return 0;
    if (cmd == L"exit") return 0;   // läuft nicht: nichts zu beenden
    return app::runApp(cmd, autostart, selftest);
}
