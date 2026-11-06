// SnowDesk – Pfade und Log
#include "common.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace app {

namespace {
CRITICAL_SECTION g_logLock;
bool g_logReady = false;
std::wstring g_logPath;
}  // namespace

std::wstring dataDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
        dir = base;
        CoTaskMemFree(base);
    } else {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        dir = (n > 0 && n < MAX_PATH) ? buf : L".";
    }
    dir += L"\\SnowDesk";
    CreateDirectoryW(dir.c_str(), nullptr);
    cached = dir + L"\\";
    return cached;
}

std::wstring exePath() {
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    return std::wstring(buf, n);
}

std::wstring exeDir() {
    std::wstring p = exePath();
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

void logInit() {
    if (g_logReady) return;
    InitializeCriticalSection(&g_logLock);
    g_logPath = dataDir() + L"SnowDesk.log";
    g_logReady = true;
}

void logLine(const wchar_t* fmt, ...) {
    if (!g_logReady) logInit();
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);
    msg[2047] = 0;

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[2200];
    _snwprintf(line, 2199, L"%04u-%02u-%02u %02u:%02u:%02u.%03u  %ls\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
               t.wMilliseconds, msg);
    line[2199] = 0;

    // UTF-8 schreiben
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return;
    std::string utf8((size_t)bytes - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, -1, &utf8[0], bytes, nullptr, nullptr);

    EnterCriticalSection(&g_logLock);
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh > 0 || info.nFileSizeLow > 1024 * 1024)) {
        std::wstring old = g_logPath + L".old";
        DeleteFileW(old.c_str());
        MoveFileW(g_logPath.c_str(), old.c_str());
    }
    HANDLE f = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(f);
    }
    LeaveCriticalSection(&g_logLock);
}

std::wstring hrText(HRESULT hr) {
    wchar_t buf[64];
    _snwprintf(buf, 63, L"0x%08lX", (unsigned long)hr);
    buf[63] = 0;
    std::wstring s = buf;
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)hr, 0,
                             (LPWSTR)&msg, 0, nullptr);
    if (n && msg) {
        std::wstring m(msg, n);
        while (!m.empty() && (m.back() == L'\r' || m.back() == L'\n' || m.back() == L' ' || m.back() == L'.')) m.pop_back();
        s += L" (" + m + L")";
    }
    if (msg) LocalFree(msg);
    return s;
}

}  // namespace app
