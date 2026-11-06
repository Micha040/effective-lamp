// efficient lamp – gemeinsame Hilfen für den Windows-Teil (Zeichenketten, Dateien, Pfade, Protokoll)
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace lamp {

constexpr const wchar_t* kAppName = L"efficient lamp";
constexpr const wchar_t* kVersion = L"1.0.0";
constexpr const char* kVersionA = "1.0.0";
constexpr const wchar_t* kRepoUrl = L"https://github.com/Micha040/effective-lamp";
constexpr const wchar_t* kCatalogUrl = L"https://github.com/Micha040/effective-lamp/releases/latest/download/catalog.json";
constexpr const wchar_t* kExeName = L"EfficientLamp.exe";

// Nachrichten der Hintergrund-Threads an das Fenster (lParam: Zeiger, den der Empfänger löscht)
constexpr UINT WM_APP_CATALOG = WM_APP + 1;     // CatalogResult*
constexpr UINT WM_APP_JOB = WM_APP + 2;         // JobEvent*
constexpr UINT WM_APP_REFRESH = WM_APP + 3;     // Zustand neu erkennen
constexpr UINT WM_APP_SELFUPDATE = WM_APP + 4;  // SelfUpdateResult*

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);
std::wstring expandEnv(const std::wstring& s);
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::wstring parentDir(const std::wstring& path);
std::wstring lowerCase(std::wstring s);
bool samePath(const std::wstring& a, const std::wstring& b);

bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);
bool readFile(const std::wstring& path, std::string& out, size_t maxBytes = 64u << 20);
bool writeFileAtomic(const std::wstring& path, const std::string& data);   // erst .tmp, dann ersetzen
bool createDirs(const std::wstring& path);
bool deleteTree(const std::wstring& path);

std::wstring selfPath();
std::wstring localAppData();     // %LOCALAPPDATA%
std::wstring roamingAppData();   // %APPDATA%
std::wstring dataDir();          // %LOCALAPPDATA%\EfficientLamp (Katalog, Pakete, Protokoll)
std::wstring installDir();       // %LOCALAPPDATA%\Programs\EfficientLamp

std::wstring formatBytes(uint64_t bytes);   // "527 KB", "1,4 MB"
std::wstring fileVersion(const std::wstring& exe);   // "1.0.0" aus der Versionsressource, sonst leer

void logLine(const wchar_t* fmt, ...);

}  // namespace lamp
