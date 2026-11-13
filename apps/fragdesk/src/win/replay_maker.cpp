// FragDesk – 2D-Daten erstellen (siehe replay_maker.h)
#include "replay_maker.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "common.h"

namespace fdw {

namespace {

// fdanalyze endet mit FragDesk
HANDLE analyzerJob() {
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {   // … und bekommt höchstens kAnalyzerMemoryLimit Arbeitsspeicher
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            li.ProcessMemoryLimit = kAnalyzerMemoryLimit;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li);
        }
        return j;
    }();
    return job;
}

// Ordner samt Inhalt löschen (eigener Zwischenordner eines Laufs: entpackte Demo, wenn abgebrochen wurde)
void removeTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(joinPath(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = joinPath(dir, n);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) removeTree(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

}  // namespace

std::wstring replayFile(const std::string& checksum) { return joinPath(matchesDir(), widen(checksum) + L".replay"); }

bool replayErrorFinal(const std::string& code) {
    return code == "FORMAT" || code == "CSGO" || code == "UNPACK" || code == "POV" || code == "SOURCE" || code == "CORRUPT" || code == "EMPTY";
}

std::string makeReplay(const std::wstring& demo, const std::wstring& target, DWORD priority, const std::function<bool()>& cancel) {
    std::wstring exe = joinPath(selfDir(), kAnalyzerExe);
    if (!fileExists(exe)) return "MISSING";
    static std::atomic<int> counter{0};
    std::wstring run = L"replay-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    std::wstring tmp = joinPath(parentDir(target), L"tmp-" + run + L".replay"), temp = joinPath(tempDir(), run);
    createDirs(parentDir(target));
    createDirs(temp);
    auto cleanup = [&] {
        DeleteFileW(tmp.c_str());
        DeleteFileW((tmp + L".part").c_str());
        removeTree(temp);
    };
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        cleanup();
        return "START";
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = L"\"" + exe + L"\" -demo \"" + demo + L"\" -replay \"" + tmp + L"\" -temp \"" + temp + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | priority | CREATE_SUSPENDED, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        logLine(L"fdanalyze -replay startet nicht (%lu)", GetLastError());
        CloseHandle(rd);
        cleanup();
        return "START";
    }
    if (HANDLE job = analyzerJob()) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    std::string pending, code, done;
    auto drain = [&] {   // Ausgabe lesen, ohne zu blockieren (fdanalyze schreibt nur ein paar Zeilen)
        DWORD avail = 0, got = 0;
        char chunk[512];
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            if (!ReadFile(rd, chunk, std::min<DWORD>(avail, DWORD(sizeof chunk)), &got, nullptr) || got == 0) break;
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
    };
    bool cancelled = false;
    for (;;) {
        DWORD w = WaitForSingleObject(pi.hProcess, 500);
        drain();
        if (w != WAIT_TIMEOUT) break;
        if (cancel && cancel()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 10000);
            cancelled = true;
            break;
        }
    }
    drain();
    CloseHandle(rd);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::string result;
    if (cancelled) result = "CANCELLED";
    else if (exitCode != 0 || done.empty()) result = code.empty() ? (exitCode != 0 ? "CRASH" : "INTERNAL") : code;
    else if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) result = "WRITE";
    cleanup();
    return result;
}

}  // namespace fdw
