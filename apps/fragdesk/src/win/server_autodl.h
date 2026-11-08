// FragDesk – „Demos automatisch laden“ in den Einstellungen (Modus „Eigener Server“, verbunden): der Server holt neue
// Premier-, Wettkampf- und Wingman-Matches des Kontos selbst bei Valve, FragDesk holt sie dann vom Server. Wie in der
// Handy-App (web/js/pages/autodl.js), nur mit dem Konto-Token von FragDesk: /api/v1/account/autodl (jedes Konto) und für
// Admins /api/v1/admin/steam (der Steam-Bot, einmal für den ganzen Server).
//
// FragDesk hat keine Textfelder: Authentifizierungscode, Freigabecode und Web-API-Schlüssel kommen aus der
// Zwischenablage (geprüft wie auf dem Server), Benutzername und Passwort des Bots und der Steam-Guard-Code aus einem
// kleinen Dialog (dialogs.h). Jede Anfrage läuft in einem kurzen eigenen Thread, die Antwort kommt als WM_APP_AUTODL.
// Ein Zeitgeber fragt nach, solange der Bot angemeldet wird (alle 2 s) oder der Server gerade prüft (alle 3 s, höchstens
// 40-mal) – sonst nur, wenn die Einstellungen offen sind (höchstens jede Minute).
#pragma once
#include <windows.h>

#include <memory>
#include <string>

#include "../core/json.h"

namespace fdw {

class App;

class ServerAutodl {
public:
    explicit ServerAutodl(App& app);
    ~ServerAutodl();

    void refresh(bool force = false);   // aus dem Zeichnen der Einstellungen: höchstens jede Minute (force: jetzt)
    void reset();                        // anderes Konto oder getrennt: alles vergessen

    // Konto
    void pasteAuthCode();    // „Authentifizierungscode einfügen“
    void pasteShareCode();   // „Freigabecode einfügen“
    void setEnabled(bool on);
    void checkNow();         // „Jetzt prüfen“
    void turnOff();          // „Ausschalten“: aus, beide Codes vom Server löschen (fragt vorher)
    // Steam-Bot (Admins)
    void pasteApiKey();      // „API-Schlüssel einfügen“
    void loginBot();         // „Bot anmelden …“: Dialog, dann Code oder Bestätigen in der Steam-App
    void enterCode();        // Code (E-Mail oder Steam-App) eingeben
    void logoutBot();        // „Bot abmelden“ (fragt vorher)

    // Anzeige
    bool loaded() const;                  // Stand des Kontos ist da
    bool steamLoaded() const;             // Stand des Bots ist da (nur Admins)
    const json::Value& account() const;   // Antwort von /api/v1/account/autodl
    const json::Value& steam() const;     // Antwort von /api/v1/admin/steam
    std::wstring problem() const;         // Stand ließ sich nicht holen (Grund), sonst leer
    bool busy(const std::string& what) const;   // Aktion läuft ("auth", "known", "toggle", "check", "off", "key", "login", "code", "logout")

    void onTimer();                              // kAutodlTimer
    LRESULT onMessage(WPARAM wp, LPARAM lp);     // WM_APP_AUTODL (UI-Thread)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Codes aus der Zwischenablage, geprüft wie auf dem Server (darf in einem längeren Text stehen); leer = keiner drin
std::string findAuthCode(const std::wstring& text);    // ^[A-Z0-9]{4}-[A-Z0-9]{5}-[A-Z0-9]{4}$ (groß geschrieben)
std::string findShareCode(const std::wstring& text);   // CSGO-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx (Alphabet von Valve)
std::string findApiKey(const std::wstring& text);      // 32 Hex-Zeichen (groß geschrieben)

}  // namespace fdw
