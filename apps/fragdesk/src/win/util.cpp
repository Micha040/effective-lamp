// FragDesk – Hilfen (siehe common.h)
#include "common.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>

namespace fdw {

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
    wchar_t buf[4096];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 4096);
    if (n == 0 || n > 4096) return s;
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

std::wstring fileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
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

bool endsWithI(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    return CompareStringOrdinal(s.c_str() + s.size() - suffix.size(), int(suffix.size()), suffix.c_str(), int(suffix.size()), TRUE) == CSTR_EQUAL;
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

int64_t fileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return -1;
    return (int64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
}

static int64_t fromFileTime(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if (u.QuadPart < 116444736000000000ULL) return 0;
    return int64_t((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

int64_t fileTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return fromFileTime(fa.ftLastWriteTime);
}

std::wstring selfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

std::wstring selfDir() { return parentDir(selfPath()); }

std::wstring knownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring localAppData() {
    std::wstring p = knownFolder(FOLDERID_LocalAppData);
    return p.empty() ? expandEnv(L"%LOCALAPPDATA%") : p;
}

static std::wstring& dataDirOverride() {
    static std::wstring d;
    return d;
}

std::wstring dataDir() {
    const std::wstring& o = dataDirOverride();
    if (!o.empty()) return o;
    std::wstring env = expandEnv(L"%FRAGDESK_DATA%");   // für Tests
    if (!env.empty() && env != L"%FRAGDESK_DATA%") return env;
    return joinPath(localAppData(), L"FragDesk");
}

std::wstring matchesDir() { return joinPath(dataDir(), L"matches"); }
std::wstring tempDir() { return joinPath(dataDir(), L"temp"); }
std::wstring demosDir() { return joinPath(dataDir(), L"demos"); }

std::wstring downloadsDir() {
    std::wstring p = knownFolder(FOLDERID_Downloads);
    return p.empty() ? expandEnv(L"%USERPROFILE%\\Downloads") : p;
}

std::wstring videosDir() {
    std::wstring p = knownFolder(FOLDERID_Videos);
    return p.empty() ? expandEnv(L"%USERPROFILE%\\Videos") : p;
}

SYSTEMTIME localTime(int64_t epoch) {
    ULARGE_INTEGER u;
    u.QuadPart = uint64_t(epoch) * 10000000ULL + 116444736000000000ULL;
    FILETIME ft, lft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME utc = {}, st = {};
    FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &st)) {
        FileTimeToLocalFileTime(&ft, &lft);
        FileTimeToSystemTime(&lft, &st);
    }
    return st;
}

std::wstring dateText(int64_t epoch) {
    if (epoch <= 0) return L"–";
    SYSTEMTIME st = localTime(epoch);
    wchar_t b[32];
    swprintf(b, 32, L"%02u.%02u.%04u", st.wDay, st.wMonth, st.wYear);
    return b;
}

std::wstring dateTimeText(int64_t epoch) {
    if (epoch <= 0) return L"–";
    SYSTEMTIME st = localTime(epoch);
    wchar_t b[40];
    swprintf(b, 40, L"%02u.%02u.%04u, %02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return b;
}

std::wstring relativeDay(int64_t epoch) {
    if (epoch <= 0) return L"–";
    SYSTEMTIME st = localTime(epoch), now;
    GetLocalTime(&now);
    auto dayNum = [](const SYSTEMTIME& s) {
        FILETIME ft;
        SYSTEMTIME d = s;
        d.wHour = d.wMinute = d.wSecond = d.wMilliseconds = 0;
        SystemTimeToFileTime(&d, &ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return int64_t(u.QuadPart / (10000000ULL * 86400ULL));
    };
    int64_t diff = dayNum(now) - dayNum(st);
    wchar_t b[40];
    if (diff == 0) swprintf(b, 40, L"Heute, %02u:%02u", st.wHour, st.wMinute);
    else if (diff == 1) swprintf(b, 40, L"Gestern, %02u:%02u", st.wHour, st.wMinute);
    else if (st.wYear == now.wYear) swprintf(b, 40, L"%02u.%02u., %02u:%02u", st.wDay, st.wMonth, st.wHour, st.wMinute);
    else swprintf(b, 40, L"%02u.%02u.%04u", st.wDay, st.wMonth, st.wYear);
    return b;
}

void openInShell(const std::wstring& target) {
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void showInExplorer(const std::wstring& file) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    } else {
        openInShell(parentDir(file));
    }
}

bool pickFolder(HWND owner, const std::wstring& title, std::wstring& folder) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title.c_str());
    if (!folder.empty()) {
        IShellItem* start = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dlg->SetFolder(start);
            start->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                folder = p;
                ok = true;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

bool pickFiles(HWND owner, std::vector<std::wstring>& files) {
    files.clear();
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    COMDLG_FILTERSPEC spec[] = {{L"CS2-Demos", L"*.dem;*.dem.zst;*.dem.gz;*.dem.bz2;*.zst;*.gz;*.bz2"}, {L"Alle Dateien", L"*.*"}};
    dlg->SetFileTypes(2, spec);
    dlg->SetTitle(L"Demos hinzufügen");
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &it))) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                        files.push_back(p);
                        CoTaskMemFree(p);
                    }
                    it->Release();
                }
            }
            items->Release();
        }
    }
    dlg->Release();
    return !files.empty();
}

std::wstring clipboardText(HWND owner) {
    std::wstring text;
    if (OpenClipboard(owner)) {
        if (HANDLE hd = GetClipboardData(CF_UNICODETEXT))
            if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(hd))) {
                text.assign(p, wcsnlen(p, GlobalSize(hd) / sizeof(wchar_t)));
                GlobalUnlock(hd);
            }
        CloseClipboard();
    }
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    size_t lead = 0;
    while (lead < text.size() && iswspace(text[lead])) ++lead;
    return text.substr(lead);
}

bool setClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    EmptyClipboard();
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
        if (void* p = GlobalLock(g)) {
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        }
        if (!ok) GlobalFree(g);   // gehört sonst der Zwischenablage
    }
    CloseClipboard();
    return ok;
}

void logLine(const wchar_t* fmt, ...) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    static std::wstring path;
    if (path.empty()) {
        createDirs(dataDir());
        path = joinPath(dataDir(), L"fragdesk.log");
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024)
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

}  // namespace fdw
