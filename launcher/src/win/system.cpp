// efficient lamp – Erkennen und Starten (siehe system.h)
#include "system.h"

#include <shellapi.h>
#include <tlhelp32.h>

#include <map>

#include "common.h"

namespace lamp {

namespace {

std::wstring regString(HKEY key, const wchar_t* name) {
    wchar_t buf[1024];
    DWORD size = sizeof buf - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) != ERROR_SUCCESS) return std::wstring();
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::wstring();
    buf[size / sizeof(wchar_t)] = 0;
    return buf;
}

// Version aus einer Datei (z. B. dem installierten C#-Quelltext) – nur neu lesen, wenn sich die Datei geändert hat,
// denn die Erkennung läuft alle paar Sekunden (nur im UI-Thread).
std::wstring versionFromFile(const std::wstring& path) {
    static std::map<std::wstring, std::pair<ULONGLONG, std::wstring>> cache;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return std::wstring();
    ULONGLONG stamp = (ULONGLONG(fa.ftLastWriteTime.dwHighDateTime) << 32 | fa.ftLastWriteTime.dwLowDateTime) ^ fa.nFileSizeLow;
    auto it = cache.find(path);
    if (it != cache.end() && it->second.first == stamp) return it->second.second;
    std::string text;
    std::wstring v;
    if (readFile(path, text, 4u << 20)) v = widen(cat::findVersionString(text));
    cache[path] = std::make_pair(stamp, v);
    return v;
}

}  // namespace

InstallInfo detectInstall(const cat::App& app) {
    InstallInfo info;
    if (!app.exe.empty()) info.exe = expandEnv(widen(app.exe));
    if (!app.installDir.empty()) info.dir = expandEnv(widen(app.installDir));
    bool viaRegistry = false;
    if (!app.uninstallKey.empty()) {
        std::wstring sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + widen(app.uninstallKey);
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
            viaRegistry = true;
            info.version = regString(k, L"DisplayVersion");
            std::wstring loc = regString(k, L"InstallLocation");
            if (!loc.empty()) info.dir = loc;
            RegCloseKey(k);
        }
    }
    if (!info.exe.empty()) info.installed = fileExists(info.exe);   // Registry-Eintrag ohne Programm zählt nicht
    else info.installed = viaRegistry;
    if (!info.installed) {
        info.version.clear();
        return info;
    }
    if (info.version.empty() && !app.versionFile.empty()) info.version = versionFromFile(expandEnv(widen(app.versionFile)));
    if (info.version.empty() && !info.exe.empty()) info.version = fileVersion(info.exe);
    if (info.dir.empty() && !info.exe.empty()) info.dir = parentDir(info.exe);
    return info;
}

std::vector<std::wstring> runningProcesses() {
    std::vector<std::wstring> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do out.push_back(lowerCase(pe.szExeFile));
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool isRunning(const std::vector<std::wstring>& procs, const std::string& exeName) {
    if (exeName.empty()) return false;
    std::wstring want = lowerCase(widen(exeName));
    for (const auto& p : procs)
        if (p == want) return true;
    return false;
}

HANDLE startScript(const std::wstring& dir, const std::wstring& script, const std::wstring& args, std::wstring& err) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmdExe = joinPath(sys, L"cmd.exe");
    // cmd /c ""C:\...\Setup.cmd" -Action Install" – die äußeren Anführungszeichen entfernt cmd selbst
    std::wstring line = L"\"" + cmdExe + L"\" /d /c \"\"" + joinPath(dir, script) + L"\"" + (args.empty() ? L"" : L" " + args) + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    std::wstring mutableLine = line;
    if (!CreateProcessW(cmdExe.c_str(), &mutableLine[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, dir.c_str(), &si, &pi)) {
        err = L"Setup konnte nicht gestartet werden (Fehler " + std::to_wstring(GetLastError()) + L")";
        logLine(L"Start fehlgeschlagen: %ls (%lu)", line.c_str(), GetLastError());
        return nullptr;
    }
    logLine(L"Gestartet: %ls", line.c_str());
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

bool startProgram(const std::wstring& exe, const std::wstring& args, std::wstring& err) {
    if (!fileExists(exe)) {
        err = L"Programm nicht gefunden: " + exe;
        return false;
    }
    AllowSetForegroundWindow(ASFW_ANY);   // damit das Programm sein Fenster nach vorne holen darf
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    std::wstring dir = parentDir(exe);
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        err = L"Programm konnte nicht gestartet werden (Fehler " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    logLine(L"Programm gestartet: %ls %ls", exe.c_str(), args.c_str());
    return true;
}

void openUrl(const std::wstring& url) {
    if (url.rfind(L"https://", 0) != 0) return;   // nur Webseiten
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openFolder(const std::wstring& dir) {
    if (!dirExists(dir)) return;
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + dir + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

bool systemUsesDarkMode() {
    DWORD v = 1, size = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                     nullptr, &v, &size) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

}  // namespace lamp
