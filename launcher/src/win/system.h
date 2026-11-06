// efficient lamp – Programme erkennen, laufende Prozesse sehen, Skripte und Programme starten.
// Anti-Cheat-freundlich: Prozesse werden nur per Toolhelp-Schnappschuss beim Namen erkannt, nie geöffnet.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "../core/catalog.h"

namespace lamp {

struct InstallInfo {
    bool installed = false;
    std::wstring version;   // leer, wenn unbekannt
    std::wstring exe;       // Hauptprogramm (falls bekannt)
    std::wstring dir;       // Installationsordner (falls bekannt)
};

InstallInfo detectInstall(const cat::App& app);
std::vector<std::wstring> runningProcesses();   // Dateinamen in Kleinbuchstaben
bool isRunning(const std::vector<std::wstring>& procs, const std::string& exeName);

// Skript aus dem entpackten Paket in einem eigenen Konsolenfenster starten. Rückgabe: Prozess-Handle (oder null)
HANDLE startScript(const std::wstring& dir, const std::wstring& script, const std::wstring& args, std::wstring& err);
bool startProgram(const std::wstring& exe, const std::wstring& args, std::wstring& err);
void openUrl(const std::wstring& url);
void openFolder(const std::wstring& dir);

bool systemUsesDarkMode();

}  // namespace lamp
