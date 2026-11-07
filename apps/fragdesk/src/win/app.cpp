// FragDesk – Zustand und Befehle der App (siehe app.h)
#include "app.h"

#include <algorithm>
#include <cmath>

#include "../core/format.h"
#include "../core/json.h"
#include "common.h"
#include "mobile_host.h"
#include "recorder.h"
#include "server_sync.h"

namespace fdw {

App::App() : mobile(std::make_unique<MobileHost>(*this)), sync(std::make_unique<ServerSync>(*this)) {}
App::~App() = default;

void App::invalidate() {
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void App::notify(const std::wstring& msg, bool error) {
    toast = msg;
    toastError = error;
    toastUntil = GetTickCount64() + (error ? 9000 : 5000);
    if (hwnd) SetTimer(hwnd, 7, 500, nullptr);
    invalidate();
}

void App::detectMe() {
    std::string id = settings.steamId;
    if (id.empty()) id = steam.recentSteamId();
    if (id.empty()) {
        // Wer in den meisten Matches mitspielt, ist vermutlich man selbst
        std::map<std::string, int> count;
        for (const auto& m : lib.matches)
            for (const auto& p : m->players) ++count[p.steamId];
        int best = 0;
        for (const auto& kv : count)
            if (kv.second > best) {
                best = kv.second;
                id = kv.first;
            }
    }
    me = id;
    meName.clear();
    for (const auto& a : steam.accounts)
        if (a.steamId == me) meName = widen(a.personaName);
    if (meName.empty())
        for (const auto& m : lib.matches)
            if (const fd::PlayerInfo* p = m->player(me)) {
                meName = widen(p->name);
                break;
            }
    invalidateCareer();
}

void App::applyFolders() {
    std::vector<std::pair<std::wstring, std::wstring>> folders;
    if (settings.watchReplays && !steam.replaysDir().empty()) folders.push_back({steam.replaysDir(), L"CS2"});
    if (settings.watchDownloads) folders.push_back({downloadsDir(), L"Downloads"});
    for (const auto& f : settings.folders) folders.push_back({f, L"Ordner"});
    lib.setFolders(folders);
    lib.setIgnored(settings.ignored);
}

void App::saveSettings() {
    if (!renderMode) settings.save();
}

void App::go(Page p) {
    if (p != Page::Match && p != Page::Replay) backPage = p;
    if (page == Page::Replay && p != Page::Replay) {   // Daten bleiben für die Rückkehr, die großen Bilder nicht
        replay.playing = false;
        replay.releaseBitmaps();
    }
    if (page == Page::Match && p != Page::Match) matchHeat.release();
    if (page == Page::Stats && p != Page::Stats) statsHeat.release();
    page = p;
    invalidate();
}

void App::openReplay(int round, int tick) {
    if (!detail) return;
    matchHeat.release();   // die großen Bilder der Heatmaps braucht die 2D-Ansicht nicht
    statsHeat.release();
    page = Page::Replay;
    replay.pendingTick = tick;   // −1: keine Stelle (auch kein Rest von einem früheren Sprung)
    replay.open(hwnd, *detail, round);
    invalidate();
}

void App::openReplayAt(const std::string& checksum, int round, int tick) {
    if (matchId != checksum || !detail) {
        int tab = matchTab;
        openMatch(checksum, tab);
        if (!detail) return;
    }
    openReplay(round, std::max(0, tick - int(2 * (detail->tickrate > 0 ? detail->tickrate : 64))));   // kurz davor
}

void App::refreshDetail() {
    if (matchId.empty() || !detail) return;
    auto d = std::make_unique<fd::Match>();
    std::wstring err;
    if (lib.loadDetail(matchId, *d, err)) detail = std::move(d);
}

int App::reanalyze(const std::vector<const fd::Match*>& matches) {
    int n = 0;
    for (const fd::Match* m : matches) {
        if (!m || !outdated(*m) || !demoAvailable(*m)) continue;
        lib.analyze(widen(m->demoPath));
        ++n;
    }
    if (n) notify(n == 1 ? L"Ein Match wird neu ausgewertet" : std::to_wstring(n) + L" Matches werden neu ausgewertet");
    return n;
}

void App::openMatch(const std::string& checksum, int tab) {
    if (page != Page::Match) backPage = page;
    page = Page::Match;
    matchTab = tab;
    focusPlayer.clear();
    scroll[int(Page::Match)] = 0;
    if (matchId != checksum || !detail) {
        matchId = checksum;
        detail = std::make_unique<fd::Match>();
        detailError.clear();
        if (!lib.loadDetail(checksum, *detail, detailError)) detail.reset();
    }
    invalidate();
}

std::vector<std::wstring> App::modeLabels() { return {L"Alle", L"Premier", L"FACEIT", L"Wettkampf", L"Wingman", L"Sonstige"}; }
std::vector<std::wstring> App::periodLabels() { return {L"Letzte 10", L"Letzte 30", L"Alle Matches"}; }

fd::Filter App::filter() const {
    fd::Filter f;
    static const char* modes[] = {"", "premier", "faceit", "competitive", "wingman", "other"};
    if (modeFilter > 0 && modeFilter < 6) f.mode = modes[modeFilter];
    f.map = mapFilter;
    f.lastN = periodFilter == 0 ? 10 : periodFilter == 1 ? 30 : 0;
    return f;
}

std::vector<const fd::Match*> App::matchPtrs() const {
    std::vector<const fd::Match*> v;
    for (const auto& m : lib.matches) v.push_back(m.get());
    return v;
}

const fd::Career& App::career() {
    fd::Filter f = filter();
    std::string key = me + "|" + f.mode + "|" + f.map + "|" + std::to_string(f.lastN) + "|" + std::to_string(lib.matches.size());
    if (careerDirty_ || key != careerKey_) {
        career_ = fd::buildCareer(matchPtrs(), me, f);
        careerKey_ = key;
        careerDirty_ = false;
    }
    return career_;
}

SortState& App::sort(const std::string& key, int defCol, bool defDesc) {
    auto it = sorts.find(key);
    if (it == sorts.end()) it = sorts.emplace(key, SortState{defCol, defDesc}).first;
    return it->second;
}

// ---- Aufnahmen ------------------------------------------------------------------------------------------------
static std::wstring clipsPath() { return joinPath(dataDir(), L"clips.json"); }

void App::loadClips() {
    clips.clear();
    std::string text;
    json::Value v;
    if (!readFile(clipsPath(), text, 8u << 20) || !json::parse(text, v) || !v.isArray()) return;
    for (const auto& c : v.arr) {
        Clip cl;
        cl.path = widen(c.getString("path"));
        cl.title = widen(c.getString("title"));
        cl.match = c.getString("match");
        cl.highlight = c.getString("highlight");
        cl.map = c.getString("map");
        cl.created = int64_t(c.getNumber("created"));
        cl.seconds = c.getNumber("seconds");
        if (fileExists(cl.path)) {
            cl.size = fileSize(cl.path);
            clips.push_back(cl);
        }
    }
    std::sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.created > b.created; });
}

void App::saveClips() {
    if (renderMode) return;
    std::string o = "[\n";
    for (size_t i = 0; i < clips.size(); ++i) {
        const Clip& c = clips[i];
        char num[96];
        snprintf(num, sizeof num, ", \"created\": %lld, \"seconds\": %.1f}", (long long)c.created, c.seconds);
        o += "  {\"path\": " + jsonQuote(narrow(c.path)) + ", \"title\": " + jsonQuote(narrow(c.title)) + ", \"match\": " + jsonQuote(c.match) +
             ", \"highlight\": " + jsonQuote(c.highlight) + ", \"map\": " + jsonQuote(c.map) + num + (i + 1 < clips.size() ? ",\n" : "\n");
    }
    o += "]\n";
    createDirs(dataDir());
    writeFileAtomic(clipsPath(), o);
}

const Clip* App::clipFor(const std::string& match, const std::string& highlight) const {
    for (const auto& c : clips)
        if (c.match == match && c.highlight == highlight) return &c;
    return nullptr;
}

// Gültiger Datei- oder Ordnername für Windows (Workshop-Maps können "/" im Namen haben)
static std::wstring safeName(const std::wstring& name, size_t maxLen) {
    std::wstring clean;
    for (wchar_t c : name) clean += (wcschr(L"<>:\"/\\|?*", c) || c < 32) ? L'_' : c == L'·' ? L'-' : c;
    if (clean.size() > maxLen) clean.resize(maxLen);
    while (!clean.empty() && (clean.back() == L'.' || clean.back() == L' ')) clean.pop_back();   // Windows mag kein Punkt/Leerzeichen am Ende
    std::wstring base = lowerCase(clean.substr(0, clean.find(L'.')));
    static const wchar_t* const reserved[] = {L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7",
                                              L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9"};
    for (const wchar_t* r : reserved)
        if (base == r) clean = L"_" + clean;
    return clean.empty() ? std::wstring(L"Video") : clean;
}

// Pfad, den es noch nicht gibt (auch nicht unter den schon geplanten): "Name.mp4", "Name (2).mp4", …
static std::wstring uniquePath(const std::wstring& path, const std::vector<std::wstring>& planned) {
    std::wstring stem = path.substr(0, path.size() - 4), ext = path.substr(path.size() - 4);
    for (int n = 1; n < 1000; ++n) {
        std::wstring p = n == 1 ? path : stem + L" (" + std::to_wstring(n) + L")" + ext;
        bool used = fileExists(p) || std::any_of(planned.begin(), planned.end(), [&](const std::wstring& q) { return samePath(p, q); });
        if (!used) return p;
    }
    return path;
}

static bool makeJob(App& app, const MomentRef& ref, RecordJob& job, std::wstring& err) {
    const fd::Match* m = app.lib.find(ref.match);
    if (!m) {
        err = L"Match nicht gefunden";
        return false;
    }
    const fd::HighlightInfo* h = nullptr;
    for (const auto& x : m->highlights)
        if (x.id == ref.highlight) h = &x;
    if (!h) {
        err = L"Moment nicht gefunden";
        return false;
    }
    const fd::PlayerInfo* p = m->player(h->player);
    std::wstring demo = widen(m->demoPath);
    if (!fileExists(demo)) {
        err = L"Die Demo-Datei ist nicht mehr da: " + demo;
        return false;
    }
    job.demo = demo;
    job.match = m->checksum;
    job.highlight = h->id;
    job.map = m->map;
    job.slot = p ? p->slot : 0;
    job.steamId = h->player;
    job.tickrate = m->tickrate;
    job.segments = h->segments;
    std::wstring who = p ? widen(p->name) : L"";
    job.title = L"R" + std::to_wstring(h->round) + L" " + widen(fd::highlightTitle(*h)) + (who.empty() || h->player == app.me ? L"" : L" (" + who + L")");
    // Ziel: Videos\FragDesk\2026-11-06 Mirage 13-11\R14 3 Kills.mp4
    int64_t t = matchEpoch(*m);
    SYSTEMTIME st = localTime(t);
    wchar_t folder[160];
    int sa = m->scoreA, sb = m->scoreB;
    if (const fd::PlayerInfo* mine = m->player(app.me)) {   // eigenes Ergebnis zuerst
        sa = m->scoreOf(mine->team);
        sb = m->scoreOf(mine->team == "A" ? "B" : "A");
    }
    swprintf(folder, 160, L"%04u-%02u-%02u %ls %d-%d", st.wYear, st.wMonth, st.wDay, widen(fd::mapName(m->map)).c_str(), sa, sb);
    std::wstring name = L"R" + std::to_wstring(h->round) + L" " + widen(fd::highlightTitle(*h));
    if (!who.empty() && h->player != app.me) name += L" - " + who;
    job.out = joinPath(joinPath(app.settings.effectiveVideoDir(), safeName(folder, 120)), safeName(name, 90) + L".mp4");
    return true;
}

void App::watch(const MomentRef& ref) {
    RecordJob job;
    std::wstring err;
    if (!makeJob(*this, ref, job, err)) {
        notify(err, true);
        return;
    }
    if (!recorder) recorder = std::make_unique<Recorder>(hwnd);
    recordError.clear();
    if (!recorder->watch(steam, job, err)) notify(err, true);
    else {
        sync->setRecording(true);   // kein Hochladen, solange CS2 für FragDesk läuft
        notify(L"CS2 startet und springt zu „" + job.title + L"“");
    }
}

void App::record(const std::vector<MomentRef>& moments) {
    if (moments.empty()) return;
    std::vector<RecordJob> jobs;
    std::wstring err;
    std::vector<std::wstring> planned;
    for (const auto& m : moments) {
        RecordJob j;
        if (!makeJob(*this, m, j, err)) continue;
        j.out = uniquePath(j.out, planned);   // nie ein vorhandenes Video überschreiben
        planned.push_back(j.out);
        jobs.push_back(j);
    }
    if (jobs.empty()) {
        notify(err.empty() ? L"Nichts zum Aufnehmen" : err, true);
        return;
    }
    if (!recorder) recorder = std::make_unique<Recorder>(hwnd);
    recordError.clear();
    if (!recorder->record(steam, settings.record, jobs, err)) notify(err, true);
    else {
        sync->setRecording(true);   // Pause beim Hochladen, bis die Aufnahme fertig ist
        selected.clear();
        go(Page::Highlights);
    }
}

bool App::recording() const { return recorder && recorder->busy(); }

bool App::demoAvailable(const fd::Match& m) {
    ULONGLONG now = GetTickCount64();
    auto& e = demoCache_[m.checksum];
    if (e.second == 0 || now - e.second > 3000) {
        e.first = fileExists(widen(m.demoPath));
        e.second = now;
    }
    return e.first;
}

Tools App::tools() {
    // appmanifest_730.acf ist klein; trotzdem nur alle paar Sekunden lesen (wird beim Zeichnen abgefragt)
    ULONGLONG now = GetTickCount64();
    if (toolsAt_ == 0 || now - toolsAt_ > 4000) {
        Tools t = workshopTools(steam);
        if (t != tools_ || toolsAt_ == 0) logLine(L"CS2 Workshop Tools: %ls", t == Tools::Installed ? L"installiert" : t == Tools::Missing ? L"fehlen" : L"unbekannt");
        tools_ = t;
        toolsAt_ = now;
    }
    return tools_;
}

std::wstring App::recordBlocker() {
    if (recording()) return L"Es läuft bereits eine Aufnahme";
    if (steam.cs2Exe().empty()) return L"CS2 wurde nicht gefunden – Ordner in den Einstellungen wählen";
    if (tools() == Tools::Missing)
        return L"Erst die kostenlosen CS2 Workshop Tools installieren: in Steam Rechtsklick auf Counter-Strike 2 › Eigenschaften › DLC";
    return L"";
}

std::wstring App::momentBlocker(const fd::Match& m) {
    std::wstring b = recordBlocker();
    if (!b.empty()) return b;
    if (!demoAvailable(m)) return L"Die Demo-Datei ist nicht mehr da";
    return L"";
}
std::wstring App::recordStatus() const { return recorder ? recorder->status() : std::wstring(); }
void App::cancelRecording() {
    if (recorder) recorder->cancel();
}

// ---- Befehle --------------------------------------------------------------------------------------------------
void App::addDemos() {
    std::vector<std::wstring> files;
    if (pickFiles(hwnd, files)) {
        lib.addFiles(files);
        notify(files.size() == 1 ? L"Demo wird ausgewertet" : std::to_wstring(files.size()) + L" Demos werden ausgewertet");
    }
}

void App::rescan() {
    lib.scanAsync();
    notify(L"Ordner werden durchsucht …");
}

void App::changeVideoDir() {
    std::wstring dir = settings.effectiveVideoDir();
    if (pickFolder(hwnd, L"Ordner für Highlight-Videos", dir)) {
        settings.videoDir = dir;
        saveSettings();
        invalidate();
    }
}

void App::changeCs2Dir() {
    std::wstring dir = steam.cs2Dir;
    if (pickFolder(hwnd, L"Ordner „Counter-Strike Global Offensive“ wählen", dir)) {
        SteamInfo s = detectSteam(dir);
        if (s.cs2Dir.empty()) {
            notify(L"In diesem Ordner liegt kein CS2 (game\\bin\\win64\\cs2.exe fehlt)", true);
            return;
        }
        settings.cs2Dir = s.cs2Dir;
        steam = s;
        saveSettings();
        applyFolders();
        lib.startWatching();
        lib.scanAsync();
        invalidate();
    }
}

void App::addFolder() {
    std::wstring dir;
    if (pickFolder(hwnd, L"Weiteren Demo-Ordner wählen", dir)) {
        for (const auto& f : settings.folders)
            if (samePath(f, dir)) return;
        settings.folders.push_back(dir);
        saveSettings();
        applyFolders();
        lib.startWatching();
        lib.scanAsync();
        invalidate();
    }
}

void App::chooseAccount(int x, int y) {
    HMENU menu = CreatePopupMenu();
    std::vector<std::string> ids;
    AppendMenuW(menu, MF_STRING | (settings.steamId.empty() ? MF_CHECKED : 0), 1, L"Automatisch (zuletzt in Steam angemeldet)");
    ids.push_back("");
    std::set<std::string> seen;
    for (const auto& a : steam.accounts) {
        std::wstring label = widen(a.personaName) + L"  (" + widen(a.accountName) + L")";
        AppendMenuW(menu, MF_STRING | (settings.steamId == a.steamId ? MF_CHECKED : 0), UINT(ids.size() + 1), label.c_str());
        ids.push_back(a.steamId);
        seen.insert(a.steamId);
    }
    // Spieler aus den Matches (häufigste zuerst)
    std::map<std::string, std::pair<int, std::string>> count;
    for (const auto& m : lib.matches)
        for (const auto& p : m->players) {
            auto& e = count[p.steamId];
            ++e.first;
            e.second = p.name;
        }
    std::vector<std::pair<int, std::string>> order;
    for (const auto& kv : count)
        if (!seen.count(kv.first) && kv.second.first >= 2) order.push_back({kv.second.first, kv.first});
    std::sort(order.rbegin(), order.rend());
    if (!order.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < order.size() && i < 12; ++i) {
        const auto& id = order[i].second;
        std::wstring label = widen(count[id].second) + L"  (" + std::to_wstring(order[i].first) + L" Matches)";
        AppendMenuW(menu, MF_STRING | (settings.steamId == id ? MF_CHECKED : 0), UINT(ids.size() + 1), label.c_str());
        ids.push_back(id);
    }
    POINT pt = {x, y};
    ClientToScreen(hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd > 0 && size_t(cmd) <= ids.size()) {
        settings.steamId = ids[cmd - 1];
        saveSettings();
        detectMe();
        sync->request();   // neuer Spieler: dem Server melden
        invalidate();
    }
}

void App::deleteMatch(const std::string& checksum) {
    const fd::Match* m = lib.find(checksum);
    if (!m) return;
    std::wstring q = L"Auswertung von " + matchTitle(*m) + L" löschen?\n\nDie Demo-Datei bleibt erhalten; FragDesk bietet sie danach wieder zum Auswerten an.";
    if (MessageBoxW(hwnd, q.c_str(), L"FragDesk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    lib.remove(checksum);
    if (matchId == checksum) {
        detail.reset();
        matchId.clear();
        if (page == Page::Match) page = backPage;
    }
    invalidateCareer();
    lib.scanAsync();
    sync->request();   // auch auf dem eigenen Server löschen
    invalidate();
}

// ---- gemeinsame Teile -----------------------------------------------------------------------------------------
int64_t matchEpoch(const fd::Match& m) {
    int64_t t = 0;
    if (!fd::parseIsoUtc(m.date, t)) fd::parseIsoUtc(m.demoModified, t);
    return t;
}

std::wstring matchTitle(const fd::Match& m, const std::string& me) {
    int a = m.scoreA, b = m.scoreB;
    if (const fd::PlayerInfo* p = me.empty() ? nullptr : m.player(me)) {   // eigenes Team zuerst
        a = m.scoreOf(p->team);
        b = m.scoreOf(p->team == "A" ? "B" : "A");
    }
    return widen(fd::mapName(m.map)) + L" · " + std::to_wstring(a) + L":" + std::to_wstring(b);
}

float pageTitle(ui::Canvas& c, float x, float y, float w, const std::wstring& title, const std::wstring& sub) {
    c.text(title, ui::F::Display, c.th.text, Gdiplus::RectF(x, y, w, 36));
    if (!sub.empty()) c.text(sub, ui::F::Body, c.th.text2, Gdiplus::RectF(x, y + 38, w, 20));
    return sub.empty() ? 44 : 64;
}

float reanalyzeNote(ui::Canvas& c, App& app, float x, float y, float w, const std::vector<const fd::Match*>& matches, const wchar_t* missing) {
    using namespace ui;
    std::vector<const fd::Match*> old;
    int withDemo = 0;
    for (const fd::Match* m : matches)
        if (m && App::outdated(*m)) {
            old.push_back(m);
            if (app.demoAvailable(*m)) ++withDemo;
        }
    if (old.empty()) return 0;
    const float h = 64;
    Gdiplus::RectF r(x, y, w, h);
    c.round(r, 12, alpha(c.th.accent, c.th.dark ? 26 : 40));
    c.roundStroke(r, 12, alpha(c.th.accent, 90), 1);
    c.icon(Icon::Info, Gdiplus::RectF(x + 16, y + 21, 22, 22), c.th.accentText, 1.7f);
    std::wstring head = old.size() == 1 ? L"Ein Match wurde mit einer älteren FragDesk-Version ausgewertet"
                                        : std::to_wstring(old.size()) + L" Matches wurden mit einer älteren FragDesk-Version ausgewertet";
    std::wstring sub = std::wstring(L"Ihnen fehlen ") + missing + L".";
    if (withDemo < int(old.size())) {
        int gone = int(old.size()) - withDemo;
        sub += L" " + (gone == 1 ? std::wstring(L"Bei einem") : L"Bei " + std::to_wstring(gone)) + L" ist die Demo nicht mehr da.";
    }
    float bw = 0;
    bool busy = app.lib.busy();
    if (busy) {
        std::wstring t = L"Wird ausgewertet … noch " + std::to_wstring(app.lib.queued());
        bw = c.measure(t, F::SmallSemi) + 8;
        c.text(t, F::SmallSemi, c.th.text2, Gdiplus::RectF(x + w - bw - 16, y, bw, h), Align::Right);
    } else if (withDemo > 0) {
        std::wstring label = withDemo == int(old.size()) ? L"Neu auswerten" : L"Neu auswerten (" + std::to_wstring(withDemo) + L")";
        bw = c.buttonWidth(label, Icon::Refresh);
        c.button(Gdiplus::RectF(x + w - bw - 14, y + 14, bw, 36), label, "reanalyze", [&app, old] { app.reanalyze(old); }, Btn::Primary,
                 Icon::Refresh);
    }
    c.text(head, F::BodySemi, c.th.text, Gdiplus::RectF(x + 50, y + 11, w - 70 - bw, 22));
    c.text(sub, F::Small, c.th.text2, Gdiplus::RectF(x + 50, y + 33, w - 70 - bw, 20));
    return h + 16;
}

std::vector<ui::ChartPoint> premierPoints(App& app, const fd::Career& c) {
    std::vector<ui::ChartPoint> pts;
    for (const auto& p : c.points) {
        if (p.premierNew <= 0 && p.premierOld <= 0) continue;
        int v = p.premierNew > 0 ? p.premierNew : p.premierOld;
        ui::ChartPoint cp;
        cp.y = v;
        int64_t t = matchEpoch(*p.match);
        cp.label = dateText(t).substr(0, 6);
        std::wstring change;
        if (p.premierOld > 0 && p.premierNew > 0) change = L" (" + widen(fd::fmtSigned(p.premierNew - p.premierOld, 0)) + L")";
        cp.tip = widen(fd::fmtRating(v)) + change + L"\n" + matchTitle(*p.match, app.me) + L" · " + widen(fd::resultName(p.me->result)) + L"\n" +
                 dateTimeText(t);
        std::string id = p.match->checksum;
        cp.click = [&app, id] { app.openMatch(id); };
        pts.push_back(cp);
    }
    return pts;
}

}  // namespace fdw
