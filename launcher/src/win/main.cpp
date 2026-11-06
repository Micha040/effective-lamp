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
#include "net.h"
#include "selfinstall.h"
#include "ui.h"

using namespace lamp;

namespace {

const wchar_t* kMutexName = L"Local\\EfficientLampLauncher";

// Läuft schon ein Launcher? Dann schließen lassen und warten, bis sein Prozess wirklich beendet ist
// (er hält die Mutex bis zum Schluss).
void closeRunningInstance() {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
    if (!m) return;
    HWND w = FindWindowW(L"EfficientLampWindow", nullptr);
    if (w) PostMessageW(w, WM_CLOSE, 0, 0);
    DWORD r = WaitForSingleObject(m, 15000);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) ReleaseMutex(m);
    CloseHandle(m);
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
        if (a == L"--catalog") {   // Testbetrieb: auch lokale Dateien, eigener Zwischenspeicher bleibt unberührt
            opt.catalogUrl = next();
            passArgs += L" --catalog " + quote(opt.catalogUrl);
            allowLocalSources(true);
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

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (opt.updated) {   // nach einem Selbst-Update: warten, bis die alte Version beendet ist
            DWORD r = WaitForSingleObject(mutex, 20000);
            if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) {
                CloseHandle(mutex);
                return 0;
            }
        } else {   // läuft schon: nach vorne holen
            HWND w = FindWindowW(L"EfficientLampWindow", nullptr);
            if (w) {
                if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
                SetForegroundWindow(w);
            }
            CloseHandle(mutex);
            return 0;
        }
    }
    cleanupOldExe();
    if (!opt.portable && !isInstalledCopy()) {
        ReleaseMutex(mutex);   // die installierte Kopie braucht den Namen gleich selbst
        CloseHandle(mutex);
        mutex = nullptr;
        if (installAndRelaunch(passArgs)) return 0;
        logLine(L"Selbstinstallation nicht möglich – laufe portabel");
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\EfficientLampLauncher");
    }
    opt.installedCopy = isInstalledCopy();
    if (opt.updated && opt.installedCopy) refreshRegistration();   // neue Version in "Installierte Apps" eintragen
    opt.relaunchArgs = passArgs + (opt.installedCopy ? L"" : (passArgs.empty() ? L"" : L" ") + std::wstring(L"--portable"));
    logLine(L"Launcher %ls gestartet (%ls)", kVersion, opt.installedCopy ? L"installiert" : L"portabel");
    int rc = runLauncher(inst, opt);
    if (mutex) CloseHandle(mutex);
    CoUninitialize();
    return rc;
}
