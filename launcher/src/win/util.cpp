// efficient lamp – Hilfen (siehe common.h)
#include "common.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>

namespace lamp {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring expandEnv(const std::wstring& s) {
    wchar_t buf[2048];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 2048);
    if (n == 0 || n > 2048) return s;
    return buf;
}

std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring parentDir(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring lowerCase(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], DWORD(s.size()));
    return s;
}

bool samePath(const std::wstring& a, const std::wstring& b) {
    wchar_t fa[MAX_PATH * 2], fb[MAX_PATH * 2];
    if (!GetFullPathNameW(a.c_str(), MAX_PATH * 2, fa, nullptr) || !GetFullPathNameW(b.c_str(), MAX_PATH * 2, fb, nullptr)) return false;
    return CompareStringOrdinal(fa, -1, fb, -1, TRUE) == CSTR_EQUAL;
}

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool readFile(const std::wstring& path, std::string& out, size_t maxBytes) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) && uint64_t(size.QuadPart) <= maxBytes;
    if (ok) {
        out.resize(size_t(size.QuadPart));
        size_t done = 0;
        while (ok && done < out.size()) {
            DWORD chunk = DWORD(out.size() - done > (1u << 24) ? (1u << 24) : out.size() - done), got = 0;
            ok = ReadFile(h, &out[done], chunk, &got, nullptr) && got > 0;
            done += got;
        }
    }
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        DWORD chunk = DWORD(data.size() - done > (1u << 24) ? (1u << 24) : data.size() - done), wrote = 0;
        ok = WriteFile(h, data.data() + done, chunk, &wrote, nullptr) && wrote == chunk;
        done += wrote;
    }
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

bool createDirs(const std::wstring& path) {
    if (path.empty() || dirExists(path)) return true;
    std::wstring parent = parentDir(path);
    if (!parent.empty() && parent != path && !(parent.size() == 2 && parent[1] == L':')) createDirs(parent);
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool deleteTree(const std::wstring& path) {
    if (!dirExists(path)) return !fileExists(path) || DeleteFileW(path.c_str());
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(joinPath(path, L"*").c_str(), &fd);
    bool ok = true;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = joinPath(path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ok = RemoveDirectoryW(p.c_str()) && ok;   // Verknüpfungen nicht verfolgen
                else ok = deleteTree(p) && ok;
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                ok = DeleteFileW(p.c_str()) && ok;
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(path.c_str()) && ok;
}

std::wstring selfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

static std::wstring knownFolder(REFKNOWNFOLDERID id, const wchar_t* envFallback) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    if (out.empty()) out = expandEnv(envFallback);
    return out;
}

std::wstring localAppData() { return knownFolder(FOLDERID_LocalAppData, L"%LOCALAPPDATA%"); }
std::wstring roamingAppData() { return knownFolder(FOLDERID_RoamingAppData, L"%APPDATA%"); }
std::wstring dataDir() { return joinPath(localAppData(), L"EfficientLamp"); }
std::wstring installDir() { return joinPath(localAppData(), L"Programs\\EfficientLamp"); }

std::wstring formatBytes(uint64_t b) {
    wchar_t buf[32];
    if (b < 1024) swprintf(buf, 32, L"%u Byte", unsigned(b));
    else if (b < 1024 * 1024) swprintf(buf, 32, L"%u KB", unsigned((b + 512) / 1024));
    else {
        swprintf(buf, 32, L"%.1f MB", double(b) / (1024.0 * 1024.0));
        for (wchar_t* p = buf; *p; ++p)
            if (*p == L'.') *p = L',';
    }
    return buf;
}

std::wstring fileVersion(const std::wstring& exe) {
    DWORD dummy = 0;
    DWORD n = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (!n) return std::wstring();
    std::string buf(n, '\0');
    if (!GetFileVersionInfoW(exe.c_str(), 0, n, &buf[0])) return std::wstring();
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return std::wstring();
    wchar_t v[64];
    swprintf(v, 64, L"%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS));
    return v;
}

void logLine(const wchar_t* fmt, ...) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    static std::wstring path;
    if (path.empty()) {
        createDirs(dataDir());
        path = joinPath(dataDir(), L"launcher.log");
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 256 * 1024)
            MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);
    msg[2047] = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2200];
    _snwprintf(line, 2199, L"%04u-%02u-%02u %02u:%02u:%02u  %ls\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    line[2199] = 0;
    std::string utf8 = narrow(line);
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, utf8.data(), DWORD(utf8.size()), &w, nullptr);
        CloseHandle(h);
    }
}

}  // namespace lamp
