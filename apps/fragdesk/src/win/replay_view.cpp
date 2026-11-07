#include "replay_view.h"

#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "common.h"
#include "library.h"

using namespace Gdiplus;

namespace fdw {

namespace {

// fdanalyze endet mit FragDesk
HANDLE analyzerJob() {
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li);
        }
        return j;
    }();
    return job;
}

// fdanalyze -replay; liefert das Fehler-Kennwort (leer = fertig)
std::string runReplayTool(const std::wstring& demo, const std::wstring& out) {
    std::wstring exe = joinPath(selfDir(), kAnalyzerExe);
    if (!fileExists(exe)) return "MISSING";
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return "START";
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = L"\"" + exe + L"\" -demo \"" + demo + L"\" -replay \"" + out + L"\" -temp \"" + tempDir() + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        logLine(L"fdanalyze -replay startet nicht (%lu)", GetLastError());
        CloseHandle(rd);
        return "START";
    }
    if (HANDLE job = analyzerJob()) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    std::string pending, code, done;
    char chunk[512];
    DWORD got = 0;
    while (ReadFile(rd, chunk, sizeof chunk, &got, nullptr) && got > 0) {
        pending.append(chunk, got);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("ERROR ", 0) == 0) {
                std::string rest = line.substr(6);
                code = rest.substr(0, rest.find(' '));
                logLine(L"2D-Daten %ls: %ls", demo.c_str(), widen(rest).c_str());
            } else if (line.rfind("DONE ", 0) == 0) {
                done = line;
            }
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode != 0 || done.empty()) return code.empty() ? "INTERNAL" : code;
    return "";
}

std::wstring replayPath(const std::string& checksum) { return joinPath(matchesDir(), widen(checksum) + L".replay"); }

// vorhandene Datei laden, sonst aus der Demo erzeugen
ReplayEvent* loadReplay(const std::string& checksum, const std::wstring& demo) {
    auto* e = new ReplayEvent();
    e->match = checksum;
    std::wstring path = replayPath(checksum);
    auto tryLoad = [&]() -> bool {
        std::string bytes, err;
        if (!readFile(path, bytes, 256u << 20)) return false;
        auto r = std::make_shared<fd::Replay>();
        if (!fd::parseReplay(bytes, *r, err)) {
            logLine(L"2D-Daten %ls unbrauchbar (%ls), werden neu erzeugt", path.c_str(), widen(err).c_str());
            DeleteFileW(path.c_str());
            return false;
        }
        e->data = std::move(r);
        return true;
    };
    if (!tryLoad()) {
        if (demo.empty() || !fileExists(demo)) {
            e->error = L"Die Demo-Datei ist nicht mehr da – ohne sie lassen sich keine 2D-Daten erstellen.";
        } else {
            createDirs(matchesDir());
            createDirs(tempDir());
            ULONGLONG t0 = GetTickCount64();
            std::string code = runReplayTool(demo, path);
            if (!code.empty()) e->error = analysisErrorText(code);
            else if (!tryLoad()) e->error = L"Die 2D-Daten sind unvollständig.";
            else logLine(L"2D-Daten erstellt in %.1f s: %ls", (GetTickCount64() - t0) / 1000.0, path.c_str());
        }
    }
    return e;
}

void loadWorker(HWND hwnd, std::string checksum, std::wstring demo) {
    ReplayEvent* e = loadReplay(checksum, demo);
    if (!PostMessageW(hwnd, WM_APP_REPLAY, 0, reinterpret_cast<LPARAM>(e))) delete e;
}

// PNG aus den Ressourcen
std::unique_ptr<Bitmap> loadPng(int id) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!res) return nullptr;
    HGLOBAL h = LoadResource(nullptr, res);
    DWORD size = SizeofResource(nullptr, res);
    const void* p = h ? LockResource(h) : nullptr;
    if (!p || !size) return nullptr;
    IStream* s = SHCreateMemStream(static_cast<const BYTE*>(p), size);
    if (!s) return nullptr;
    std::unique_ptr<Bitmap> bmp(Bitmap::FromStream(s));
    s->Release();
    if (!bmp || bmp->GetLastStatus() != Ok) return nullptr;
    return bmp;
}

double perfSeconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / double(freq.QuadPart);
}

}  // namespace

void ReplayView::open(HWND hwnd, const fd::Match& m, int n) {
    if (match != m.checksum || state == State::Failed || state == State::Idle) {
        if (match != m.checksum) {
            data.reset();
            roster.clear();
            follow.clear();
            radar = nullptr;
            radarIndex = -1;
            releaseBitmaps();
        }
        match = m.checksum;
        playing = false;
        state = State::Working;
        error.clear();
        std::thread(loadWorker, hwnd, m.checksum, widen(m.demoPath)).detach();
    }
    round = n > 0 ? n : (m.rounds.empty() ? 1 : m.rounds.front().n);
    if (data) setRound(m, round);
}

void ReplayView::openNow(const fd::Match& m, int n) {
    match = m.checksum;
    round = n > 0 ? n : (m.rounds.empty() ? 1 : m.rounds.front().n);
    state = State::Working;
    std::unique_ptr<ReplayEvent> e(loadReplay(m.checksum, widen(m.demoPath)));
    apply(e.get(), &m);
}

bool ReplayView::apply(ReplayEvent* e, const fd::Match* m) {
    if (!e || e->match != match) return false;
    if (e->data) {
        data = e->data;
        state = State::Ready;
        prepareMap();
        if (m) setRound(*m, round);
    } else {
        state = State::Failed;
        error = e->error;
    }
    return true;
}

void ReplayView::prepareMap() {
    radar = data ? fd::radarMap(data->map, &radarIndex) : nullptr;
    fallback = fd::RadarMap{"", -2048, 2048, 4096, 0, false};   // ohne Positionen: irgendein gültiger Ausschnitt
    if (radar || !data || data->samples.empty()) return;
    // ohne Radarbild: Ausschnitt um alle Positionen
    int16_t x0 = INT16_MAX, x1 = INT16_MIN, y0 = INT16_MAX, y1 = INT16_MIN;
    for (const auto& s : data->samples) {
        x0 = std::min(x0, s.pos.x);
        x1 = std::max(x1, s.pos.x);
        y0 = std::min(y0, s.pos.y);
        y1 = std::max(y1, s.pos.y);
    }
    float span = float(std::max(x1 - x0, y1 - y0)) + 600.0f;
    fallback = fd::RadarMap{"", (x0 + x1) / 2.0f - span / 2, (y0 + y1) / 2.0f + span / 2, span, 0, false};
}

void ReplayView::setRound(const fd::Match& m, int n) {
    const fd::RoundInfo* r = nullptr;
    for (const auto& ri : m.rounds)
        if (ri.n == n) r = &ri;
    if (!r && !m.rounds.empty()) r = &m.rounds.front();
    if (!r) return;
    round = r->n;
    double tr = data ? data->tickrate : (m.tickrate > 0 ? m.tickrate : 64);
    startTick = r->start;
    freezeTick = std::max(r->freezeEnd, r->start);
    // Ende: kurz vor dem Neustart – ab dem offiziellen Rundenende stehen alle schon am Spawn der nächsten Runde
    endTick = r->endOfficial > r->end ? r->endOfficial - 1 : r->end + int(4 * tr);
    const fd::RoundInfo* next = r + 1 < m.rounds.data() + m.rounds.size() ? r + 1 : nullptr;
    if (next && next->start > r->end) endTick = std::min(endTick, next->start - 1);
    if (endTick <= startTick) endTick = startTick + int(tr);
    tick = std::max(double(startTick), freezeTick - 3 * tr);
    last_ = 0;
    // Spieler dieser Runde (aus den Positionen), CT zuerst
    roster.clear();
    if (!data) return;
    for (size_t i = fd::frameAt(*data, startTick); i < data->frames.size() && data->frames[i].tick <= endTick; ++i) {
        const auto& f = data->frames[i];
        for (int k = 0; k < f.count; ++k) {
            const auto& s = data->samples[f.first + k];
            if (std::none_of(roster.begin(), roster.end(), [&](const RosterEntry& e) { return e.player == s.player; }))
                roster.push_back({s.player, (s.flags & fd::RpCT) != 0});
        }
    }
    const auto& players = data->players;
    std::sort(roster.begin(), roster.end(), [&](const RosterEntry& a, const RosterEntry& b) {
        if (a.ct != b.ct) return a.ct;
        const std::string& na = a.player < players.size() ? players[a.player].name : std::string();
        const std::string& nb = b.player < players.size() ? players[b.player].name : std::string();
        return na < nb;
    });
}

bool ReplayView::stepRound(const fd::Match& m, int dir) {
    for (size_t i = 0; i < m.rounds.size(); ++i) {
        if (m.rounds[i].n != round) continue;
        size_t j = i + dir;   // bei i = 0 und dir = -1 sehr groß
        if (j >= m.rounds.size()) return false;
        setRound(m, m.rounds[j].n);
        return true;
    }
    return false;
}

void ReplayView::seek(double t) {
    tick = std::clamp(t, double(startTick), double(endTick));
    last_ = 0;
}

void ReplayView::togglePlay() {
    if (!data) return;
    if (!playing && tick >= endTick - 1) tick = std::max(double(startTick), freezeTick - 3.0 * data->tickrate);
    playing = !playing;
    last_ = 0;
}

bool ReplayView::advance() {
    if (!playing || !data) return false;
    double now = perfSeconds();
    if (last_ == 0) {   // erster Schritt nach Start oder Sprung
        last_ = now;
        return true;
    }
    double dt = std::min(0.25, now - last_);
    last_ = now;
    tick += dt * data->tickrate * speed;
    if (tick >= endTick) {
        tick = endTick;
        playing = false;
        last_ = 0;
        return false;
    }
    return true;
}

Bitmap* ReplayView::radarBitmap(bool lower, int px) {
    if (!radar || radarIndex < 0 || px <= 0) return nullptr;
    int l = lower ? 1 : 0;
    if (lower && !radar->lower) return nullptr;
    if (srcIndex_ != radarIndex) {
        releaseBitmaps();
        srcIndex_ = radarIndex;
    }
    if (scaled_[l] && scaledPx_[l] == px) return scaled_[l].get();
    // Quelle nur zum Skalieren laden (spart den Speicher für das große Bild)
    std::unique_ptr<Bitmap> src = loadPng(fd::kRadarResBase + 2 * radarIndex + l);
    if (!src) {
        logLine(L"Radarbild %d fehlt", fd::kRadarResBase + 2 * radarIndex + l);
        return nullptr;
    }
    auto out = std::make_unique<Bitmap>(px, px, PixelFormat32bppPARGB);
    {
        Graphics g(out.get());
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.Clear(Color(0, 0, 0, 0));
        g.DrawImage(src.get(), 0, 0, px, px);
    }
    scaled_[l] = std::move(out);
    scaledPx_[l] = px;
    return scaled_[l].get();
}

void ReplayView::releaseBitmaps() {
    for (int i = 0; i < 2; ++i) {
        scaled_[i].reset();
        scaledPx_[i] = 0;
    }
    srcIndex_ = -1;
}

}  // namespace fdw
