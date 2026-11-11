// FragDesk – Seite „Einstellungen“
#include <algorithm>

#include <ctime>
#include <cwctype>

#include "../core/cs2con.h"
#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "mobile_host.h"
#include "server_autodl.h"
#include "server_sync.h"

namespace fdw {

using namespace ui;

namespace {

std::wstring W(const std::string& s) { return widen(s); }

struct Section {
    Canvas& c;
    float x, y, w, top;
    Section(Canvas& cv, float x_, float y_, float w_, const std::wstring& title, const std::wstring& sub) : c(cv), x(x_), y(y_), w(w_), top(y_) {
        y += 16;
        c.text(title, F::H2, c.th.text, RectF(x + 20, y, w - 40, 24));
        y += 26;
        if (!sub.empty()) {
            float h = c.textHeight(sub, F::Small, w - 40) + 2;
            c.text(sub, F::Small, c.th.text3, RectF(x + 20, y, w - 40, h), Align::Left, false, true);
            y += h;
        }
        y += 10;
    }
    // Zeile mit Beschriftung links, Inhalt rechts; liefert das Rechteck für den Inhalt
    RectF row(const std::wstring& label, const std::wstring& desc, float h = 56, float controlW = 300, Color descColor = Color(0, 0, 0, 0)) {
        c.line(x + 20, y, x + w - 20, y, alpha(c.th.border, 150), 1);
        float lw = w - 40 - controlW - 16;
        c.text(label, F::BodySemi, c.th.text, RectF(x + 20, y + (desc.empty() ? 0 : 9), lw, desc.empty() ? h : 22));
        if (!desc.empty())
            c.text(desc, F::Small, descColor.GetA() ? descColor : c.th.text3, RectF(x + 20, y + 30, lw, h - 32), Align::Left, false, true);
        RectF r(x + w - 20 - controlW, y, controlW, h);
        y += h;
        return r;
    }
    float finish() { return y - top + 14; }
};

void cardBehind(Canvas& c, float x, float y, float w, float h) { c.card(RectF(x, y, w, h)); }

// QR-Code: dunkle Module auf weißem Grund (auch im dunklen Design, sonst erkennt die Kamera ihn schlecht)
void drawQr(Canvas& c, const RectF& r, const std::string& text) {
    auto rows = qrModules(text);
    c.round(r, 10, Color(255, 255, 255, 255));
    if (rows.empty()) return;
    float n = float(rows.size()) + 8, cell = r.Width / n;   // 4 Module Ruhezone ringsum
    for (size_t y = 0; y < rows.size(); ++y)
        for (size_t x = 0; x < rows[y].size();) {
            if (!rows[y][x]) {
                ++x;
                continue;
            }
            size_t e = x;
            while (e < rows[y].size() && rows[y][e]) ++e;   // zusammenhängende Module als ein Rechteck: keine Lücken
            c.fill(RectF(r.X + (4 + x) * cell, r.Y + (4 + y) * cell, (e - x) * cell + 0.3f, cell + 0.3f), Color(255, 17, 24, 39));
            x = e;
        }
}

// Kopplung: QR-Code, Code, Restzeit und die Schritte (Höhe kPanelH)
const float kPanelH = 236;

void pairingPanel(Canvas& c, float x, float y, float cw, const std::string& url, const std::string& code, int64_t until,
                  const std::wstring& noCamera) {
    float py = y + 4, qs = 212;
    if (!url.empty()) drawQr(c, RectF(x + 20, py, qs, qs), url);
    float tx = x + 20 + qs + 28, tw = cw - 40 - qs - 28;
    c.text(L"Code", F::Small, c.th.text2, RectF(tx, py, tw, 18));
    c.text(widen(code.substr(0, 3) + " " + code.substr(3)), F::NumHuge, c.th.accentText, RectF(tx, py + 16, tw, 54));
    int64_t left = std::max<int64_t>(0, until - int64_t(time(nullptr)));
    wchar_t rest[64];
    swprintf(rest, 64, L"Noch %lld:%02lld Minuten gültig", static_cast<long long>(left / 60), static_cast<long long>(left % 60));
    c.text(rest, F::Small, c.th.text3, RectF(tx, py + 72, tw, 18));
    const std::wstring steps =
        L"1. QR-Code mit der iPhone-Kamera scannen – Safari öffnet FragDesk und verbindet sich.\n"
        L"2. In Safari auf „Teilen“ › „Zum Home-Bildschirm“ tippen.\n"
        L"3. FragDesk über das neue Symbol öffnen. Fragt es nach dem Code, gib ihn dort ein.\n" +
        noCamera;
    c.text(steps, F::Small, c.th.text2, RectF(tx, py + 100, tw, kPanelH - 104), Align::Left, false, true);
}

// „vor 2 min“ – wie lange ein Zeitpunkt her ist
std::wstring agoText(int64_t t) {
    if (t <= 0) return L"noch nie";
    int64_t s = std::max<int64_t>(0, int64_t(time(nullptr)) - t);
    if (s < 60) return L"gerade eben";
    if (s < 3600) return L"vor " + std::to_wstring(s / 60) + L" min";
    if (s < 86400) return L"vor " + std::to_wstring(s / 3600) + L" h";
    return L"am " + dateText(t);
}

// Stand des Abgleichs in beide Richtungen: „44 Matches auf dem PC · 3 vom Server geholt · 4 von 4 Videos · zuletzt vor
// 2 min“ (fehlt noch etwas auf dem Server: „38 von 41 hochgeladen“), Pause oder Fehler (col: Farbe dafür)
std::wstring syncText(Canvas& c, App& app, const SyncStatus& st, const ServerSettings& srv, Color& col) {
    col = c.th.text3;
    std::wstring counts;
    if (st.counted) {
        int onPc = int(app.lib.matches.size()), pulled = app.lib.pulledCount();
        counts = std::to_wstring(onPc) + (onPc == 1 ? L" Match auf dem PC · " : L" Matches auf dem PC · ");
        if (pulled > 0) counts += std::to_wstring(pulled) + L" vom Server geholt · ";
        if (st.matchesDone < st.matchesTotal)
            counts += std::to_wstring(st.matchesDone) + L" von " + std::to_wstring(st.matchesTotal) + L" hochgeladen · ";
        counts += srv.videos ? std::to_wstring(st.clipsDone) + L" von " + std::to_wstring(st.clipsTotal) + L" Videos" : L"Videos hochladen aus";
        if (srv.replays && st.replaysTotal > 0)
            counts += L" · 2D-Ansicht " + std::to_wstring(st.replaysDone) + L" von " + std::to_wstring(st.replaysTotal);
    }
    // älterer Server: nimmt an, gibt aber nichts heraus
    const std::wstring old = st.pullOld ? L" Dein Server gibt noch keine Matches an den PC – auf dem Server den Installationsbefehl noch einmal "
                                          L"ausführen (aktualisiert, Daten bleiben)."
                                        : L"";
    std::wstring done = counts.empty() ? std::wstring() : L" Stand: " + counts + L".";
    switch (st.state) {
        case SyncStatus::AuthFailed:
            col = c.th.danger;
            return L"Der Server lässt dieses FragDesk nicht mehr rein (anderer PC verbunden, abgemeldet oder Konto gelöscht). Hol dir in "
                   L"der Handy-App unter Einstellungen einen Link „PC neu verbinden“ (oder vom Admin), kopiere ihn und klicke „Neu verbinden“.";
        case SyncStatus::PausedCs2:   // in „Manuell“ nur der Hintergrund – ein Klick läuft trotzdem
            col = c.th.warning;
            return (app.settings.cs2Mode == fd::Cs2Mode::Performance
                        ? std::wstring(L"Pausiert – CS2 läuft. Danach geht es von selbst weiter.")
                        : std::wstring(L"Pausiert – CS2 läuft. Danach geht es von selbst weiter – oder jetzt gleich mit „Jetzt abgleichen“.")) +
                   done;
        case SyncStatus::PausedRecording: col = c.th.warning; return L"Pausiert – FragDesk nimmt gerade auf." + done;
        case SyncStatus::Full: col = c.th.danger; return st.error + L" Lösche Videos oder bitte den Admin um mehr Platz.";
        case SyncStatus::Offline: {
            col = c.th.danger;
            int64_t s = std::max<int64_t>(0, st.retryAt - int64_t(time(nullptr)));
            std::wstring when = s == 0 ? std::wstring(L"gleich") : s < 60 ? L"in " + std::to_wstring(s) + L" s" : L"in " + std::to_wstring((s + 59) / 60) + L" min";
            return st.error + L" – neuer Versuch " + when + L".";
        }
        default: break;
    }
    if (st.state == SyncStatus::Running) {
        col = c.th.text2;
        return (counts.empty() ? std::wstring() : counts + L" · ") + (st.current.empty() ? std::wstring(L"lädt hoch …") : st.current);
    }
    if (!st.counted) return srv.lastSync ? L"Zuletzt abgeglichen " + agoText(srv.lastSync) : std::wstring(L"Wird gleich abgeglichen …");
    return counts + L" · zuletzt " + agoText(srv.lastSync) + (old.empty() ? L"" : L"." + old);
}

// Höhe einer Zeile, deren Beschreibung umbricht
float rowHeight(Canvas& c, float cw, const std::wstring& desc, float controlW, float minH) {
    return std::max(minH, 30 + c.textHeight(desc, F::Small, cw - 40 - controlW - 16) + 14);
}

// Modus „Eigener Server“: Zeilen unter „Verbindung“. sec == nullptr: nur die Höhe bestimmen.
float serverRows(Canvas& c, App& app, float x, float cw, Section* sec) {
    ServerSync& sy = *app.sync;
    ServerSettings& srv = app.settings.mobile.srv;
    float h = 0;
    if (srv.token.empty()) {   // noch nicht verbunden
        const std::wstring join =
            L"Du brauchst eine Einladung vom Admin deines FragDesk-Servers – oder für dein bisheriges Konto einen Link „PC neu verbinden“ "
            L"aus der Handy-App (Einstellungen): Link kopieren (https://…/#invite=…), dann hier einfügen.";
        float rh = rowHeight(c, cw, join, 220, 72);
        if (sec) {
            RectF r = sec->row(L"Mit Server verbinden", join, rh, 220);
            bool busy = sy.connecting();
            c.button(RectF(r.X + r.Width - 210, r.Y + (rh - 36) / 2, 210, 36), busy ? L"Verbindet …" : L"Einladung einfügen", "s-join",
                     [&app] { app.sync->connectFromClipboard(); }, Btn::Primary, Icon::Plus, !busy, L"Kopiere vorher den Einladungslink");
        }
        h += rh;
        const std::wstring how =
            L"Ein kleiner Linux-Server reicht, z. B. ein VPS für ein paar Euro im Monat. Er hält Stats und Clips für dich und deine Freunde "
            L"bereit – die Anleitung steht im README.";
        float rh2 = rowHeight(c, cw, how, 170, 72);
        if (sec) {
            RectF r = sec->row(L"Eigenen Server einrichten", how, rh2, 170);
            c.button(RectF(r.X + r.Width - 160, r.Y + (rh2 - 36) / 2, 160, 36), L"Anleitung", "s-howto",
                     [] { openInShell(std::wstring(kRepoUrl) + L"/tree/main/apps/fragdesk#eigener-server"); }, Btn::Secondary, Icon::External);
        }
        return h + rh2;
    }

    const SyncStatus& st = sy.status();
    // Server und Konto
    std::wstring who = L"Konto: " + (srv.user.empty() ? std::wstring(L"–") : widen(srv.user)) + (srv.admin ? L" · Admin" : L"");
    if (sec) {
        RectF r = sec->row(sy.serverLabel(), who, 64, 160);
        c.button(RectF(r.X + r.Width - 140, r.Y + 14, 140, 36), L"Trennen", "s-leave", [&app] { app.sync->disconnect(); }, Btn::Ghost, Icon::Close,
                 true, L"Abmelden und Adresse samt Schlüssel vergessen");
    }
    h += 64;
    // Speicher (bis der Server antwortet: Stand vom letzten Abgleich)
    int64_t usedB = st.used >= 0 ? st.used : srv.used, limitB = st.used >= 0 ? st.limit : srv.limit;
    std::wstring used;
    double share = 0;
    if (usedB < 0) used = L"Noch unbekannt – kommt mit dem ersten Abgleich";
    else if (limitB > 0) {
        share = double(usedB) / double(limitB);
        used = widen(fd::fmtBytes(double(usedB))) + L" von " + widen(fd::fmtBytes(double(limitB))) + L" belegt (" +
               std::to_wstring(int(std::min(100.0, share * 100) + 0.5)) + L" %)";
    } else used = widen(fd::fmtBytes(double(usedB))) + L" belegt · ohne Limit";
    if (sec) {
        RectF r = sec->row(L"Speicher auf dem Server", used, 64, 240);
        if (usedB >= 0 && limitB > 0)
            c.progress(RectF(r.X + r.Width - 220, r.Y + 28, 220, 8), share, share > 0.9 ? c.th.danger : share > 0.75 ? c.th.warning : c.th.accent);
    }
    h += 64;
    // Stand
    Color col;
    std::wstring state = syncText(c, app, st, srv, col);
    float rh = rowHeight(c, cw, state, 200, 72);
    if (sec) {
        RectF r = sec->row(L"Abgleich", state, rh, 200, col);
        RectF b(r.X + r.Width - 190, r.Y + (rh - 36) / 2, 190, 36);
        if (st.state == SyncStatus::AuthFailed)
            c.button(b, sy.connecting() ? L"Verbindet …" : L"Neu verbinden", "s-rejoin", [&app] { app.sync->connectFromClipboard(); }, Btn::Primary,
                     Icon::Plus, !sy.connecting(), L"Kopiere vorher den neuen Einladungslink");
        else
            c.button(b, L"Jetzt abgleichen", "s-now", [&app] { app.sync->request(0, SyncKick::Now); }, Btn::Secondary, Icon::Refresh,
                     st.state != SyncStatus::Running,
                     app.settings.cs2Mode == fd::Cs2Mode::Performance ? L"Nie während CS2 läuft – dann geht es danach weiter"
                                                                      : L"Gleicht sofort ab – auch während CS2 läuft");
        if (app.hwnd && !app.renderMode) SetTimer(app.hwnd, kSyncViewTimer, 30000, nullptr);   // „vor … min“ frisch halten
    }
    h += rh;
    // Videos
    if (sec) {
        RectF r = sec->row(L"Videos hochladen", L"Highlight-Videos in Stücken à 4 MB – ein unterbrochener Upload geht an der Stelle weiter.", 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), srv.videos, "t-svideos", [&app] {
            app.settings.mobile.srv.videos = !app.settings.mobile.srv.videos;
            app.saveSettings();
            app.sync->request(0);
            app.invalidate();
        });
    }
    h += 64;
    // 2D-Ansicht (ein älterer Server nimmt keine an: dann steht hier, was zu tun ist)
    const fd::Cs2Mode mode = app.settings.cs2Mode;
    const std::wstring replays = srv.replays && st.replaysOld
        ? L"Dein Server kennt die 2D-Ansicht noch nicht – führ auf dem Server den Installationsbefehl noch einmal aus (aktualisiert, "
          L"Daten bleiben). Bis dahin erstellt FragDesk dafür nichts im Hintergrund und lädt nichts hoch."
        : std::wstring(L"Positionen jeder Runde für die 2D-Ansicht in der Handy-App, rund 2 MB pro Match. Neue Matches bekommen sie beim "
                       L"Auswerten, für ältere erstellt FragDesk sie im Hintergrund aus der Demo – ") +
              (mode == fd::Cs2Mode::Performance ? L"eins nach dem anderen und nie, solange CS2 läuft."
               : mode == fd::Cs2Mode::Manual    ? L"eins nach dem anderen; solange CS2 läuft, nur nach „Jetzt abgleichen“."
                                                : L"eins nach dem anderen, mit niedrigster Priorität.");
    float rr = rowHeight(c, cw, replays, 60, 64);
    if (sec) {
        RectF r = sec->row(L"2D-Ansicht hochladen", replays, rr, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + (rr - 24) / 2, 44, 24), srv.replays, "t-sreplays", [&app] {
            app.settings.mobile.srv.replays = !app.settings.mobile.srv.replays;
            app.saveSettings();
            app.sync->apply();     // auch: beim Auswerten gleich mit erzeugen (oder nicht mehr)
            app.sync->request(0);
            app.invalidate();
        });
    }
    h += rr;
    // Handy koppeln
    std::string code = sy.pairingCode();
    bool rejected = st.state == SyncStatus::AuthFailed;   // erst neu verbinden
    if (sec) {
        RectF r = sec->row(L"Handy verbinden",
                           code.empty() ? L"Zeigt einen QR-Code: mit der iPhone-Kamera scannen, fertig. Das Handy liest dann vom Server." : L"", 64, 200);
        if (code.empty())
            c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), sy.pairingBusy() ? L"Code wird geholt …" : L"Handy verbinden", "s-pair",
                     [&app] { app.sync->startPairing(); }, Btn::Primary, Icon::Plus, !sy.pairingBusy() && !rejected,
                     rejected ? L"Erst neu verbinden" : L"");
        else
            c.button(RectF(r.X + r.Width - 140, r.Y + 14, 140, 36), L"Fertig", "s-pairend", [&app] {
                app.sync->stopPairing();
                app.invalidate();
            }, Btn::Secondary);
        if (!code.empty()) {
            pairingPanel(c, x, sec->y, cw, sy.pairingUrl(), code, sy.pairingUntil(),
                         L"Ohne Kamera: " + widen(sy.pairingBase()) + L" in Safari öffnen und den Code eingeben.");
            sec->y += kPanelH;
        }
    }
    h += 64 + (code.empty() ? 0 : kPanelH);
    // Freunde einladen (nur Admins)
    if (srv.admin) {
        if (sec) {
            RectF r = sec->row(L"Freund einladen", L"Erstellt einen Einladungslink (7 Tage gültig, nur einmal nutzbar) und kopiert ihn – per Messenger schicken.",
                               64, 200);
            c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), sy.inviteBusy() ? L"Wird erstellt …" : L"Freund einladen", "s-invite",
                     [&app] { app.sync->invite(); }, Btn::Secondary, Icon::Users, !sy.inviteBusy() && !rejected, rejected ? L"Erst neu verbinden" : L"");
        }
        h += 64;
    }
    return h;
}

// ---- Demos automatisch laden (eigener Server, verbunden) ---------------------------------------------------------------
// Wie in der Handy-App: Stand, ein/aus, beide Codes aus der Zwischenablage, „Jetzt prüfen“, „Ausschalten“; für Admins der
// Steam-Bot des Servers. sec == nullptr: nur die Höhe bestimmen.
const wchar_t* kSteamHelp = L"https://help.steampowered.com/de/wizard/HelpWithGameIssue/?appid=730&issueid=128";
const wchar_t* kKeyPage = L"https://steamcommunity.com/dev/apikey";

bool pendingLogin(const std::string& s) { return s == "working" || s == "need-code" || s == "need-confirm"; }

// Der Stand kommt vom Server und ist fürs Handy geschrieben: am PC steht der Steam-Bot weiter unten, der Spieler oben
std::wstring pcStatus(std::wstring s) {
    auto swap = [&s](const std::wstring& from, const std::wstring& to) {
        size_t at = s.find(from);
        if (at != std::wstring::npos) s.replace(at, from.size(), to);
    };
    swap(L"unter Server verwalten › Steam-Bot", L"unten unter „Steam-Bot“");
    swap(L": Server verwalten › Steam-Bot", L" – unten unter „Steam-Bot“");
    swap(L"Erst in FragDesk am PC deinen Spieler wählen", L"Erst oben unter „Dein Konto“ deinen Spieler wählen");
    return s;
}

void subHeading(Canvas& c, float x, float cw, Section* sec, const std::wstring& title) {
    if (!sec) return;
    c.line(x + 20, sec->y, x + cw - 20, sec->y, alpha(c.th.border, 150), 1);
    c.text(title, F::SmallSemi, c.th.text2, RectF(x + 20, sec->y + 12, cw - 40, 20));
    sec->y += 40;
}

float steamBotRows(Canvas& c, App& app, float x, float cw, Section* sec) {
    ServerAutodl& ad = *app.autodl;
    float h = 40;
    subHeading(c, x, cw, sec, L"Steam-Bot – einmal für den ganzen Server (nur Admins)");
    if (!ad.steamLoaded()) {
        if (sec) sec->row(L"Steam-Bot", L"Wird vom Server geholt …", 64, 10);
        return h + 64;
    }
    const json::Value& s = ad.steam();
    const json::Value& b = s["bot"];
    const json::Value& l = s["login"];
    std::string state = l.getString("state", "idle"), codeType = l.getString("codeType");
    bool loggedIn = b.getBool("loggedIn"), needs = b.getBool("needsLogin"), pending = pendingLogin(state);
    std::wstring name = W(b.getString("name")), helper = W(s.getString("helper")), hint = W(l.getString("hint"));
    // Bot
    std::wstring title = needs ? L"Neu anmelden nötig" + (name.empty() ? L"" : L": " + name) : loggedIn ? L"Angemeldet als " + (name.empty() ? L"Bot" : name)
                                                                                                        : L"Nicht angemeldet";
    int64_t until = int64_t(b.getNumber("tokenUntil"));
    std::wstring desc = !helper.empty() ? helper
                        : needs         ? (b.getString("problem").empty() ? L"Steam nimmt die Anmeldung des Bots nicht mehr an." : W(b.getString("problem")))
                        : loggedIn      ? (until > 0 ? L"Token gültig bis " + dateText(until) + L" – dann hier neu anmelden." : L"Token ohne Ablaufdatum.")
                                        : L"Ein eigenes Steam-Konto des Servers (nicht deins) holt die Adressen der Demos – mit CS2 in der Bibliothek, "
                                          L"5 € Umsatz und Steam Guard über die Steam-App. Die Anleitung steht im README.";
    // Platz rechts nach den Knöpfen (Schrift und Maßstab ändern ihre Breite), damit lange Texte nie darunter laufen
    std::wstring label = pending ? L"Neu anfangen …" : loggedIn || needs ? L"Bot neu anmelden …" : L"Bot anmelden …";
    bool guide = !loggedIn && !needs;
    float bw = c.buttonWidth(label, Icon::Shield), gw = guide ? c.buttonWidth(L"Anleitung", Icon::External) : 0;
    float cw1 = std::max(300.0f, bw + (guide ? 8 + gw : 0));
    float rh = rowHeight(c, cw, desc, cw1, 72);
    if (sec) {
        RectF r = sec->row(title, desc, rh, cw1, !helper.empty() || needs ? c.th.danger : Color(0, 0, 0, 0));
        c.button(RectF(r.X + r.Width - bw, r.Y + (rh - 36) / 2, bw, 36), label, "ad-login", [&app] { app.autodl->loginBot(); },
                 loggedIn || pending ? Btn::Secondary : Btn::Primary, Icon::Shield, helper.empty() && !ad.busy("login"),
                 helper.empty() ? L"Benutzername und Passwort des Bot-Kontos – bleiben nur kurz im Arbeitsspeicher des Servers" : helper);
        if (guide)
            c.button(RectF(r.X + r.Width - bw - 8 - gw, r.Y + (rh - 36) / 2, gw, 36), L"Anleitung", "ad-guide",
                     [] { openInShell(std::wstring(kRepoUrl) + L"/tree/main/apps/fragdesk#demos-automatisch-laden"); }, Btn::Ghost, Icon::External);
    }
    h += rh;
    // laufende Anmeldung: Code oder Bestätigen in der Steam-App (der Stand kommt alle 2 s vom Server)
    if (pending || state == "error") {
        std::wstring t = state == "working"        ? L"Anmeldung läuft …"
                         : state == "need-code"    ? (codeType == "app" ? L"Code aus der Steam-App" : L"Code aus der E-Mail von Steam")
                         : state == "need-confirm" ? L"In der Steam-App bestätigen"
                                                   : L"Anmeldung hat nicht geklappt";
        bool code = state == "need-code" || (state == "need-confirm" && codeType == "app");
        float rh2 = rowHeight(c, cw, hint, code ? 200 : 10, 64);
        if (sec) {
            RectF r = sec->row(t, hint, rh2, code ? 200 : 10, state == "error" ? c.th.danger : c.th.accentText);
            if (code)
                c.button(RectF(r.X + r.Width - 180, r.Y + (rh2 - 36) / 2, 180, 36), L"Code eingeben …", "ad-code", [&app] { app.autodl->enterCode(); },
                         state == "need-code" ? Btn::Primary : Btn::Secondary, Icon::Shield, !ad.busy("code"));
        }
        h += rh2;
    }
    // Web-API-Schlüssel
    bool key = s.getBool("apiKeySet");
    std::wstring kd = key ? L"Gesetzt – ein neuer ersetzt ihn." : L"Fehlt noch. Als Bot auf steamcommunity.com/dev/apikey holen (eine Domain eintragen, z. B. "
                                                                    L"die Adresse deines Servers), kopieren, hier einfügen. Nimm den des Bots, nicht deinen eigenen.";
    float kw = c.buttonWidth(L"Schlüssel einfügen", Icon::Plus), pw = c.buttonWidth(L"Seite öffnen", Icon::External);
    float cw3 = std::max(300.0f, kw + 8 + pw);
    float rh3 = rowHeight(c, cw, kd, cw3, 64);
    if (sec) {
        RectF r = sec->row(L"Web-API-Schlüssel", kd, rh3, cw3);
        c.button(RectF(r.X + r.Width - kw, r.Y + (rh3 - 36) / 2, kw, 36), L"Schlüssel einfügen", "ad-key", [&app] { app.autodl->pasteApiKey(); },
                 key ? Btn::Secondary : Btn::Primary, Icon::Plus, !ad.busy("key"), L"Kopiere vorher den Schlüssel (32 Zeichen, 0–9 und A–F)");
        c.button(RectF(r.X + r.Width - kw - 8 - pw, r.Y + (rh3 - 36) / 2, pw, 36), L"Seite öffnen", "ad-keypage", [] { openInShell(kKeyPage); }, Btn::Ghost,
                 Icon::External);
    }
    h += rh3;
    // Letzter Lauf
    const json::Value& run = s["lastRun"];
    int n = int(s.getNumber("accounts"));
    std::wstring rd = run.getBool("running") ? std::wstring(L"Läuft gerade …")
                      : run.getNumber("at") > 0 ? dateTimeText(int64_t(run.getNumber("at"))) + (run.getString("text").empty() ? L"" : L" · " + W(run.getString("text")))
                                                : std::wstring(L"Noch keiner");
    rd += n > 0 ? L" · " + std::to_wstring(n) + (n == 1 ? L" Konto hat es eingeschaltet" : L" Konten haben es eingeschaltet") : L" · noch von keinem Konto eingeschaltet";
    float rh4 = rowHeight(c, cw, rd, 10, 64);
    if (sec) sec->row(L"Letzter Lauf", rd, rh4, 10);
    h += rh4;
    if (loggedIn || needs) {
        const std::wstring od = L"Dann lädt der Server keine Demos mehr, bis du ihn wieder anmeldest. Der API-Schlüssel bleibt.";
        float rh5 = rowHeight(c, cw, od, 180, 64);
        if (sec) {
            RectF r = sec->row(L"Bot abmelden", od, rh5, 180);
            c.button(RectF(r.X + r.Width - 170, r.Y + (rh5 - 36) / 2, 170, 36), L"Bot abmelden", "ad-logout", [&app] { app.autodl->logoutBot(); }, Btn::Danger,
                     Icon::Close, !ad.busy("logout"));
        }
        h += rh5;
    }
    return h;
}

float autodlRows(Canvas& c, App& app, float x, float cw, Section* sec) {
    ServerAutodl& ad = *app.autodl;
    if (!ad.loaded()) {
        std::wstring t = ad.problem().empty() ? L"Wird vom Server geholt …" : ad.problem();
        float rh = rowHeight(c, cw, t, 170, 64);
        if (sec) {
            RectF r = sec->row(L"Stand", t, rh, 170, ad.problem().empty() ? Color(0, 0, 0, 0) : c.th.danger);
            if (!ad.problem().empty())
                c.button(RectF(r.X + r.Width - 160, r.Y + (rh - 36) / 2, 160, 36), L"Noch einmal", "ad-retry", [&app] { app.autodl->refresh(true); },
                         Btn::Secondary, Icon::Refresh, !ad.busy("get"));
        }
        return rh;
    }
    const json::Value& a = ad.account();
    bool enabled = a.getBool("enabled"), hasSteamId = !a.getString("steamId").empty(), ready = a.getBool("ready"), checking = a.getBool("checking");
    std::wstring masked = W(a.getString("authCodeMasked")), known = W(a.getString("knownCode")), status = pcStatus(W(a.getString("status", "–")));
    bool saved = !masked.empty() && !known.empty();
    std::string tone = a.getString("tone");
    Color dotCol = tone == "ok" ? c.th.success : tone == "error" ? c.th.danger : tone == "off" ? c.th.text3 : c.th.warning;
    float h = 0;
    // Stand und Schalter
    float rh = rowHeight(c, cw, status, 70, 64);
    if (sec) {
        RectF r = sec->row(L"Stand", status, rh, 70, tone == "error" ? c.th.danger : Color(0, 0, 0, 0));
        c.dot(x + 20 + c.measure(L"Stand", F::BodySemi) + 10, r.Y + 20, 4.5f, dotCol);
        if (saved && hasSteamId)
            c.toggle(RectF(r.X + r.Width - 46, r.Y + (rh - 24) / 2, 44, 24), enabled, "t-autodl", [&app, enabled] {
                if (!app.autodl->busy("toggle")) app.autodl->setEnabled(!enabled);
            });
    }
    h += rh;
    const std::wstring noPlayer = L"Erst oben unter „Dein Konto“ deinen Spieler wählen (die SteamID kommt vom PC)";
    // Authentifizierungscode
    std::wstring ad1 = masked.empty() ? L"Noch nicht eingetragen. Auf der Steam-Seite „Authentifizierungscode erstellen“, kopieren und hier einfügen."
                                      : masked + L" – nur gekürzt zu sehen. Damit kann der Server nur deinen Spielverlauf abfragen, dich nicht anmelden.";
    const std::wstring label1 = L"Authentifizierungscode einfügen";
    float bw1 = c.buttonWidth(label1, Icon::Plus), cw1 = std::max(260.0f, bw1);   // Platz nach dem Knopf (Schrift, Maßstab)
    float rh1 = rowHeight(c, cw, ad1, cw1, 64);
    if (sec) {
        RectF r = sec->row(L"Authentifizierungscode", ad1, rh1, cw1);
        c.button(RectF(r.X + r.Width - bw1, r.Y + (rh1 - 36) / 2, bw1, 36), label1, "ad-auth", [&app] { app.autodl->pasteAuthCode(); },
                 masked.empty() ? Btn::Primary : Btn::Secondary, Icon::Plus, hasSteamId && !ad.busy("auth"),
                 hasSteamId ? L"Kopiere vorher den Code (AAAA-AAAAA-AAAA)" : noPlayer);
    }
    h += rh1;
    // Freigabecode
    std::wstring ad2 = known.empty() ? L"Steht auf derselben Steam-Seite unter dem Authentifizierungscode (beginnt mit CSGO-). Geladen werden die Matches danach."
                                     : known + L" – zählt von selbst weiter. Neu einfügen nur, wenn der Server sagt, dass er nicht passt.";
    const std::wstring label2 = L"Freigabecode einfügen";
    float bw2 = c.buttonWidth(label2, Icon::Plus), cw2 = std::max(260.0f, bw2);
    float rh2 = rowHeight(c, cw, ad2, cw2, 64);
    if (sec) {
        RectF r = sec->row(L"Freigabecode deines letzten Matches", ad2, rh2, cw2);
        c.button(RectF(r.X + r.Width - bw2, r.Y + (rh2 - 36) / 2, bw2, 36), label2, "ad-known", [&app] { app.autodl->pasteShareCode(); },
                 known.empty() && !masked.empty() ? Btn::Primary : Btn::Secondary, Icon::Plus, hasSteamId && !ad.busy("known"),
                 hasSteamId ? L"Kopiere vorher den Code (CSGO-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx)" : noPlayer);
    }
    h += rh2;
    // Steam-Seite
    const std::wstring ad3 = L"Beide Codes stehen auf derselben Steam-Seite (mit deinem eigenen Steam-Konto). Dort kopieren – FragDesk nimmt sie aus der "
                             L"Zwischenablage.";
    float bw3 = c.buttonWidth(L"Codes bei Steam holen", Icon::External), cw3 = std::max(240.0f, bw3);
    float rh3 = rowHeight(c, cw, ad3, cw3, 64);
    if (sec) {
        RectF r = sec->row(L"Codes bei Steam holen", ad3, rh3, cw3);
        c.button(RectF(r.X + r.Width - bw3, r.Y + (rh3 - 36) / 2, bw3, 36), L"Codes bei Steam holen", "ad-steam", [] { openInShell(kSteamHelp); },
                 Btn::Ghost, Icon::External);
    }
    h += rh3;
    // Jetzt prüfen
    if (saved) {
        int64_t lastNew = int64_t(a.getNumber("lastNew"));
        std::wstring ad4 = (lastNew > 0 ? L"Neuestes Match geladen am " + dateText(lastNew) + L". " : std::wstring()) +
                           L"Sonst prüft der Server alle " + std::to_wstring(int(a.getNumber("every", 15))) + L" Minuten. Neue Matches holt FragDesk danach von selbst.";
        float rh4 = rowHeight(c, cw, ad4, 180, 64);
        if (sec) {
            RectF r = sec->row(L"Jetzt prüfen", ad4, rh4, 180);
            c.button(RectF(r.X + r.Width - 170, r.Y + (rh4 - 36) / 2, 170, 36), checking ? L"Prüft …" : L"Jetzt prüfen", "ad-check",
                     [&app] { app.autodl->checkNow(); }, Btn::Secondary, Icon::Refresh, enabled && ready && !checking && !ad.busy("check"),
                     !enabled ? L"Erst einschalten" : !ready ? L"Der Steam-Bot ist noch nicht bereit" : L"");
        }
        h += rh4;
    }
    // Ausschalten (Codes löschen)
    if (saved || enabled || !masked.empty() || !known.empty()) {
        const std::wstring ad5 = L"Schaltet ab und löscht beide Codes vom Server.";
        float rh5 = rowHeight(c, cw, ad5, 180, 64);
        if (sec) {
            RectF r = sec->row(L"Ausschalten", ad5, rh5, 180);
            c.button(RectF(r.X + r.Width - 170, r.Y + (rh5 - 36) / 2, 170, 36), L"Ausschalten", "ad-off", [&app] { app.autodl->turnOff(); }, Btn::Danger,
                     Icon::Close, !ad.busy("off"));
        }
        h += rh5;
    }
    if (app.settings.mobile.srv.admin) h += steamBotRows(c, app, x, cw, sec);
    return h;
}

float autodlCard(Canvas& c, App& app, float x, float y, float cw) {
    app.autodl->refresh();   // höchstens jede Minute (die Anfrage läuft im Hintergrund)
    const std::wstring sub =
        L"Dein Server holt neue Premier-, Wettkampf- und Wingman-Matches selbst bei Valve und wertet sie aus – auch wenn dein PC aus ist. "
        L"FragDesk holt sie danach vom Server. Valve hebt Demos nur etwa einen Monat auf; FACEIT geht so nicht (das lädt FragDesk hoch).";
    float subH = c.textHeight(sub, F::Small, cw - 40) + 2;
    float h = 16 + 26 + subH + 10 + autodlRows(c, app, x, cw, nullptr) + 14;
    cardBehind(c, x, y, cw, h);
    Section sec(c, x, y, cw, L"Demos automatisch laden", sub);
    autodlRows(c, app, x, cw, &sec);
    return sec.finish();
}

float mobileCard(Canvas& c, App& app, float x, float y, float cw) {
    MobileHost& mh = *app.mobile;
    MobileSettings& ms = app.settings.mobile;
    const bool server = ms.server;
    const fd::Cs2Mode mode = app.settings.cs2Mode;
    const std::wstring sub =
        server ? std::wstring(L"Deine Stats und Clips auf dem Handy – über deinen eigenen Server als Drehscheibe: FragDesk lädt neue Matches "
                              L"und Videos im Hintergrund hoch und holt, was der Server sonst hat (z. B. selbst bei Valve geladene Matches) – ") +
                     (mode == fd::Cs2Mode::Performance ? L"nie, solange CS2 läuft."
                      : mode == fd::Cs2Mode::Manual    ? L"solange CS2 läuft, nur nach „Jetzt abgleichen“."
                                                       : L"auch, während CS2 läuft.") +
                     L" Das Handy liest vom Server, auch wenn der PC aus ist. FragDesk selbst nimmt dann keine Verbindungen an."
               : L"Deine Stats und Clips auf dem Handy: FragDesk liefert dafür eine kleine Web-App aus, die du auf den Home-Bildschirm legst – "
                 L"nur an Handys, die du hier koppelst. Solange es aus ist, nimmt FragDesk keine Verbindungen an.";
    const std::wstring modeDesc = server ? L"Das Handy liest vom Server – auch unterwegs und wenn der PC aus ist."
                                         : L"Das Handy holt die Daten bei FragDesk – der PC muss dafür an sein.";
    const std::vector<std::wstring> modes = {L"Direkt vom PC", L"Eigener Server"};
    auto modeRow = [&](Section& sec) {
        RectF r = sec.row(L"Verbindung", modeDesc, 64, 300);
        float mw = c.chipRowWidth(modes);
        c.chipRow(r.X + r.Width - mw, r.Y + 17, modes, server ? 1 : 0, "m-mode", [&app](int i) {
            MobileSettings& m = app.settings.mobile;
            if (m.server == (i == 1)) return;
            m.server = i == 1;
            app.mobile->stopPairing();
            app.sync->stopPairing();
            app.saveSettings();
            app.mobile->apply();   // im Modus „Eigener Server“ lauscht FragDesk nicht
            app.sync->apply();
            app.invalidate();
        });
    };
    if (server) {
        float subH = c.textHeight(sub, F::Small, cw - 40) + 2;
        float h = 16 + 26 + subH + 10 + 64 + serverRows(c, app, x, cw, nullptr) + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Handy-App", sub);
        modeRow(sec);
        serverRows(c, app, x, cw, &sec);
        return sec.finish();
    }

    std::wstring status;
    if (!ms.enabled) status = L"Aus";
    else if (!mh.error().empty()) status = L"Startet nicht: " + mh.error();
    else if (mh.running())
        status = L"An (Port " + std::to_wstring(mh.port()) + L"). Beim ersten Mal fragt Windows, ob FragDesk Verbindungen annehmen darf – "
                 L"„Zugriff zulassen“.";
    else status = L"Startet …";
    std::vector<MobileAddress> addrs = ms.enabled ? mh.addresses() : std::vector<MobileAddress>();
    std::vector<MobileDevice> devs = mh.devices();
    std::string code = mh.pairingCode();
    bool on = ms.enabled && mh.running();
    bool ts = false;
    for (const auto& a : addrs)
        if (a.label == L"Tailscale") ts = true;

    float subH = c.textHeight(sub, F::Small, cw - 40) + 2;
    float h = 16 + 26 + subH + 10 + 64 + 72;
    if (on) h += 64 + 72 + 64 + (code.empty() ? 0 : kPanelH) + (devs.empty() ? 0 : 40 + devs.size() * 56.0f);
    h += 14;
    cardBehind(c, x, y, cw, h);
    Section sec(c, x, y, cw, L"Handy-App", sub);
    modeRow(sec);

    RectF r = sec.row(L"Handy-App erlauben", status, 72, 60);
    c.toggle(RectF(r.X + r.Width - 46, r.Y + 24, 44, 24), ms.enabled, "t-mobile", [&app] {
        app.settings.mobile.enabled = !app.settings.mobile.enabled;
        if (!app.settings.mobile.enabled) app.mobile->stopPairing();
        app.saveSettings();
        app.mobile->apply();
        app.invalidate();
    });
    if (!on) return sec.finish();

    // Adresse
    std::wstring where = addrs.empty() ? L"Kein Netzwerk gefunden" : addrs[0].url + L"  (" + addrs[0].label + L")";
    if (addrs.size() > 1) where += L"\nauch: " + addrs[1].url + L"  (" + addrs[1].label + L")";
    r = sec.row(L"Adresse fürs Handy", where, 64, 200);
    // Eigene Adresse (z. B. die HTTPS-Adresse von „tailscale serve“) aus der Zwischenablage übernehmen
    if (ms.address.empty())
        c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), L"Eigene einfügen", "m-addr", [&app] {
            std::wstring text = clipboardText(app.hwnd);
            bool ok = (text.rfind(L"https://", 0) == 0 || text.rfind(L"http://", 0) == 0) && text.size() < 200 &&
                      text.find_first_of(L" \t\r\n\"<>") == std::wstring::npos;
            if (!ok) {
                app.notify(L"In der Zwischenablage steht keine Adresse (http://… oder https://…).", true);
                return;
            }
            if (text.back() != L'/') text += L'/';
            app.settings.mobile.address = narrow(text);
            app.saveSettings();
            app.mobile->apply();
            app.notify(L"Adresse fürs Handy: " + text);
        }, Btn::Secondary, Icon::Plus, true, L"Kopiere vorher die Adresse, z. B. die HTTPS-Adresse von „tailscale serve“");
    else
        c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), L"Automatisch", "m-addr0", [&app] {
            app.settings.mobile.address.clear();
            app.saveSettings();
            app.mobile->apply();
        }, Btn::Secondary, Icon::Refresh, true, L"Eigene Adresse entfernen und wieder automatisch wählen");

    // Unterwegs
    r = sec.row(L"Unterwegs", ts ? L"Tailscale ist verbunden: Dein Handy erreicht FragDesk auch unterwegs, wenn dort Tailscale an ist."
                                 : L"Für unterwegs: Tailscale (kostenlos) auf PC und Handy installieren und mit demselben Konto anmelden. Ohne "
                                   L"Tailscale geht die Handy-App nur im Heimnetz.",
                72, ts ? 10 : 190);
    if (!ts)
        c.button(RectF(r.X + r.Width - 180, r.Y + 18, 180, 36), L"Tailscale laden", "ts-get", [] { openInShell(L"https://tailscale.com/download"); },
                 Btn::Secondary, Icon::External);

    // Koppeln
    r = sec.row(L"Handy verbinden", code.empty() ? L"Zeigt einen QR-Code: mit der iPhone-Kamera scannen, fertig." : L"", 64, 200);
    if (code.empty())
        c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), L"Handy verbinden", "m-pair", [&app] {
            app.mobile->startPairing();
            app.invalidate();
        }, Btn::Primary, Icon::Plus);
    else
        c.button(RectF(r.X + r.Width - 140, r.Y + 14, 140, 36), L"Fertig", "m-pairend", [&app] {
            app.mobile->stopPairing();
            app.invalidate();
        }, Btn::Secondary);
    if (!code.empty()) {
        std::string url = addrs.empty() ? std::string() : narrow(addrs[0].url) + "?pair=" + code;
        pairingPanel(c, x, sec.y, cw, url, code, mh.pairingUntil(), L"Ohne Kamera: die Adresse oben in Safari öffnen und den Code eingeben.");
        sec.y += kPanelH;
    }

    // Gekoppelte Geräte
    if (!devs.empty()) {
        c.line(x + 20, sec.y, x + cw - 20, sec.y, alpha(c.th.border, 150), 1);
        c.text(L"Gekoppelte Handys", F::SmallSemi, c.th.text2, RectF(x + 20, sec.y + 12, cw - 40, 20));
        sec.y += 40;
        for (const auto& d : devs) {
            std::wstring desc = L"Gekoppelt am " + dateText(d.created) + L" · zuletzt " + relativeDay(d.lastSeen);
            RectF rr = sec.row(widen(d.name), desc, 56, 140);
            std::string token = d.token;
            c.button(RectF(rr.X + rr.Width - 130, rr.Y + 11, 130, 34), L"Entfernen", "m-del:" + token.substr(0, 12), [&app, token] {
                app.mobile->removeDevice(token);
                app.invalidate();
            }, Btn::Ghost, Icon::Trash);
        }
    }
    return sec.finish();
}

}  // namespace

float drawSettings(Canvas& c, App& app, float w) {
    const float pad = 32;
    float x = pad, cw = std::min(w - pad * 2, 980.0f), y = 28;
    Settings& s = app.settings;
    bool toServer = s.mobile.server && !s.mobile.srv.token.empty();
    y += pageTitle(c, x, y, cw, L"Einstellungen",
                   toServer ? L"Gespeichert auf diesem PC und auf deinem eigenen Server – FragDesk gleicht Matches in beide Richtungen ab"
                            : L"Alles wird nur auf diesem PC gespeichert") +
         14;

    // ---- Spieler
    {
        float h = 16 + 26 + 18 + 10 + 64 + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Dein Konto", L"Für dieses Konto berechnet FragDesk die Statistiken.");
        std::wstring who = app.meName.empty() ? L"Nicht erkannt" : app.meName;
        RectF r = sec.row(who, app.me.empty() ? L"Wähle dein Konto aus" : L"SteamID " + W(app.me), 64, 220);
        c.button(RectF(r.X + r.Width - 200, r.Y + 14, 200, 36), L"Konto wählen", "acc", [&app] {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(app.hwnd, &p);
            app.chooseAccount(p.x, p.y);
        }, Btn::Secondary, Icon::ChevronDown);
        y += h + 16;
    }

    // ---- Demos
    {
        size_t extra = s.folders.size();
        float h = 16 + 26 + 36 + 10 + 64 * 3 + extra * 52 + 56 + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Demos finden",
                    L"FragDesk schaut in diese Ordner und bietet neue Demos zum Auswerten an. Es öffnet dabei nur Dateien, nie andere Programme.");
        std::wstring replays = app.steam.replaysDir();
        RectF r = sec.row(L"CS2-Downloads (Premier, Wettkampf)", replays.empty() ? L"CS2 nicht gefunden" : replays, 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.watchReplays, "t-replays", [&app] {
            app.settings.watchReplays = !app.settings.watchReplays;
            app.saveSettings();
            app.applyFolders();
            app.lib.startWatching();
            app.lib.scanAsync();
            app.invalidate();
        });
        r = sec.row(L"Downloads-Ordner (FACEIT)", downloadsDir(), 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.watchDownloads, "t-dl", [&app] {
            app.settings.watchDownloads = !app.settings.watchDownloads;
            app.saveSettings();
            app.applyFolders();
            app.lib.startWatching();
            app.lib.scanAsync();
            app.invalidate();
        });
        for (size_t i = 0; i < s.folders.size(); ++i) {
            r = sec.row(L"Weiterer Ordner", s.folders[i], 52, 60);
            std::wstring f = s.folders[i];
            c.iconButton(RectF(r.X + r.Width - 40, r.Y + 9, 34, 34), Icon::Close, "rmf:" + narrow(f),
                         [&app, f] {
                             auto& v = app.settings.folders;
                             v.erase(std::remove(v.begin(), v.end(), f), v.end());
                             app.saveSettings();
                             app.applyFolders();
                             app.lib.startWatching();
                             app.lib.scanAsync();
                             app.invalidate();
                         },
                         L"Ordner entfernen");
        }
        r = sec.row(L"Neue Demos sofort auswerten", L"Sonst wertest du jedes Match selbst per Klick aus.", 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.autoAnalyze, "t-auto", [&app] {
            app.settings.autoAnalyze = !app.settings.autoAnalyze;
            app.saveSettings();
            app.invalidate();
        });
        c.line(x + 20, sec.y, x + cw - 20, sec.y, alpha(c.th.border, 150), 1);
        float bw = c.buttonWidth(L"Ordner hinzufügen", Icon::Plus);
        c.button(RectF(x + 20, sec.y + 12, bw, 34), L"Ordner hinzufügen", "addf", [&app] { app.addFolder(); }, Btn::Secondary, Icon::Plus);
        y += h + 16;
    }

    // ---- CS2
    {
        const std::wstring sub =
            L"Zum Ansehen und Aufnehmen von Momenten startet FragDesk CS2 im Workshop-Tools-Modus (-tools -netconport) und steuert die Demo "
            L"über dessen Konsole – unter Windows öffnet CS2 diese Konsole nur in dem Modus.";
        bool found = !app.steam.cs2Dir.empty();
        Tools t = found ? app.tools() : Tools::Unknown;
        std::wstring toolsDesc =
            t == Tools::Installed ? L"Kostenloser DLC von CS2, ist installiert."
            : t == Tools::Missing ? L"Kostenloser DLC von CS2, fehlt noch. Einmalig in Steam: Rechtsklick auf Counter-Strike 2 › Eigenschaften › DLC "
                                    L"› bei „Counter-Strike 2 Workshop Tools“ Installieren anhaken."
                                  : L"Kostenloser DLC von CS2 (Steam › Counter-Strike 2 › Eigenschaften › DLC). Ob er installiert ist, ließ sich "
                                    L"nicht feststellen – FragDesk versucht es trotzdem.";
        const float controlW = 200;
        float subH = c.textHeight(sub, F::Small, cw - 40) + 2;
        float descH = c.textHeight(toolsDesc, F::Small, cw - 40 - controlW - 16) + 2;
        float toolsH = std::max(64.0f, 32 + descH + 12);
        // Performance-Modus: was FragDesk tut, solange CS2 läuft (die Beschreibung sagt, was im gewählten Modus wartet)
        const std::vector<std::wstring> modes = {L"Performance", L"Manuell", L"Automatisch"};
        const std::wstring stillClosed = L" Ansehen und Aufnehmen in CS2 gehen weiter nur, wenn CS2 zu ist.";
        std::wstring modeDesc =
            s.cs2Mode == fd::Cs2Mode::Manual
                ? L"Solange CS2 läuft, startet FragDesk im Hintergrund nichts (Abgleich, 2D-Daten). Was du selbst anstößt, läuft aber sofort: "
                  L"„Jetzt abgleichen“, „Demo von Valve laden“ und alles, was du auf dem Handy öffnest." + stillClosed
            : s.cs2Mode == fd::Cs2Mode::Auto
                ? L"Nichts wartet auf CS2: Abgleich, Downloads und Handy-App laufen auch während des Spiels. Hoch- und Herunterladen kann "
                  L"deinen Ping kosten; Rechenarbeit bleibt auf niedriger Priorität." + stillClosed
                : L"Solange CS2 läuft, wartet FragDesk mit allem, was Internet oder viel Leistung braucht: Abgleich mit deinem Server, "
                  L"„Demo von Valve laden“, Videos und 2D-Ansicht fürs Handy. Danach geht es von selbst weiter.";
        float modesW = c.chipRowWidth(modes), modeCW = std::max(300.0f, modesW);
        float modeH = rowHeight(c, cw, modeDesc, modeCW, 64);
        float h = 16 + 26 + subH + 10 + 64 + toolsH + modeH + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Counter-Strike 2", sub);
        RectF r = sec.row(found ? L"Gefunden" : L"Nicht gefunden", found ? app.steam.cs2Dir : L"Wähle den Ordner „Counter-Strike Global Offensive“", 64,
                          controlW);
        c.button(RectF(r.X + r.Width - 180, r.Y + 14, 180, 36), L"Ordner wählen", "cs2dir", [&app] { app.changeCs2Dir(); }, Btn::Secondary,
                 Icon::Folder);
        r = sec.row(L"Workshop Tools", toolsDesc, toolsH, controlW);
        if (found) {
            std::wstring label = t == Tools::Installed ? L"Installiert" : t == Tools::Missing ? L"Fehlen" : L"Unbekannt";
            Color col = t == Tools::Installed ? c.th.success : t == Tools::Missing ? c.th.danger : c.th.text3;
            float pw = c.pillWidth(label, F::SmallSemi);
            c.pill(RectF(r.X + r.Width - pw, r.Y + 20, pw, 24), label, alpha(col, c.th.dark ? 40 : 34), col, F::SmallSemi);
        }
        r = sec.row(L"Performance-Modus", modeDesc, modeH, modeCW);
        int mi = s.cs2Mode == fd::Cs2Mode::Manual ? 1 : s.cs2Mode == fd::Cs2Mode::Auto ? 2 : 0;
        c.chipRow(r.X + r.Width - modesW, r.Y + (modeH - 30) / 2, modes, mi, "cs2mode", [&app](int i) {
            fd::Cs2Mode m = i == 1 ? fd::Cs2Mode::Manual : i == 2 ? fd::Cs2Mode::Auto : fd::Cs2Mode::Performance;
            if (app.settings.cs2Mode == m) return;
            app.settings.cs2Mode = m;
            app.saveSettings();
            app.applyCs2Mode();   // gilt sofort: eine Pause wegen CS2 prüft gleich neu
            app.invalidate();
        });
        y += h + 16;
    }

    // ---- Aufnahme
    {
        float h = 16 + 26 + 18 + 10 + 64 * 7 + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Highlight-Videos", L"So werden aufgenommene Momente gespeichert.");
        std::wstring dir = s.effectiveVideoDir();
        RectF r = sec.row(L"Ordner", dir, 64, 250);
        c.button(RectF(r.X + r.Width - 240, r.Y + 14, 116, 36), L"Ändern", "vdir", [&app] { app.changeVideoDir(); }, Btn::Secondary);
        c.button(RectF(r.X + r.Width - 116, r.Y + 14, 116, 36), L"Öffnen", "vopen", [dir] {
            createDirs(dir);
            openInShell(dir);
        }, Btn::Ghost, Icon::Folder);
        r = sec.row(L"Auflösung", L"Höhe des Videos; CS2 läuft dafür kurz in einem Fenster dieser Größe.", 64, 300);
        int hi = s.record.height == 720 ? 0 : s.record.height == 1440 ? 2 : 1;
        float cwid = c.chipRowWidth({L"720p", L"1080p", L"1440p"});
        c.chipRow(r.X + r.Width - cwid, r.Y + 17, {L"720p", L"1080p", L"1440p"}, hi, "res", [&app](int i) {
            app.settings.record.height = i == 0 ? 720 : i == 2 ? 1440 : 1080;
            app.saveSettings();
            app.invalidate();
        });
        r = sec.row(L"Bildrate", L"", 64, 300);
        cwid = c.chipRowWidth({L"30 fps", L"60 fps"});
        c.chipRow(r.X + r.Width - cwid, r.Y + 17, {L"30 fps", L"60 fps"}, s.record.fps == 30 ? 0 : 1, "fps", [&app](int i) {
            app.settings.record.fps = i == 0 ? 30 : 60;
            app.saveSettings();
            app.invalidate();
        });
        r = sec.row(L"Qualität", L"Höhere Qualität ergibt größere Dateien.", 64, 300);
        cwid = c.chipRowWidth({L"Sparsam", L"Gut", L"Sehr gut"});
        c.chipRow(r.X + r.Width - cwid, r.Y + 17, {L"Sparsam", L"Gut", L"Sehr gut"}, s.record.quality - 1, "qual", [&app](int i) {
            app.settings.record.quality = i + 1;
            app.saveSettings();
            app.invalidate();
        });
        r = sec.row(L"Nur Killfeed einblenden", L"Blendet das übrige HUD aus – schönere Clips.", 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.record.onlyKillfeed, "t-kf", [&app] {
            app.settings.record.onlyKillfeed = !app.settings.record.onlyKillfeed;
            app.saveSettings();
            app.invalidate();
        });
        r = sec.row(L"Spielton aufnehmen", L"Nur der Ton von CS2, nicht Discord oder Musik.", 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.record.sound, "t-snd", [&app] {
            app.settings.record.sound = !app.settings.record.sound;
            app.saveSettings();
            app.invalidate();
        });
        r = sec.row(L"Sprachchat der Demo", L"Stimmen deiner Mitspieler, falls die Demo sie enthält.", 64, 60);
        c.toggle(RectF(r.X + r.Width - 46, r.Y + 21, 44, 24), s.record.voices, "t-voice", [&app] {
            app.settings.record.voices = !app.settings.record.voices;
            app.saveSettings();
            app.invalidate();
        });
        y += h + 16;
    }

    // ---- Handy-App, eigener Server: Demos automatisch laden
    y += mobileCard(c, app, x, y, cw) + 16;
    if (s.mobile.server && !s.mobile.srv.token.empty()) y += autodlCard(c, app, x, y, cw) + 16;

    // ---- Darstellung
    {
        float h = 16 + 26 + 10 + 64 + 14;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Darstellung", L"");
        RectF r = sec.row(L"Farbschema", L"", 64, 340);
        int ti = s.theme == "dark" ? 1 : s.theme == "light" ? 2 : 0;
        float cwid = c.chipRowWidth({L"Wie Windows", L"Dunkel", L"Hell"});
        c.chipRow(r.X + r.Width - cwid, r.Y + 17, {L"Wie Windows", L"Dunkel", L"Hell"}, ti, "theme", [&app](int i) {
            app.settings.theme = i == 1 ? "dark" : i == 2 ? "light" : "";
            app.saveSettings();
            if (app.hwnd) PostMessageW(app.hwnd, WM_SETTINGCHANGE, 0, 0);
            app.invalidate();
        });
        y += h + 16;
    }

    // ---- Über
    {
        std::wstring about =
            L"FragDesk " + std::wstring(kVersion) + L" ist Teil von efficient lamp. Die Demos liest fdanalyze auf Grundlage von cs-demo-analyzer "
            L"(AkiVer, MIT-Lizenz) und demoinfocs-golang (markus-wa, MIT-Lizenz). Auswertungen liegen in " + dataDir() + L".\n\n"
            L"Anti-Cheat: FragDesk liest nur Dateien. Zum Ansehen und Aufnehmen startet es CS2 im offiziellen Workshop-Tools-Modus mit der "
            L"Konsolen-Schnittstelle (-tools -netconport) und nimmt das Fenster von außen auf, wie OBS. Es verändert keine Spieldateien und "
            L"lädt nichts ins Spiel. Für Matches CS2 danach normal neu starten, und Aufnahmen nie während eines Matches starten.";
        float th = c.textHeight(about, F::Small, cw - 40) + 4;
        float h = 16 + 26 + 10 + th + 66;
        cardBehind(c, x, y, cw, h);
        Section sec(c, x, y, cw, L"Über FragDesk", L"");
        c.text(about, F::Small, c.th.text2, RectF(x + 20, sec.y, cw - 40, th), Align::Left, false, true);
        float by = sec.y + th + 10;
        std::wstring data = dataDir();
        float b1 = c.buttonWidth(L"Datenordner öffnen", Icon::Folder);
        c.button(RectF(x + 20, by, b1, 34), L"Datenordner öffnen", "dataopen", [data] { openInShell(data); }, Btn::Secondary, Icon::Folder);
        float b2 = c.buttonWidth(L"Projekt auf GitHub", Icon::External);
        c.button(RectF(x + 28 + b1, by, b2, 34), L"Projekt auf GitHub", "gh", [] { openInShell(kRepoUrl); }, Btn::Ghost, Icon::External);
        y += h + 16;
    }
    return y + 20;
}

}  // namespace fdw
