// FragDesk – Demos automatisch laden in den Einstellungen (siehe server_autodl.h)
#include "server_autodl.h"

#include <cwctype>
#include <set>
#include <thread>

#include "../api/jsonw.h"
#include "app.h"
#include "common.h"
#include "dialogs.h"
#include "net.h"
#include "server_sync.h"

namespace fdw {

namespace {

enum class Req { Get, Auth, Known, Toggle, Check, Off, GetSteam, Key, Login, Code, Logout };

const char* busyKey(Req r) {
    switch (r) {
        case Req::Get: return "get";
        case Req::Auth: return "auth";
        case Req::Known: return "known";
        case Req::Toggle: return "toggle";
        case Req::Check: return "check";
        case Req::Off: return "off";
        case Req::GetSteam: return "getsteam";
        case Req::Key: return "key";
        case Req::Login: return "login";
        case Req::Code: return "code";
        case Req::Logout: return "logout";
    }
    return "";
}

bool isSteamReq(Req r) { return r == Req::GetSteam || r == Req::Key || r == Req::Login || r == Req::Code || r == Req::Logout; }

struct AutodlEvent {
    Req req = Req::Get;
    std::string key;   // Konto, für das gefragt wurde (Adresse|Schlüssel)
    int status = 0;
    DWORD error = 0;
    std::string body;
    bool on = false;   // Toggle: ein oder aus
};

bool pendingLogin(const std::string& state) { return state == "working" || state == "need-code" || state == "need-confirm"; }

bool alnumW(wchar_t ch) { return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z'); }
bool hexW(wchar_t ch) { return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F') || (ch >= L'a' && ch <= L'f'); }

std::string upperAscii(std::string s) {
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    return s;
}

std::wstring noSpace(const std::wstring& s) {   // auch geschützte Leerzeichen aus kopierten Texten
    std::wstring out;
    for (wchar_t ch : s)
        if (!iswspace(ch) && ch != 0xA0) out += ch;
    return out;
}

// Wie der Server (autodl.cpp): Zeichen aus dem Alphabet der Freigabecodes, als Zahl zur Basis 57 höchstens 144 Bit
bool validShareCode(const std::string& s) {
    if (s.size() != 34 || s.compare(0, 5, "CSGO-") != 0) return false;
    for (size_t g = 0; g < 5; ++g) {
        size_t at = 5 + g * 6;
        if (g > 0 && s[at - 1] != '-') return false;
        for (size_t k = 0; k < 5; ++k)
            if (!alnumW(wchar_t(static_cast<unsigned char>(s[at + k])))) return false;
    }
    static const std::string kDict = "ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789";
    uint32_t limb[5] = {0, 0, 0, 0, 0};
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

std::wstring failText(const std::string& body, int status, DWORD error) {   // Fehler als deutscher Satz
    if (status == 0) return L"Server nicht erreichbar: " + netErrorText(error);
    json::Value v;
    if (json::parse(body, v) && v.isObject() && !v.getString("error").empty()) return widen(v.getString("error").substr(0, 400));
    return L"Der Server antwortet mit Fehler " + std::to_wstring(status) + L".";
}

const json::Value kNull;

}  // namespace

// ---- Codes aus der Zwischenablage ------------------------------------------------------------------------------------
std::string findAuthCode(const std::wstring& text) {
    std::wstring t = noSpace(text);
    auto at = [&](size_t i, size_t n) {
        for (size_t k = 0; k < n; ++k)
            if (i + k >= t.size() || !alnumW(t[i + k])) return false;
        return true;
    };
    for (size_t i = 0; i + 15 <= t.size(); ++i) {   // AAAA-AAAAA-AAAA, ohne weitere Buchstaben oder Striche davor und danach
        if (i > 0 && (alnumW(t[i - 1]) || t[i - 1] == L'-')) continue;
        if (!(at(i, 4) && t[i + 4] == L'-' && at(i + 5, 5) && t[i + 10] == L'-' && at(i + 11, 4))) continue;
        if (i + 15 < t.size() && (alnumW(t[i + 15]) || t[i + 15] == L'-')) continue;
        return upperAscii(narrow(t.substr(i, 15)));
    }
    return std::string();
}

std::string findShareCode(const std::wstring& text) {
    std::wstring t = noSpace(text);
    std::wstring low = lowerCase(t);
    for (size_t i = low.find(L"csgo-"); i != std::wstring::npos; i = low.find(L"csgo-", i + 1)) {
        if (i + 34 > t.size()) break;
        std::string c = "CSGO" + narrow(t.substr(i + 4, 30));
        if (validShareCode(c) && (i + 34 == t.size() || !alnumW(t[i + 34]))) return c;
    }
    return std::string();
}

std::string findApiKey(const std::wstring& text) {
    std::wstring t = noSpace(text);
    for (size_t i = 0; i + 32 <= t.size(); ++i) {
        if (i > 0 && hexW(t[i - 1])) continue;
        size_t n = 0;
        while (i + n < t.size() && hexW(t[i + n])) ++n;
        if (n == 32) return upperAscii(narrow(t.substr(i, 32)));
        i += n;
    }
    return std::string();
}

// ---- Anfragen ------------------------------------------------------------------------------------------------------
struct ServerAutodl::Impl {
    App& app;
    std::string key;   // Konto der angezeigten Daten (Adresse|Schlüssel)
    json::Value account, steam;
    bool haveAccount = false, haveSteam = false, timerOn = false, dialogOpen = false;
    std::wstring problem;
    std::set<std::string> busy;
    ULONGLONG fetchedAt = 0;
    int ticks = 0, checkPolls = 0;
    std::string loginState = "idle", promptedSig;   // zuletzt gesehener Stand der Anmeldung / Code schon abgefragt für
    std::wstring lastUser;
    explicit Impl(App& a) : app(a) {}

    std::string currentKey() const {
        const ServerSettings& s = app.settings.mobile.srv;
        return app.sync->active() ? s.url + "|" + s.token : std::string();
    }
    DialogLook dialogLook() const {
        DialogLook l;
        l.dark = app.uiDark;
        l.scale = app.uiScale;
        return l;
    }
    void send(Req req, const wchar_t* method, const std::string& path, std::string body = std::string(), bool on = false) {
        std::string k = currentKey();
        ServerUrl url;
        if (k.empty() || app.renderMode || !parseServerUrl(app.settings.mobile.srv.url, url)) return;
        busy.insert(busyKey(req));
        HWND hwnd = app.hwnd;
        std::string token = app.settings.mobile.srv.token;
        std::thread([hwnd, k, url, token, req, method, path, body, on]() mutable {
            HttpClient http;
            HttpRequest rq;
            rq.method = method;
            rq.path = path;
            rq.token = token;
            if (std::wstring(method) != L"GET") {
                if (body.empty()) body = "{}";
                rq.contentType = "application/json";
                rq.body = body.data();
                rq.bodySize = body.size();
            }
            HttpResponse r = http.send(url, rq);
            SecureZeroMemory(&body[0], body.size());   // kann das Passwort des Bots enthalten
            auto* e = new AutodlEvent();
            e->req = req;
            e->key = k;
            e->status = r.status;
            e->error = r.error;
            e->body = std::move(r.body);
            e->on = on;
            if (!hwnd || !PostMessageW(hwnd, WM_APP_AUTODL, 0, reinterpret_cast<LPARAM>(e))) delete e;
        }).detach();
        if (!body.empty()) SecureZeroMemory(&body[0], body.size());
        app.invalidate();
    }
    void startTimer() {
        if (timerOn || !app.hwnd) return;
        SetTimer(app.hwnd, kAutodlTimer, 2000, nullptr);
        timerOn = true;
        ticks = 0;
    }
    void watchCheck() {   // nach „Jetzt prüfen“ oder dem Einschalten ein paar Mal nachsehen, bis der Server fertig ist
        checkPolls = 0;
        startTimer();
    }
    bool checking() const {
        return haveAccount && (account.getBool("checking") || (account.getBool("enabled") && account.getNumber("lastCheck") <= 0 && account.getBool("ready")));
    }
    void putAccount(Req req, const std::string& field, const std::string& value) {
        fdapi::JsonW j;
        j.obj().kv(field.c_str(), value).end();
        send(req, L"PUT", "api/v1/account/autodl", j.take());
    }
    void refreshSteam() {   // Admins: Stand des Bots neu (Zahl der Konten, letzter Lauf)
        if (app.settings.mobile.srv.admin && haveSteam && !busy.count("getsteam")) send(Req::GetSteam, L"GET", "api/v1/admin/steam");
    }
};

ServerAutodl::ServerAutodl(App& app) : d_(std::make_unique<Impl>(app)) {}
ServerAutodl::~ServerAutodl() = default;

bool ServerAutodl::loaded() const { return d_->haveAccount && d_->key == d_->currentKey(); }
bool ServerAutodl::steamLoaded() const { return d_->haveSteam && d_->key == d_->currentKey(); }
const json::Value& ServerAutodl::account() const { return loaded() ? d_->account : kNull; }
const json::Value& ServerAutodl::steam() const { return steamLoaded() ? d_->steam : kNull; }
std::wstring ServerAutodl::problem() const { return d_->problem; }
bool ServerAutodl::busy(const std::string& what) const { return d_->busy.count(what) > 0; }

void ServerAutodl::reset() {
    Impl& d = *d_;
    d.key.clear();
    d.account = json::Value();
    d.steam = json::Value();
    d.haveAccount = d.haveSteam = false;
    d.problem.clear();
    d.busy.clear();
    d.fetchedAt = 0;
    d.loginState = "idle";
    d.promptedSig.clear();
}

void ServerAutodl::refresh(bool force) {
    Impl& d = *d_;
    if (d.app.renderMode) return;
    std::string k = d.currentKey();
    if (k.empty()) {
        if (!d.key.empty()) reset();
        return;
    }
    if (k != d.key) {
        reset();
        d.key = k;
    }
    ULONGLONG now = GetTickCount64();
    if ((!force && d.fetchedAt && now - d.fetchedAt < 60000) || d.busy.count("get")) return;
    d.fetchedAt = now;
    d.send(Req::Get, L"GET", "api/v1/account/autodl");
    if (d.app.settings.mobile.srv.admin && !d.busy.count("getsteam")) d.send(Req::GetSteam, L"GET", "api/v1/admin/steam");
}

void ServerAutodl::pasteAuthCode() {
    std::string code = findAuthCode(clipboardText(d_->app.hwnd));
    if (code.empty()) {   // Hinweise kurz: der Toast hat eine Zeile
        d_->app.notify(L"Kein Authentifizierungscode (AAAA-AAAAA-AAAA) in der Zwischenablage – erst bei Steam kopieren.", true);
        return;
    }
    d_->putAccount(Req::Auth, "authCode", code);
}

void ServerAutodl::pasteShareCode() {
    std::string code = findShareCode(clipboardText(d_->app.hwnd));
    if (code.empty()) {
        d_->app.notify(L"Kein Freigabecode (CSGO-…) in der Zwischenablage – er steht bei Steam unter dem Authentifizierungscode.", true);
        return;
    }
    d_->putAccount(Req::Known, "knownCode", code);
}

void ServerAutodl::setEnabled(bool on) {
    fdapi::JsonW j;
    j.obj().kb("enabled", on).end();
    d_->send(Req::Toggle, L"PUT", "api/v1/account/autodl", j.take(), on);
}

void ServerAutodl::checkNow() { d_->send(Req::Check, L"POST", "api/v1/account/autodl/check"); }

void ServerAutodl::turnOff() {
    if (MessageBoxW(d_->app.hwnd, L"„Demos automatisch laden“ ausschalten und beide Codes vom Server löschen?", L"FragDesk",
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    d_->send(Req::Off, L"DELETE", "api/v1/account/autodl");
}

void ServerAutodl::pasteApiKey() {
    std::string key = findApiKey(clipboardText(d_->app.hwnd));
    if (key.empty()) {
        d_->app.notify(L"Kein Web-API-Schlüssel (32 Zeichen, 0–9 und A–F) in der Zwischenablage – erst als Bot kopieren.", true);
        return;
    }
    fdapi::JsonW j;
    j.obj().kv("key", key).end();
    d_->send(Req::Key, L"PUT", "api/v1/admin/steam/apikey", j.take());
}

void ServerAutodl::loginBot() {
    Impl& d = *d_;
    if (d.dialogOpen) return;
    std::wstring user = d.lastUser, password;
    d.dialogOpen = true;
    bool ok = askBotLogin(d.app.hwnd, d.dialogLook(), user, password);
    d.dialogOpen = false;
    if (!ok) return;
    d.lastUser = user;
    std::string pw = narrow(password);
    if (!password.empty()) SecureZeroMemory(&password[0], password.size() * sizeof(wchar_t));
    fdapi::JsonW j;
    j.obj().kv("user", narrow(user)).kv("password", pw).end();
    if (!pw.empty()) SecureZeroMemory(&pw[0], pw.size());
    d.promptedSig.clear();
    d.loginState = "working";
    d.send(Req::Login, L"POST", "api/v1/admin/steam/login", j.take());
    logLine(L"Server: Steam-Bot – Anmeldung als %ls angefragt", user.c_str());
}

void ServerAutodl::enterCode() {
    Impl& d = *d_;
    if (d.dialogOpen || !d.haveSteam) return;
    const json::Value& l = d.steam["login"];
    std::wstring code;
    d.dialogOpen = true;
    bool ok = askGuardCode(d.app.hwnd, d.dialogLook(), l.getString("codeType") == "app", widen(l.getString("hint")), code);
    d.dialogOpen = false;
    if (!ok) return;
    fdapi::JsonW j;
    j.obj().kv("code", narrow(code)).end();
    d.send(Req::Code, L"POST", "api/v1/admin/steam/code", j.take());
}

void ServerAutodl::logoutBot() {
    if (MessageBoxW(d_->app.hwnd, L"Steam-Bot abmelden?\n\nDann lädt der Server keine Demos mehr, bis du ihn wieder anmeldest. Der API-Schlüssel bleibt.",
                    L"FragDesk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    d_->send(Req::Logout, L"DELETE", "api/v1/admin/steam");
}

void ServerAutodl::onTimer() {
    Impl& d = *d_;
    bool login = d.haveSteam && pendingLogin(d.loginState);
    bool check = d.checking() && d.checkPolls < 40;
    if ((!login && !check) || d.currentKey() != d.key || d.key.empty()) {
        if (d.app.hwnd) KillTimer(d.app.hwnd, kAutodlTimer);
        d.timerOn = false;
        return;
    }
    ++d.ticks;
    if (login && !d.busy.count("getsteam")) d.send(Req::GetSteam, L"GET", "api/v1/admin/steam");
    if (check && d.ticks % 2 == 0 && !d.busy.count("get")) {   // alle 4 s
        ++d.checkPolls;
        d.send(Req::Get, L"GET", "api/v1/account/autodl");
    }
}

LRESULT ServerAutodl::onMessage(WPARAM, LPARAM lp) {
    std::unique_ptr<AutodlEvent> e(reinterpret_cast<AutodlEvent*>(lp));
    if (!e) return 0;
    Impl& d = *d_;
    App& app = d.app;
    d.busy.erase(busyKey(e->req));
    if (e->key != d.key || e->key != d.currentKey()) return 0;   // inzwischen ein anderes Konto
    json::Value v;
    if (!(e->status == 200 && json::parse(e->body, v) && v.isObject())) {
        std::wstring why = failText(e->body, e->status, e->error);
        if (e->req == Req::Get) {
            d.problem = e->status == 404 ? L"Dein Server kennt das für FragDesk noch nicht – auf dem Server den Installationsbefehl noch einmal ausführen "
                                           L"(aktualisiert, Daten bleiben)."
                                         : why;
        } else if (e->req == Req::GetSteam) {
            if (e->status == 403) d.haveSteam = false;   // kein Admin (mehr)
        } else {
            app.notify(why, true);
            if (e->req == Req::Login) d.loginState = "idle";
        }
        app.invalidate();
        return 0;
    }
    if (isSteamReq(e->req)) {
        d.steam = std::move(v);
        d.haveSteam = true;
        const json::Value& l = d.steam["login"];
        std::string state = l.getString("state", "idle"), before = d.loginState;
        std::wstring hint = widen(l.getString("hint"));
        d.loginState = state;
        if (e->req == Req::Key) app.notify(L"Schlüssel gespeichert");
        if (e->req == Req::Logout) app.notify(L"Bot abgemeldet");
        if ((e->req == Req::Key || e->req == Req::Logout) && !d.busy.count("get"))
            d.send(Req::Get, L"GET", "api/v1/account/autodl");   // „bereit“ ändert sich (Stand oben)
        if (pendingLogin(state)) d.startTimer();
        if (pendingLogin(before) && state == "done") {
            std::wstring name = widen(d.steam["bot"].getString("name"));
            app.notify(name.empty() ? L"Steam-Bot angemeldet." : L"Steam-Bot angemeldet als " + name + L".");
            d.send(Req::Get, L"GET", "api/v1/account/autodl");   // „bereit“ ändert sich
        } else if (pendingLogin(before) && state == "error") {
            app.notify(hint.empty() ? L"Die Anmeldung hat nicht geklappt." : hint, true);
        }
        // Steam will einen Code: gleich danach fragen (einmal je Stand – ein falscher Code kommt mit neuem Hinweis zurück)
        std::string sig = state + "|" + l.getString("hint") + "|" + l.getString("codeType");
        app.invalidate();
        if (state == "need-code" && sig != d.promptedSig && !d.dialogOpen) {
            d.promptedSig = sig;
            enterCode();
        }
        return 0;
    }
    d.account = std::move(v);
    d.haveAccount = true;
    d.problem.clear();
    bool ready = d.account.getBool("ready");
    switch (e->req) {
        case Req::Auth:
            app.notify(d.account.getString("knownCode").empty() ? L"Authentifizierungscode gespeichert – jetzt noch den Freigabecode einfügen."
                                                                : L"Authentifizierungscode gespeichert.");
            break;
        case Req::Known:
            app.notify(d.account.getString("authCodeMasked").empty() ? L"Freigabecode gespeichert – jetzt noch den Authentifizierungscode einfügen."
                                                                     : L"Freigabecode gespeichert.");
            break;
        case Req::Toggle:
            app.notify(e->on ? (ready ? L"Eingeschaltet – der Server prüft gleich." : L"Gespeichert – es geht los, sobald der Steam-Bot bereit ist.")
                             : L"Ausgeschaltet – deine Codes bleiben gespeichert.");
            if (e->on) d.watchCheck();
            d.refreshSteam();
            break;
        case Req::Check:
            app.notify(L"Der Server prüft gleich – neue Matches erscheinen von selbst.");
            d.watchCheck();
            break;
        case Req::Off:
            app.notify(L"Ausgeschaltet – deine Codes sind vom Server gelöscht.");
            d.refreshSteam();
            break;
        default: break;
    }
    if (d.checking()) d.startTimer();
    else if (e->req == Req::Get && d.checkPolls > 0 && d.checkPolls < 40) {   // der Server ist fertig: neue Matches gleich holen
        d.checkPolls = 40;
        app.sync->request(0);
        d.refreshSteam();
    }
    app.invalidate();
    return 0;
}

}  // namespace fdw
