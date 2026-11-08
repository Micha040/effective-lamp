// fragdesk-server – „Demos automatisch laden“ (Premier, Wettkampf, Wingman): Prüfungen der Codes, Maskieren, die
// Anmeldung des Steam-Bots (nur im Speicher) und die fertigen Sätze für die Handy-App.
//
// Den Weg nach außen hat nur der Helfer fragdesk-fetch (eigener systemd-Dienst, Node.js, siehe server/fetch/). Er
// holt sich über /api/v1/local/fetch/* (nur 127.0.0.1, local.key) seinen Auftrag, meldet Zwischenstände und lädt die
// Ergebnisse hoch. Der Server weckt ihn, indem er <data>/fetch/wake neu schreibt (systemd-Path-Unit).
//
// Geheim und nie im Protokoll: Passwort und Steam-Guard-Code des Bots (nur hier im Speicher, höchstens 15 Minuten,
// je genau einmal an den Helfer), Anmelde-Token und Web-API-Schlüssel (steam.json, 0600), die
// Authentifizierungscodes der Spieler (account.json, an die Handy-App nur maskiert).
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace fdsrv {

constexpr int64_t kLoginSeconds = 15 * 60;   // so lange bleibt eine Anmeldung (samt Passwort) im Speicher
constexpr int64_t kCheckMinutes = 15;        // der Timer des Helfers (fragdesk-fetch.timer)

// ---- Codes -----------------------------------------------------------------------------------------------------
// Authentifizierungscode für den Spielverlauf (Steam-Hilfe): Großbuchstaben erzwingen, Leerzeichen weg;
// gültig ist ^[A-Z0-9]{4}-[A-Z0-9]{5}-[A-Z0-9]{4}$
std::string normAuthCode(const std::string& s);
bool validAuthCode(const std::string& s);
std::string maskAuthCode(const std::string& s);   // "ABCD-EFGHI-12AB" -> "••••-•••••-12AB", leer bleibt leer
// Freigabecode eines Matches ^CSGO(-[A-Za-z0-9]{5}){5}$ (Groß/klein zählt), Leerzeichen weg; gültig nur mit Zeichen aus
// Valves Alphabet und höchstens 144 Bit (wie fragdesk-fetch, sonst lehnt der Helfer ihn ab)
std::string normShareCode(const std::string& s);
bool validShareCode(const std::string& s);
// Web-API-Schlüssel: 32 Hex-Zeichen, groß
std::string normApiKey(const std::string& s);
bool validApiKey(const std::string& s);
// Steam-Guard-Code (E-Mail oder Steam-App): 5 Zeichen, groß
std::string normGuardCode(const std::string& s);
bool validGuardCode(const std::string& s);
bool validRefreshToken(const std::string& s);   // JWT von Steam: [A-Za-z0-9._-], 20–4096 Zeichen
// Adresse einer Demo, wie sie der Game Coordinator liefert (http://replay183.valve.net/730/…_….dem.bz2): http(s)://, mit
// Host, höchstens 512 sichtbare Zeichen ohne Leerzeichen, Anführungszeichen und spitze Klammern
bool validDemoUrl(const std::string& s);
bool validSteamName(const std::string& s);      // Kontoname zum Anmelden: 1–64 sichtbare Zeichen ohne Leerzeichen
std::string cleanSteamId64(const std::string& s);   // nur Ziffern, höchstens 20
// Zeit vom Helfer: Sekunden seit 1970 (Millisekunden werden erkannt und umgerechnet), < 0 -> 0
int64_t epochFrom(double v);

// ---- Texte ---------------------------------------------------------------------------------------------------
// Zahl und Einheit jeweils mit geschütztem Leerzeichen (U+00A0)
std::string agoText(int64_t seconds);   // "gerade eben", "vor 3 min", "vor 2 Std.", "vor 1 Tag", "vor 4 Tagen"
std::string matchesText(int64_t n);     // "1 Match", "4 Matches"

// Stand des Helfers auf diesem Server (install.sh schreibt FRAGDESK_FETCH nach /etc/default/fragdesk):
// "" oder "on" = eingerichtet; "node" = Node.js ≥ 18 fehlt; "release" = Release ohne Helfer; sonst nicht eingerichtet
std::string helperProblem(const std::string& env);   // leer = alles da, sonst ein deutscher Satz

// Der fertige Satz für die Handy-App („Prüft alle 15 Minuten · zuletzt vor 3 min · 4 Matches geladen“) und ein Ton
// für die Anzeige: "ok", "off", "wait" (etwas fehlt noch), "error"
struct AutodlFacts {
    bool enabled = false, hasAuth = false, hasKnown = false, hasSteamId = false, botReady = false, admin = false;
    bool botNeedsLogin = false;   // Bot war angemeldet, Steam nimmt das Token aber nicht mehr
    bool checking = false, soon = false, full = false;   // läuft gerade / gleich (Jetzt prüfen) / Speicher voll
    std::string helperProblem, lastError;
    int64_t lastCheck = 0, fetched = 0, now = 0;
};
std::string autodlStatus(const AutodlFacts& f, std::string& tone);

// ---- Anmeldung des Bots (nur im Speicher) --------------------------------------------------------------------
// Zustände: idle → working (Admin hat Benutzer und Passwort geschickt) → need-code / need-confirm (meldet der Helfer)
// → working (Admin hat den Code geschickt) → … → done / error. Passwort und Code gibt take() je genau einmal heraus;
// nach 15 Minuten ist alles vergessen (offene Anmeldung: error „Zeit abgelaufen“), done/error nach 15 Minuten idle.
class SteamLogin {
public:
    struct View {
        std::string state = "idle", hint, codeType;   // codeType: "email" | "app" (nur bei need-code)
        int64_t started = 0, updated = 0;
        bool active = false;     // working, need-code oder need-confirm
        bool pickedUp = false;   // der Helfer hat das Passwort schon abgeholt
    };
    struct Handout {
        bool active = false;
        std::string user, password, code;   // password/code leer = schon herausgegeben bzw. (noch) keiner da
        bool hasPassword = false, hasCode = false;
    };

    void start(const std::string& user, const std::string& password, int64_t now);
    // Admin schickt den Code; 409, wenn gerade keiner gebraucht wird (Text in err)
    bool code(const std::string& code, int64_t now, std::string& err);
    Handout take(int64_t now);   // für /api/v1/local/fetch/job
    // Meldung des Helfers; false = gerade keine Anmeldung offen (err)
    bool report(const std::string& state, const std::string& hint, const std::string& codeType, int64_t now, std::string& err);
    // Der Helfer meldet "error": gehört das zu einer Anmeldung (offen oder vor weniger als 15 Minuten abgelaufen – der
    // Helfer wartet selbst bis zu 10 Minuten auf den Code und meldet sich dann verspätet)? Sonst (false) kommt es aus
    // dem normalen Lauf: Steam lehnt das gespeicherte Token ab, und ein altes Ergebnis („angemeldet“) verschwindet.
    bool loginError(int64_t now);
    void cancel();               // Bot abgemeldet
    View view(int64_t now);      // räumt dabei Abgelaufenes auf
    void expire(int64_t now);

private:
    std::mutex mu_;
    View v_;
    std::string user_, password_, code_;
    bool handedOut_ = false;   // Passwort schon beim Helfer
    int64_t expiredAt_ = 0;    // wann eine offene Anmeldung zuletzt abgelaufen ist
    void wipe();               // Geheimnisse vergessen (mit mu_)
    void expireLocked(int64_t now);
};

}  // namespace fdsrv
