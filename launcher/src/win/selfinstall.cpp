// efficient lamp – Selbstinstallation und -aktualisierung (siehe selfinstall.h)
#include "selfinstall.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "../core/catalog.h"
#include "common.h"

namespace lamp {

namespace {

const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EfficientLamp";

std::wstring shortcutPath() { return joinPath(roamingAppData(), L"Microsoft\\Windows\\Start Menu\\Programs\\efficient lamp.lnk"); }

bool createShortcut(const std::wstring& target) {
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        link->SetPath(target.c_str());
        link->SetWorkingDirectory(parentDir(target).c_str());
        link->SetDescription(L"Programme von efficient lamp installieren, starten und aktualisieren");
        link->SetIconLocation(target.c_str(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            createDirs(parentDir(shortcutPath()));
            ok = SUCCEEDED(file->Save(shortcutPath().c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }
    return ok;
}

void setString(HKEY k, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

void setDword(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v); }

void writeUninstallEntry(const std::wstring& exe) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    setString(k, L"DisplayName", L"efficient lamp");
    setString(k, L"DisplayVersion", kVersion);
    setString(k, L"DisplayIcon", exe + L",0");
    setString(k, L"Publisher", L"Micha040");
    setString(k, L"URLInfoAbout", kRepoUrl);
    setString(k, L"InstallLocation", parentDir(exe));
    setString(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    setDword(k, L"NoModify", 1);
    setDword(k, L"NoRepair", 1);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa)) setDword(k, L"EstimatedSize", (fa.nFileSizeLow + 1023) / 1024);
    RegCloseKey(k);
}

bool launch(const std::wstring& exe, const std::wstring& args) {
    std::wstring line = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &line[0], nullptr, nullptr, FALSE, 0, nullptr, parentDir(exe).c_str(), &si, &pi)) return false;
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace

std::wstring installedExe() { return joinPath(installDir(), kExeName); }

bool isInstalledCopy() { return samePath(selfPath(), installedExe()); }

void refreshRegistration() {
    std::wstring exe = installedExe();
    if (!fileExists(exe)) return;
    createShortcut(exe);
    writeUninstallEntry(exe);
}

void cleanupOldExe() {
    std::wstring old = joinPath(parentDir(selfPath()), L"EfficientLamp.old.exe");
    // Nach einem Update läuft die alte Version evtl. noch kurz – ein paar Mal versuchen
    for (int i = 0; i < 15 && fileExists(old) && !DeleteFileW(old.c_str()); ++i) Sleep(200);
}

bool installAndRelaunch(const std::wstring& extraArgs) {
    std::wstring self = selfPath(), target = installedExe();
    if (!createDirs(installDir())) {
        logLine(L"Selbstinstallation: Ordner %ls nicht anlegbar (%lu)", installDir().c_str(), GetLastError());
        return false;
    }
    std::wstring installedVersion = fileExists(target) ? fileVersion(target) : std::wstring();
    bool copy = installedVersion.empty() || cat::compareVersions(narrow(installedVersion), kVersionA) < 0;
    if (copy) {
        std::wstring old = joinPath(installDir(), L"EfficientLamp.old.exe");
        if (fileExists(target)) {   // eine laufende Datei lässt sich umbenennen, aber nicht überschreiben
            DeleteFileW(old.c_str());
            MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        if (!CopyFileW(self.c_str(), target.c_str(), FALSE)) {
            logLine(L"Selbstinstallation: Kopieren fehlgeschlagen (%lu)", GetLastError());
            if (!fileExists(target) && fileExists(old)) MoveFileExW(old.c_str(), target.c_str(), 0);
            return false;
        }
        logLine(L"Launcher %ls installiert nach %ls", kVersion, target.c_str());
    } else {
        logLine(L"Installierter Launcher (%ls) ist aktuell, starte ihn", installedVersion.c_str());
    }
    refreshRegistration();
    return launch(target, L"--welcome" + (extraArgs.empty() ? L"" : L" " + extraArgs));
}

bool applyUpdate(const std::string& exeData, std::wstring& err, const std::wstring& extraArgs) {
    std::wstring self = selfPath();
    std::wstring dir = parentDir(self);
    std::wstring fresh = joinPath(dir, L"EfficientLamp.new.exe");
    std::wstring old = joinPath(dir, L"EfficientLamp.old.exe");
    if (!writeFileAtomic(fresh, exeData)) {
        err = L"neue Version konnte nicht gespeichert werden";
        return false;
    }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = L"Launcher-Datei ist gesperrt (Fehler " + std::to_wstring(GetLastError()) + L")";
        DeleteFileW(fresh.c_str());
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = L"neue Version konnte nicht eingesetzt werden (Fehler " + std::to_wstring(GetLastError()) + L")";
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    logLine(L"Launcher aktualisiert, starte neu");
    if (!launch(self, L"--updated" + (extraArgs.empty() ? L"" : L" " + extraArgs))) {
        err = L"neue Version konnte nicht gestartet werden";
        return false;
    }
    return true;
}

bool uninstallSelf(HWND owner) {
    int answer = MessageBoxW(owner,
                             L"efficient lamp (den Launcher) vom PC entfernen?\n\n"
                             L"Deine installierten Programme bleiben erhalten – sie lassen sich jederzeit über "
                             L"\"Einstellungen › Apps › Installierte Apps\" einzeln entfernen.",
                             L"efficient lamp entfernen", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDYES) return false;
    DeleteFileW(shortcutPath().c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    deleteTree(dataDir());
    // Erst die Meldung, dann das Aufräumen starten – die laufende .exe kann sich nicht selbst löschen,
    // deshalb versucht ein kleiner Hilfsprozess es bis zu 40 Sekunden lang, bis dieser Prozess beendet ist.
    MessageBoxW(owner, L"efficient lamp wurde entfernt.", L"efficient lamp", MB_OK | MB_ICONINFORMATION);
    std::wstring dir = installDir();
    if (dirExists(dir)) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring cmd = joinPath(sys, L"cmd.exe");
        std::wstring line = L"\"" + cmd + L"\" /d /c \"for /l %i in (1,1,20) do (ping -n 3 127.0.0.1 >nul & rd /s /q \"" + dir +
                            L"\" 2>nul & if not exist \"" + dir + L"\\\" exit)\"";
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        if (CreateProcessW(cmd.c_str(), &line[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return true;
}

}  // namespace lamp
