// DeskCats – gemeinsame Windows-Definitionen
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <string>
#include <vector>

namespace app {

// Kleiner COM-Zeiger (ohne WRL)
template <class T>
class Com {
public:
    Com() = default;
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com& operator=(const Com& o) {
        if (o.p_) o.p_->AddRef();
        reset();
        p_ = o.p_;
        return *this;
    }
    ~Com() { reset(); }
    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

// Pfade
std::wstring dataDir();      // %LOCALAPPDATA%\DeskCats (wird angelegt)
std::wstring exePath();      // vollständiger Pfad dieser .exe
std::wstring exeDir();       // Ordner dieser .exe (mit abschließendem \)

// Log (DeskCats.log im Datenordner, rotiert bei 1 MB)
void logInit();
void logLine(const wchar_t* fmt, ...);
std::wstring hrText(HRESULT hr);

// Kennungen für die Verständigung mit dem Kontextmenü (DeskCatsMenu.dll) und weiteren Instanzen
constexpr const wchar_t* kHostClass = L"DeskCatsHost";
constexpr const wchar_t* kRegKey = L"Software\\DeskCats";
constexpr ULONG_PTR kCopyDataId = 0x43415453;   // 'CATS'

}  // namespace app
