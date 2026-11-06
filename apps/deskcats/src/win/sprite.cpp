// DeskCats – Fenster pro Katze (siehe sprite.h)
#include "sprite.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>

namespace app {

namespace {
const wchar_t* kClass = L"DeskCatsSprite";
constexpr UINT kZFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
}  // namespace

bool SpriteWindow::create(HINSTANCE inst, int id, MouseFn fn) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }
    id_ = id;
    mouse_ = std::move(fn);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    // nicht in Alt+Tab, nicht in Aufnahmen von Fensterwechseln animiert
    BOOL off = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof(off));
    memDC_ = CreateCompatibleDC(nullptr);
    return memDC_ != nullptr;
}

void SpriteWindow::destroy() {
    if (memDC_) {
        if (oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) DeleteObject(dib_);
    dib_ = nullptr;
    bits_ = nullptr;
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    shown_ = false;
}

void SpriteWindow::ensureDib(int w, int h) {
    if (bits_ && w <= dibW_ && h <= dibH_) return;
    int nw = std::max(w, dibW_), nh = std::max(h, dibH_);
    BITMAPINFO bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = nw;
    bi.bmiHeader.biHeight = -nh;   // von oben nach unten
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return;
    if (oldBmp_) SelectObject(memDC_, oldBmp_);
    if (dib_) DeleteObject(dib_);
    dib_ = bmp;
    oldBmp_ = (HBITMAP)SelectObject(memDC_, dib_);
    bits_ = static_cast<uint32_t*>(bits);
    dibW_ = nw;
    dibH_ = nh;
}

void SpriteWindow::show(const cat::PixelImage& img, int scale, int left, int top, bool contentChanged) {
    if (!hwnd_ || img.w <= 0 || img.h <= 0) return;
    scale = std::max(1, scale);
    const int W = img.w * scale, H = img.h * scale;
    if (contentChanged || W != curW_ || H != curH_ || !shown_) {
        ensureDib(W, H);
        if (!bits_) return;
        // ganzzahlig vergrößern (harte Pixel); Werte sind schon vormultipliziert (Alpha 0 oder 255)
        for (int y = 0; y < img.h; ++y) {
            uint32_t* row = bits_ + (size_t)y * scale * dibW_;
            const uint32_t* src = img.px.data() + (size_t)y * img.w;
            for (int x = 0; x < img.w; ++x) {
                uint32_t v = src[x];
                uint32_t* d = row + (size_t)x * scale;
                for (int k = 0; k < scale; ++k) d[k] = v;
            }
            for (int k = 1; k < scale; ++k) std::memcpy(row + (size_t)k * dibW_, row, (size_t)W * sizeof(uint32_t));
        }
        POINT dst{left, top}, src{0, 0};
        SIZE size{W, H};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, nullptr, &dst, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
        curX_ = left;
        curY_ = top;
        curW_ = W;
        curH_ = H;
    } else if (left != curX_ || top != curY_) {
        SetWindowPos(hwnd_, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        curX_ = left;
        curY_ = top;
    }
    if (!shown_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        shown_ = true;
    }
}

void SpriteWindow::hide() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    z = ZState{};   // nach dem Wiederzeigen neu einordnen
}

void SpriteWindow::zAbove(HWND platform) {
    if (!hwnd_ || !platform) return;
    if (topmost_) {
        SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, kZFlags);
        topmost_ = false;
    }
    HWND prev = GetWindow(platform, GW_HWNDPREV);
    if (prev == hwnd_) return;   // liegt schon direkt darüber
    HWND after = prev;
    // ganz oben (bzw. nur "immer oben"-Fenster darüber): an die Spitze der normalen Fenster
    if (!prev || (GetWindowLongPtrW(prev, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = HWND_TOP;
    SetWindowPos(hwnd_, after, 0, 0, 0, 0, kZFlags);
}

void SpriteWindow::zTopmost() {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, kZFlags);
    topmost_ = true;
}

void SpriteWindow::zAboveDesktop(HWND desktopTop) {
    if (!hwnd_) return;
    if (topmost_) {
        SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, kZFlags);
        topmost_ = false;
    }
    if (!desktopTop) return;
    HWND prev = GetWindow(desktopTop, GW_HWNDPREV);
    if (prev == hwnd_) return;
    SetWindowPos(hwnd_, prev ? prev : HWND_TOP, 0, 0, 0, 0, kZFlags);
}

bool SpriteWindow::directlyAbove(HWND platform, const std::function<bool(HWND)>& isOwn, HWND* blocker) const {
    if (blocker) *blocker = nullptr;
    if (!hwnd_ || topmost_) return false;
    int guard = 0;
    for (HWND h = GetWindow(hwnd_, GW_HWNDNEXT); h && guard < 4096; h = GetWindow(h, GW_HWNDNEXT), ++guard) {
        if (h == platform) return true;
        if (isOwn(h) || !IsWindowVisible(h)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        if (blocker) *blocker = h;
        return false;
    }
    return false;
}

LRESULT CALLBACK SpriteWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SpriteWindow* self = reinterpret_cast<SpriteWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_RBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_CAPTURECHANGED:
            if (self && self->mouse_) {
                POINT pt;
                GetCursorPos(&pt);
                self->mouse_(self->id_, msg, pt);
            }
            return 0;
        case WM_RBUTTONDOWN: return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace app
