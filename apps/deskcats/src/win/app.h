// DeskCats – Hauptprogramm (Katzen-Fenster, Takt, Befehle, Menüs)
#pragma once
#include "common.h"

namespace app {

constexpr const wchar_t* kVersion = L"1.0.1";

// Läuft schon eine Instanz? Dann Befehl hinschicken.
bool sendToRunning(const std::wstring& cmd);
// Programm ausführen (kehrt beim Beenden zurück). selftestSeconds > 0: nach so vielen Sekunden mit Statistik beenden.
int runApp(const std::wstring& startCmd, bool autostart, int selftestSeconds);

}  // namespace app
