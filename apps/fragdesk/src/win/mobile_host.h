// FragDesk – Handy-App in FragDesk: Webserver starten/anhalten, Anfragen im UI-Thread beantworten, Handys koppeln,
// Adressen fürs Handy finden (WLAN, Tailscale), Vorschaubilder der Videos. Aus, bis man es einschaltet.
#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "settings.h"

namespace fdw {

class App;

struct MobileAddress {
    std::wstring url;     // z. B. http://100.101.102.103:7420/
    std::wstring label;   // "Tailscale", "WLAN", "Eigene Adresse"
};

class MobileHost {
public:
    explicit MobileHost(App& app);
    ~MobileHost();

    void apply();   // nach den Einstellungen starten oder anhalten (UI-Thread)
    void stop();
    bool running() const;
    int port() const;
    const std::wstring& error() const;   // warum der Start nicht ging (leer = kein Fehler)

    std::vector<MobileAddress> addresses() const;   // beste zuerst: eigene, Tailscale, WLAN
    bool tailscale() const;                          // Tailscale auf diesem PC verbunden?

    // Kopplung
    std::string startPairing();   // neuer Code, 10 Minuten gültig
    void stopPairing();
    std::string pairingCode() const;
    int64_t pairingUntil() const;   // Sekunden seit 1970
    std::vector<MobileDevice> devices() const;
    void removeDevice(const std::string& token);

    LRESULT onMessage(WPARAM wp, LPARAM lp);   // WM_APP_MOBILE (UI-Thread)

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// QR-Code (Fehlerkorrektur M) als Zeilen von Modulen, true = dunkel; leer bei Fehler
std::vector<std::vector<bool>> qrModules(const std::string& text);

}  // namespace fdw
