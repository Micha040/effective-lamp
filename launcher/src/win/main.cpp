// efficient lamp – Einstieg
//
//   EfficientLamp.exe                 Launcher öffnen (beim ersten Start: unter %LOCALAPPDATA%\Programs einrichten)
//   EfficientLamp.exe --portable      ohne Einrichtung direkt aus diesem Ordner laufen
//   EfficientLamp.exe --uninstall     Launcher entfernen (die Programme bleiben)
//
// Für Tests: --catalog <URL|Pfad>  --theme dark|light  --demo  --render <datei.png> [--size 760x600] [--scale 100]
#include <objbase.h>
#include <shellapi.h>

#include "common.h"
#include "selfinstall.h"
#include "ui.h"

using namespace lamp;

namespace {

void closeRunningInstance() {
    HWND w = FindWindowW(L"EfficientLampWindow", nullptr);
    if (!w) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    PostMessageW(w, WM_CLOSE, 0, 0);
    for (int i = 0; i < 50 && IsWindow(w); ++i) Sleep(100);   // höchstens 5 Sekunden
}

std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Options opt;
    bool uninstall = false;
    std::wstring passArgs;   // für den Neustart der installierten Kopie
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--catalog") {
            opt.catalogUrl = next();
            passArgs += L" --catalog " + quote(opt.catalogUrl);
        } else if (a == L"--portable") opt.portable = true;
        else if (a == L"--welcome") opt.welcome = true;
        else if (a == L"--updated") opt.updated = true;
        else if (a == L"--demo") {
            opt.demo = true;
            passArgs += L" --demo";
        } else if (a == L"--theme") {
            std::wstring t = next();
            opt.theme = t == L"dark" ? 1 : t == L"light" ? 0 : -1;
            passArgs += L" --theme " + t;
        } else if (a == L"--render") opt.renderPng = next();
        else if (a == L"--size") swscanf(next().c_str(), L"%dx%d", &opt.renderWidth, &opt.renderHeight);
        else if (a == L"--scale") opt.renderScale = _wtoi(next().c_str());
        else if (a == L"--uninstall") uninstall = true;
    }
    if (argv) LocalFree(argv);
    if (!passArgs.empty()) passArgs.erase(0, 1);

    if (!opt.renderPng.empty()) return runLauncher(inst, opt);   // Bild zeichnen, sonst nichts

    if (uninstall) {
        closeRunningInstance();
        uninstallSelf(nullptr);
        return 0;
    }

    cleanupOldExe();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\EfficientLampLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {   // läuft schon: nach vorne holen
        HWND w = FindWindowW(L"EfficientLampWindow", nullptr);
        if (w) {
            if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return 0;
    }
    if (!opt.portable && !isInstalledCopy()) {
        ReleaseMutex(mutex);   // die installierte Kopie braucht den Namen gleich selbst
        CloseHandle(mutex);
        mutex = nullptr;
        if (installAndRelaunch(passArgs)) return 0;
        logLine(L"Selbstinstallation nicht möglich – laufe portabel");
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\EfficientLampLauncher");
    }
    if (opt.updated && isInstalledCopy()) refreshRegistration();   // neue Version in "Installierte Apps" eintragen
    logLine(L"Launcher %ls gestartet (%ls)", kVersion, isInstalledCopy() ? L"installiert" : L"portabel");
    int rc = runLauncher(inst, opt);
    if (mutex) CloseHandle(mutex);
    CoUninitialize();
    return rc;
}
