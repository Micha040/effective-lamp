// efficient lamp – der Launcher installiert und aktualisiert sich selbst (nur für den aktuellen Benutzer,
// ohne Administratorrechte): %LOCALAPPDATA%\Programs\EfficientLamp, Startmenü-Eintrag, Eintrag unter "Installierte Apps".
#pragma once
#include <windows.h>

#include <string>

namespace lamp {

bool isInstalledCopy();   // läuft gerade die installierte Kopie?
std::wstring installedExe();

// Kopiert den Launcher an seinen festen Platz und richtet Startmenü + Deinstallations-Eintrag ein.
// Rückgabe true: die installierte Kopie wurde gestartet, dieser Prozess soll sich beenden.
bool installAndRelaunch(const std::wstring& extraArgs);

void refreshRegistration();   // Startmenü-Verknüpfung + Deinstallations-Eintrag (auch nach einem Update)
void cleanupOldExe();         // Rest eines Updates (EfficientLamp.old.exe) entfernen

// Neue Version (bereits geprüft) einspielen und mit "--updated <extraArgs>" neu starten. Rückgabe false + err bei Fehler.
bool applyUpdate(const std::string& exeData, std::wstring& err, const std::wstring& extraArgs);

// "Launcher deinstallieren": fragt nach, entfernt Launcher, Verknüpfung, Eintrag und Zwischenspeicher.
// Die installierten Programme bleiben. Rückgabe true = entfernt (Prozess beenden).
bool uninstallSelf(HWND owner);

}  // namespace lamp
