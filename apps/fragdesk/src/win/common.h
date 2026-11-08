// FragDesk – gemeinsame Hilfen für den Windows-Teil (Zeichenketten, Dateien, Ordner, Protokoll)
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fdw {

constexpr const wchar_t* kAppName = L"FragDesk";
constexpr const wchar_t* kVersion = L"1.6.0";
constexpr const char* kVersionA = "1.6.0";
constexpr const wchar_t* kRepoUrl = L"https://github.com/Micha040/effective-lamp";
constexpr const wchar_t* kAnalyzerExe = L"fdanalyze.exe";

// Nachrichten der Hintergrund-Threads an das Fenster
constexpr UINT WM_APP_LIBRARY = WM_APP + 1;    // Bibliothek geladen / geändert
constexpr UINT WM_APP_DEMOS = WM_APP + 2;      // Demo-Ordner durchsucht
constexpr UINT WM_APP_ANALYSIS = WM_APP + 3;   // Fortschritt einer Auswertung (lParam: AnalysisEvent*)
constexpr UINT WM_APP_RECORD = WM_APP + 4;     // Fortschritt einer Aufnahme (lParam: RecordEvent*)
constexpr UINT WM_APP_FOLDER = WM_APP + 5;     // ein überwachter Ordner hat sich geändert
constexpr UINT WM_APP_REPLAY = WM_APP + 6;     // 2D-Daten geladen oder Fehler (lParam: ReplayEvent*)
constexpr UINT WM_APP_HEAT = WM_APP + 7;       // Heatmap vieler Matches geladen (lParam: HeatEvent*)
constexpr UINT WM_APP_MOBILE = WM_APP + 8;     // Handy-App: Anfrage im UI-Thread beantworten bzw. Geräteliste geändert
constexpr UINT WM_APP_SYNC = WM_APP + 9;       // eigener Server: Stand des Abgleichs, Ergebnis einer Aktion (lParam: SyncEvent*)
constexpr UINT WM_APP_AUTODL = WM_APP + 10;    // eigener Server: Demos automatisch laden, Steam-Bot (lParam: AutodlEvent*)
constexpr UINT WM_APP_DEMOFETCH = WM_APP + 11; // Demo von Valve laden: Fortschritt, fertig, Fehler (lParam: DemoFetchEvent*)
constexpr UINT_PTR kMobileTimer = 10;          // Countdown des Kopplungscodes
constexpr UINT_PTR kSyncTimer = 11;            // eigener Server: alle 15 Minuten abgleichen
constexpr UINT_PTR kSyncViewTimer = 12;        // eigener Server: „zuletzt vor …“ in den Einstellungen auffrischen
constexpr UINT_PTR kAutodlTimer = 13;          // eigener Server: Anmeldung des Steam-Bots bzw. „Jetzt prüfen“ verfolgen

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);
std::wstring expandEnv(const std::wstring& s);
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::wstring parentDir(const std::wstring& path);
std::wstring fileName(const std::wstring& path);
std::wstring lowerCase(std::wstring s);
bool samePath(const std::wstring& a, const std::wstring& b);
bool endsWithI(const std::wstring& s, const std::wstring& suffix);

bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);
bool readFile(const std::wstring& path, std::string& out, size_t maxBytes = 64u << 20);
bool writeFileAtomic(const std::wstring& path, const std::string& data);
bool createDirs(const std::wstring& path);
int64_t fileSize(const std::wstring& path);
int64_t fileTime(const std::wstring& path);   // letzte Änderung, Sekunden seit 1970 (UTC), 0 wenn unbekannt

std::wstring selfPath();
std::wstring selfDir();
std::wstring knownFolder(const GUID& id);
std::wstring localAppData();
std::wstring dataDir();        // %LOCALAPPDATA%\FragDesk
std::wstring matchesDir();     // %LOCALAPPDATA%\FragDesk\matches
std::wstring tempDir();        // %LOCALAPPDATA%\FragDesk\temp
std::wstring demosDir();       // %LOCALAPPDATA%\FragDesk\demos – Demos, die FragDesk selbst bei Valve geladen hat
std::wstring downloadsDir();
std::wstring videosDir();

// Ortszeit aus Sekunden seit 1970 (UTC)
SYSTEMTIME localTime(int64_t epoch);
std::wstring dateText(int64_t epoch);       // 06.11.2026
std::wstring dateTimeText(int64_t epoch);   // 06.11.2026, 21:13
std::wstring relativeDay(int64_t epoch);    // Heute / Gestern / 06.11.

void openInShell(const std::wstring& target);          // Datei, Ordner oder Adresse öffnen
void showInExplorer(const std::wstring& file);         // Ordner öffnen und Datei markieren
bool pickFolder(HWND owner, const std::wstring& title, std::wstring& folder);
bool pickFiles(HWND owner, std::vector<std::wstring>& files);

std::wstring clipboardText(HWND owner);                        // Text aus der Zwischenablage, ohne Leerraum am Rand
bool setClipboardText(HWND owner, const std::wstring& text);   // Text in die Zwischenablage

void logLine(const wchar_t* fmt, ...);

}  // namespace fdw
