// FragDesk – Seite „Einstellungen“
#include <algorithm>

#include <ctime>

#include "../core/format.h"
#include "app.h"
#include "common.h"
#include "mobile_host.h"

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
    RectF row(const std::wstring& label, const std::wstring& desc, float h = 56, float controlW = 300) {
        c.line(x + 20, y, x + w - 20, y, alpha(c.th.border, 150), 1);
        float lw = w - 40 - controlW - 16;
        c.text(label, F::BodySemi, c.th.text, RectF(x + 20, y + (desc.empty() ? 0 : 9), lw, desc.empty() ? h : 22));
        if (!desc.empty()) c.text(desc, F::Small, c.th.text3, RectF(x + 20, y + 30, lw, h - 32), Align::Left, false, true);
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

float mobileCard(Canvas& c, App& app, float x, float y, float cw) {
    MobileHost& mh = *app.mobile;
    MobileSettings& ms = app.settings.mobile;
    const std::wstring sub =
        L"Deine Stats und Clips auf dem Handy: FragDesk liefert dafür eine kleine Web-App aus, die du auf den Home-Bildschirm legst – "
        L"nur an Handys, die du hier koppelst. Solange es aus ist, nimmt FragDesk keine Verbindungen an.";
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

    const float panelH = 236;
    float subH = c.textHeight(sub, F::Small, cw - 40) + 2;
    float h = 16 + 26 + subH + 10 + 72;
    if (on) h += 64 + 72 + 64 + (code.empty() ? 0 : panelH) + (devs.empty() ? 0 : 40 + devs.size() * 56.0f);
    h += 14;
    cardBehind(c, x, y, cw, h);
    Section sec(c, x, y, cw, L"Handy-App", sub);

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
    sec.row(L"Adresse fürs Handy", where, 64, 10);

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
        float py = sec.y + 4, qs = 212;
        std::string url = addrs.empty() ? std::string() : narrow(addrs[0].url) + "?pair=" + code;
        if (!url.empty()) drawQr(c, RectF(x + 20, py, qs, qs), url);
        float tx = x + 20 + qs + 28, tw = cw - 40 - qs - 28;
        c.text(L"Code", F::Small, c.th.text2, RectF(tx, py, tw, 18));
        c.text(widen(code.substr(0, 3) + " " + code.substr(3)), F::NumHuge, c.th.accentText, RectF(tx, py + 16, tw, 54));
        int64_t left = std::max<int64_t>(0, mh.pairingUntil() - int64_t(time(nullptr)));
        wchar_t rest[64];
        swprintf(rest, 64, L"Noch %lld:%02lld Minuten gültig", static_cast<long long>(left / 60), static_cast<long long>(left % 60));
        c.text(rest, F::Small, c.th.text3, RectF(tx, py + 72, tw, 18));
        const std::wstring steps =
            L"1. QR-Code mit der iPhone-Kamera scannen – Safari öffnet FragDesk und verbindet sich.\n"
            L"2. In Safari auf „Teilen“ › „Zum Home-Bildschirm“ tippen.\n"
            L"3. FragDesk über das neue Symbol öffnen. Fragt es nach dem Code, gib ihn dort ein.\n"
            L"Ohne Kamera: die Adresse oben in Safari öffnen und den Code eingeben.";
        c.text(steps, F::Small, c.th.text2, RectF(tx, py + 100, tw, panelH - 104), Align::Left, false, true);
        sec.y += panelH;
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
    y += pageTitle(c, x, y, cw, L"Einstellungen", L"Alles wird nur auf diesem PC gespeichert") + 14;
    Settings& s = app.settings;

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
            L"Nur zum Ansehen und Aufnehmen von Momenten. FragDesk startet CS2 dafür im Workshop-Tools-Modus (-tools -netconport) und steuert "
            L"die Demo über dessen Konsole – unter Windows öffnet CS2 diese Konsole nur in dem Modus.";
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
        float h = 16 + 26 + subH + 10 + 64 + toolsH + 14;
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

    // ---- Handy-App
    y += mobileCard(c, app, x, y, cw) + 16;

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
