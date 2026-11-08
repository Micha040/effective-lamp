// fragdesk-server – „Demos automatisch laden“: Codes, Texte, Anmeldung des Steam-Bots (siehe autodl.h)
#include "autodl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fdsrv {

namespace {

bool upperAlnum(char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); }
bool alnum(char ch) { return upperAlnum(ch) || (ch >= 'a' && ch <= 'z'); }

// ohne Leerraum (auch geschützte Leerzeichen aus kopierten Texten)
std::string noSpace(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {   // U+00A0
            ++i;
            continue;
        }
        if (ch <= 0x20 || ch == 0x7f) continue;
        out += char(ch);
    }
    return out;
}

std::string upper(std::string s) {
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    return s;
}

// Gruppen aus genau diesen Längen, getrennt durch '-', erlaubte Zeichen laut ok
template <class Ok>
bool groups(const std::string& s, std::initializer_list<size_t> lens, Ok ok) {
    size_t pos = 0, n = 0;
    for (size_t len : lens) {
        if (n++ > 0) {
            if (pos >= s.size() || s[pos] != '-') return false;
            ++pos;
        }
        if (pos + len > s.size()) return false;
        for (size_t i = 0; i < len; ++i)
            if (!ok(s[pos + i])) return false;
        pos += len;
    }
    return pos == s.size();
}

const char* kBullet = "\xE2\x80\xA2";   // •

}  // namespace

// ---- Codes -----------------------------------------------------------------------------------------------------
std::string normAuthCode(const std::string& s) { return upper(noSpace(s)); }

bool validAuthCode(const std::string& s) { return groups(s, {4, 5, 4}, upperAlnum); }

std::string maskAuthCode(const std::string& s) {
    if (s.empty()) return std::string();
    std::string out;
    size_t keep = s.size() >= 4 ? s.size() - 4 : 0;   // die letzten vier bleiben sichtbar
    for (size_t i = 0; i < s.size(); ++i) out += i >= keep || s[i] == '-' ? std::string(1, s[i]) : std::string(kBullet);
    return out;
}

std::string normShareCode(const std::string& s) {
    std::string t = noSpace(s);
    if (t.size() >= 4 && upper(t.substr(0, 4)) == "CSGO") t.replace(0, 4, "CSGO");   // "csgo-…" aus Versehen klein
    return t;
}

bool validShareCode(const std::string& s) {
    if (!(s.size() == 34 && s.compare(0, 4, "CSGO") == 0 && s[4] == '-' && groups(s.substr(5), {5, 5, 5, 5, 5}, alnum))) return false;
    // Wie Valve (und fragdesk-fetch): nur Zeichen aus dem Alphabet der Freigabecodes (ohne 0, 1, I, l, g), als Zahl zur
    // Basis 57 (letztes Zeichen vorn) höchstens 144 Bit – Match-ID, Ergebnis-ID und Token
    static const std::string kDict = "ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789";
    uint32_t limb[5] = {0, 0, 0, 0, 0};   // 160 Bit, limb[0] niederwertig
    for (size_t k = s.size(); k-- > 5;) {
        if (s[k] == '-') continue;
        size_t digit = kDict.find(s[k]);
        if (digit == std::string::npos) return false;
        uint64_t carry = digit;
        for (uint32_t& l : limb) {
            uint64_t v = uint64_t(l) * 57 + carry;
            l = uint32_t(v);
            carry = v >> 32;
        }
        if (carry) return false;
    }
    return (limb[4] >> 16) == 0;
}

std::string normApiKey(const std::string& s) { return upper(noSpace(s)); }

bool validApiKey(const std::string& s) {
    if (s.size() != 32) return false;
    for (char ch : s)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
    return true;
}

std::string normGuardCode(const std::string& s) { return upper(noSpace(s)); }

bool validGuardCode(const std::string& s) { return s.size() == 5 && std::all_of(s.begin(), s.end(), upperAlnum); }

bool validRefreshToken(const std::string& s) {
    if (s.size() < 20 || s.size() > 4096) return false;
    for (char ch : s)
        if (!(alnum(ch) || ch == '.' || ch == '_' || ch == '-')) return false;
    return true;
}

bool validSteamName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char ch : s)
        if (ch <= 0x20 || ch == 0x7f) return false;
    return true;
}

std::string cleanSteamId64(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (ch >= '0' && ch <= '9' && out.size() < 20) out += ch;
    return out;
}

int64_t epochFrom(double v) {
    if (!std::isfinite(v) || v <= 0) return 0;
    if (v > 1e11) v /= 1000;   // Millisekunden (Date.now()) statt Sekunden
    return v > 4e9 ? 0 : int64_t(v);
}

// ---- Texte ---------------------------------------------------------------------------------------------------
// Zahl und Einheit mit geschütztem Leerzeichen (U+00A0): in der Handy-App nie auf zwei Zeilen
#define FD_NBSP "\xC2\xA0"

std::string agoText(int64_t s) {
    if (s < 60) return "gerade eben";
    if (s < 3600) return "vor " + std::to_string(s / 60) + FD_NBSP "min";
    if (s < 86400) return "vor " + std::to_string(s / 3600) + FD_NBSP "Std.";
    int64_t d = s / 86400;
    return d == 1 ? "vor 1" FD_NBSP "Tag" : "vor " + std::to_string(d) + FD_NBSP "Tagen";
}

std::string matchesText(int64_t n) { return std::to_string(n) + (n == 1 ? FD_NBSP "Match" : FD_NBSP "Matches"); }

std::string helperProblem(const std::string& env) {
    if (env.empty() || env == "on" || env == "1") return std::string();
    if (env == "node")
        return "Auf dem Server fehlt Node.js 18 oder neuer – deshalb kann er keine Demos laden. Nach dem Installieren von Node.js "
               "install.sh noch einmal ausführen.";
    if (env == "release")
        return "Das installierte Release enthält den Helfer zum Laden der Demos noch nicht. install.sh noch einmal ausführen (holt "
               "das neueste Release).";
    if (env == "verify")
        return "Die systemd-Dienste zum Laden der Demos ließen sich nicht einrichten. install.sh noch einmal ausführen und die "
               "Meldungen ansehen.";
    return "Der Helfer zum Laden der Demos ist auf diesem Server nicht eingerichtet. install.sh noch einmal ausführen.";
}

std::string autodlStatus(const AutodlFacts& f, std::string& tone) {
    tone = "wait";
    if (!f.hasSteamId) return "Erst in FragDesk am PC deinen Spieler wählen – dann kannst du das hier einschalten.";
    if (!f.helperProblem.empty()) return f.admin ? f.helperProblem : "Der Server kann gerade keine Demos laden. Sag dem Admin Bescheid.";
    if (!f.botReady && f.botNeedsLogin) {
        tone = f.enabled ? "error" : "wait";   // stört nur, wer es eingeschaltet hat
        return f.admin ? "Der Steam-Bot muss neu angemeldet werden: Server verwalten › Steam-Bot."
                       : "Der Steam-Bot des Servers muss neu angemeldet werden – sag dem Admin Bescheid.";
    }
    if (!f.botReady) {
        return f.admin ? "Der Steam-Bot ist noch nicht eingerichtet – das machst du einmal für den ganzen Server unter Server verwalten › Steam-Bot."
                       : "Der Admin muss erst den Steam-Bot einrichten. Deine Codes kannst du schon eintragen.";
    }
    if (!f.enabled) {
        tone = "off";
        return f.hasAuth && f.hasKnown ? "Ausgeschaltet – deine Codes sind gespeichert." : "Aus – trag deine beiden Codes ein und schalte es ein.";
    }
    std::string got = f.fetched > 0 ? matchesText(f.fetched) + " geladen" : "noch kein neues Match";
    if (f.full) {
        tone = "error";
        return "Dein Speicher auf dem Server ist voll – neue Demos lädt der Server erst wieder, wenn Platz ist.";
    }
    tone = "ok";
    if (f.checking) return "Prüft gerade … · " + got;
    if (f.soon) return "Prüft gleich … · " + got;
    if (!f.lastError.empty()) {
        tone = "error";
        return f.lastError + (f.lastCheck > 0 ? " (" + agoText(f.now - f.lastCheck) + ")" : "");
    }
    if (f.lastCheck <= 0) return "Eingeschaltet – die erste Prüfung kommt gleich.";
    return "Prüft alle " + std::to_string(kCheckMinutes) + FD_NBSP "Minuten · zuletzt " + agoText(f.now - f.lastCheck) + " · " + got;
}

// ---- Anmeldung des Bots --------------------------------------------------------------------------------------
namespace {

void burn(std::string& s) {   // Geheimnis überschreiben, dann freigeben (so gut es mit std::string geht)
    std::fill(s.begin(), s.end(), '\0');
    s.clear();
    s.shrink_to_fit();
}

bool pending(const std::string& state) { return state == "working" || state == "need-code" || state == "need-confirm"; }

}  // namespace

void SteamLogin::wipe() {
    burn(user_);
    burn(password_);
    burn(code_);
}

void SteamLogin::expireLocked(int64_t now) {
    if (v_.active && now - v_.started >= kLoginSeconds) {
        wipe();
        v_.state = "error";
        v_.hint = "Zeit abgelaufen – bitte noch einmal anmelden.";
        v_.codeType.clear();
        v_.active = false;
        v_.updated = now;
        expiredAt_ = handedOut_ ? now : 0;   // nur dann kann vom Helfer noch eine verspätete Meldung kommen
    } else if (!v_.active && v_.state != "idle" && now - v_.updated >= kLoginSeconds) {
        v_ = View();   // Ergebnis einer alten Anmeldung nicht ewig zeigen
    }
}

void SteamLogin::start(const std::string& user, const std::string& password, int64_t now) {
    std::lock_guard<std::mutex> lk(mu_);
    wipe();
    user_ = user;
    password_ = password;
    handedOut_ = false;
    expiredAt_ = 0;
    v_ = View();
    v_.state = "working";
    v_.hint = "Anmeldung startet – der Server meldet sich gleich bei Steam.";
    v_.started = v_.updated = now;
    v_.active = true;
}

bool SteamLogin::code(const std::string& code, int64_t now, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
    if (!v_.active) {
        err = "Gerade läuft keine Anmeldung (oder sie ist abgelaufen). Tippe erst auf „Bot anmelden“.";
        return false;
    }
    if (v_.state != "need-code" && v_.state != "need-confirm") {   // need-confirm: statt Bestätigen geht auch der Code aus der App
        err = "Gerade wird kein Code gebraucht.";
        return false;
    }
    burn(code_);
    code_ = code;
    v_.state = "working";
    v_.hint = "Code wird geprüft …";
    v_.codeType.clear();
    v_.updated = now;
    return true;
}

SteamLogin::Handout SteamLogin::take(int64_t now) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
    Handout h;
    if (!v_.active) return h;
    h.active = true;
    h.user = user_;
    if (!handedOut_ && !password_.empty()) {   // genau einmal, dann vergessen
        h.password = password_;
        h.hasPassword = true;
        handedOut_ = true;
        burn(password_);
    }
    if (!code_.empty()) {
        h.code = code_;
        h.hasCode = true;
        burn(code_);
    }
    return h;
}

bool SteamLogin::report(const std::string& state, const std::string& hint, const std::string& codeType, int64_t now, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
    if (state == "done" || state == "error") {   // Ergebnis gilt immer – auch kurz nach Ablauf
        wipe();
        v_.state = state;
        v_.hint = hint.empty() ? (state == "done" ? "Angemeldet." : "Die Anmeldung hat nicht geklappt.") : hint;
        v_.codeType.clear();
        v_.active = false;
        v_.updated = now;
        return true;
    }
    if (!pending(state)) {
        err = "state muss working, need-code, need-confirm, done oder error sein.";
        return false;
    }
    if (!v_.active) {
        err = "Gerade ist keine Anmeldung offen (abgelaufen oder abgebrochen).";
        return false;
    }
    v_.state = state;
    if (!hint.empty()) v_.hint = hint;
    else if (state == "need-code") v_.hint = codeType == "app" ? "Code aus der Steam-App eingeben." : "Code aus der E-Mail von Steam eingeben.";
    else if (state == "need-confirm") v_.hint = "In der Steam-App auf dem Handy bestätigen.";
    else v_.hint = "Anmeldung läuft …";
    // need-code: woher der Code kommt; need-confirm mit "app": statt Bestätigen geht auch der Code aus der App
    v_.codeType = state == "need-code" ? (codeType == "app" ? "app" : "email") : state == "need-confirm" && codeType == "app" ? "app" : std::string();
    if (state == "need-code") burn(code_);   // ein älterer Code gilt nicht mehr
    v_.updated = now;
    return true;
}

bool SteamLogin::loginError(int64_t now) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
    if (v_.active || (expiredAt_ > 0 && now - expiredAt_ < kLoginSeconds)) return true;
    v_ = View();   // „angemeldet“ von vorhin gilt nicht mehr
    return false;
}

void SteamLogin::cancel() {
    std::lock_guard<std::mutex> lk(mu_);
    wipe();
    handedOut_ = false;
    expiredAt_ = 0;
    v_ = View();
}

SteamLogin::View SteamLogin::view(int64_t now) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
    View v = v_;
    v.pickedUp = handedOut_;
    return v;
}

void SteamLogin::expire(int64_t now) {
    std::lock_guard<std::mutex> lk(mu_);
    expireLocked(now);
}

}  // namespace fdsrv
