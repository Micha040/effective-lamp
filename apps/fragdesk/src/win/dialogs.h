// FragDesk – kleine Eingabe-Dialoge (sonst hat FragDesk keine Textfelder): den Steam-Bot des eigenen Servers anmelden
// (Benutzername und Passwort) und den Steam-Guard-Code eingeben. Modal zum Hauptfenster, dunkel oder hell und mit der
// DPI wie FragDesk, gezeichnet mit denselben Bausteinen; nur die Eingabefelder sind echte Windows-Steuerelemente.
// Enter bestätigt, Esc bricht ab, Tab wechselt das Feld. Das Passwortfeld wird nach dem Lesen geleert.
#pragma once
#include <windows.h>

#include <string>

namespace fdw {

struct DialogLook {
    bool dark = true;
    float scale = 1;   // wie das Hauptfenster (DPI / 96)
};

// Benutzername und Passwort des Bot-Kontos; false = abgebrochen
bool askBotLogin(HWND owner, const DialogLook& look, std::wstring& user, std::wstring& password);
// Steam-Guard-Code (5 Zeichen, Großbuchstaben und Ziffern); fromApp: aus der Steam-App statt aus der E-Mail
bool askGuardCode(HWND owner, const DialogLook& look, bool fromApp, const std::wstring& hint, std::wstring& code);

}  // namespace fdw
