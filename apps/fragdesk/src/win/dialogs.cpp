// FragDesk – kleine Eingabe-Dialoge (siehe dialogs.h)
#include "dialogs.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <functional>
#include <vector>

#include "common.h"
#include "gfx.h"

using namespace Gdiplus;

namespace fdw {

namespace {

using ui::Btn;
using ui::F;

constexpr wchar_t kClass[] = L"FragDeskInputDialog";
constexpr float kW = 460, kPad = 24, kFieldH = 38;

struct Field {
    std::wstring label, help;
    bool password = false, upper = false;
    int maxLen = 64;
    HWND edit = nullptr;
    std::wstring value;
    RectF frame;   // in DIP
};

struct Dialog {
    HWND hwnd = nullptr, owner = nullptr;
    ui::Theme th;
    ui::Fonts fonts;
    float scale = 1;
    std::wstring title, text, okLabel, error;
    std::vector<Field> fields;
    std::function<std::wstring(Dialog&)> validate;   // "" = in Ordnung, sonst der Grund
    std::vector<ui::Region> regions;
    uint32_t hot = 0, pressed = 0;
    bool tracking = false, done = false, ok = false;
    HFONT editFont = nullptr;
    HBRUSH fieldBrush = nullptr;
    float textH = 0, height = 0;

    void layout();
    void paint();
    void submit();
    void cancel() {
        done = true;
        ok = false;
    }
    int regionAt(int x, int y) const {
        for (int i = int(regions.size()) - 1; i >= 0; --i) {
            const RECT& r = regions[i].px;
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
        }
        return -1;
    }
};

// Höhen messen (DIP): ein kleines Bild reicht, die Schriften rechnen in Pixeln wie im Fenster
void Dialog::layout() {
    Bitmap bmp(4, 4, PixelFormat32bppARGB);
    Graphics g(&bmp);
    std::vector<ui::Region> none;
    ui::Canvas c(g, th, fonts, 1.0f, none, 0, 0);
    float w = kW - 2 * kPad;
    textH = text.empty() ? 0 : c.textHeight(text, F::Small, w) + 4;
    float y = kPad + 30 + (textH > 0 ? textH + 10 : 0);
    for (Field& f : fields) {
        y += 22;   // Beschriftung
        f.frame = RectF(kPad, y, w, kFieldH);
        y += kFieldH + 6;
        if (!f.help.empty()) y += c.textHeight(f.help, F::Tiny, w) + 6;
        y += 8;
    }
    y += 22;   // Platz für einen Hinweis (z. B. „Code hat 5 Zeichen“)
    height = y + 36 + kPad;
}

void Dialog::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = std::max(1L, rc.right), h = std::max(1L, rc.bottom);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        regions.clear();
        ui::Canvas c(g, th, fonts, scale, regions, hot, pressed);
        float cw = kW;
        c.fill(RectF(0, 0, cw, height + 40), th.card);
        c.text(title, F::H2, th.text, RectF(kPad, kPad - 2, cw - 2 * kPad, 26));
        float y = kPad + 30;
        if (textH > 0) {
            c.text(text, F::Small, th.text2, RectF(kPad, y, cw - 2 * kPad, textH), ui::Align::Left, false, true);
            y += textH + 10;
        }
        HWND focus = GetFocus();
        for (const Field& f : fields) {
            c.text(f.label, F::SmallSemi, th.text, RectF(kPad, f.frame.Y - 22, cw - 2 * kPad, 20));
            bool on = focus == f.edit;
            c.round(f.frame, 8, th.dark ? th.card2 : th.bg);
            c.roundStroke(f.frame, 8, on ? th.accent : th.border, on ? 1.6f : 1);
            if (!f.help.empty()) {
                float hh = c.textHeight(f.help, F::Tiny, cw - 2 * kPad) + 2;
                c.text(f.help, F::Tiny, th.text3, RectF(kPad, f.frame.Y + kFieldH + 5, cw - 2 * kPad, hh), ui::Align::Left, false, true);
            }
        }
        float by = height - kPad - 36;
        if (!error.empty()) c.text(error, F::Small, th.danger, RectF(kPad, by - 26, cw - 2 * kPad, 20));
        float okW = std::max(120.0f, c.buttonWidth(okLabel)), noW = std::max(110.0f, c.buttonWidth(L"Abbrechen"));
        c.button(RectF(cw - kPad - okW, by, okW, 36), okLabel, "dlg-ok", [this] { submit(); }, Btn::Primary);
        c.button(RectF(cw - kPad - okW - 10 - noW, by, noW, 36), L"Abbrechen", "dlg-no", [this] { cancel(); }, Btn::Secondary);
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void Dialog::submit() {
    for (Field& f : fields) {
        int n = GetWindowTextLengthW(f.edit);
        std::vector<wchar_t> buf(size_t(n) + 1, L'\0');
        GetWindowTextW(f.edit, buf.data(), n + 1);
        f.value.assign(buf.data());
        SecureZeroMemory(buf.data(), buf.size() * sizeof(wchar_t));
    }
    error = validate ? validate(*this) : std::wstring();
    if (error.empty()) {
        done = true;
        ok = true;
        return;
    }
    for (Field& f : fields)   // nur der Grund, das Passwort bleibt nicht im Speicher stehen
        if (f.password && !f.value.empty()) {
            SecureZeroMemory(&f.value[0], f.value.size() * sizeof(wchar_t));
            f.value.clear();
        }
    MessageBeep(MB_ICONWARNING);
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK dialogProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        d = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        d->hwnd = h;
    }
    if (!d) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_PAINT: d->paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            Color t = d->th.text, b = d->th.dark ? d->th.card2 : d->th.bg;
            SetTextColor(dc, RGB(t.GetR(), t.GetG(), t.GetB()));
            SetBkColor(dc, RGB(b.GetR(), b.GetG(), b.GetB()));
            return reinterpret_cast<LRESULT>(d->fieldBrush);
        }
        case WM_COMMAND:
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);   // Rahmen des Felds
            return 0;
        case WM_MOUSEMOVE: {
            if (!d->tracking) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
                d->tracking = TrackMouseEvent(&t);
            }
            int i = d->regionAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            uint32_t id = i >= 0 ? d->regions[i].id : 0;
            if (id != d->hot) {
                d->hot = id;
                InvalidateRect(h, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            d->tracking = false;
            d->hot = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(h);
            d->pressed = d->hot;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            int i = d->regionAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            uint32_t was = d->pressed;
            d->pressed = 0;
            if (i >= 0 && d->regions[i].id == was && d->regions[i].click) {
                auto fn = d->regions[i].click;
                fn();
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: d->cancel(); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool runDialog(Dialog& d) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = dialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    d.fonts.init();
    d.layout();
    Color fb = d.th.dark ? d.th.card2 : d.th.bg;
    d.fieldBrush = CreateSolidBrush(RGB(fb.GetR(), fb.GetG(), fb.GetB()));
    d.editFont = CreateFontW(-int(14 * d.scale + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME;
    RECT r = {0, 0, LONG(kW * d.scale), LONG(d.height * d.scale)};
    AdjustWindowRectEx(&r, style, FALSE, ex);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT o = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (d.owner) GetWindowRect(d.owner, &o);
    int x = o.left + ((o.right - o.left) - ww) / 2, y = o.top + ((o.bottom - o.top) - wh) / 3;
    HWND h = CreateWindowExW(ex, kClass, d.title.c_str(), style, x, y, ww, wh, d.owner, nullptr, inst, &d);
    if (!h) {
        DeleteObject(d.fieldBrush);
        DeleteObject(d.editFont);
        return false;
    }
    BOOL dark = d.th.dark ? TRUE : FALSE;   // Titelleiste wie FragDesk
    if (FAILED(DwmSetWindowAttribute(h, 20, &dark, sizeof dark))) DwmSetWindowAttribute(h, 19, &dark, sizeof dark);
    COLORREF cap = RGB(d.th.card.GetR(), d.th.card.GetG(), d.th.card.GetB()), txt = RGB(d.th.text.GetR(), d.th.text.GetG(), d.th.text.GetB());
    DwmSetWindowAttribute(h, 35, &cap, sizeof cap);
    DwmSetWindowAttribute(h, 36, &txt, sizeof txt);
    int fontH = int(14 * d.scale + 0.5f) + int(6 * d.scale);
    for (size_t i = 0; i < d.fields.size(); ++i) {
        Field& f = d.fields[i];
        RECT fr = {LONG((f.frame.X + 12) * d.scale), LONG(f.frame.Y * d.scale + (kFieldH * d.scale - fontH) / 2), LONG((f.frame.X + f.frame.Width - 12) * d.scale),
                   0};
        fr.bottom = fr.top + fontH;
        DWORD es = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (f.password ? ES_PASSWORD : 0) | (f.upper ? ES_UPPERCASE : 0);
        f.edit = CreateWindowExW(0, L"EDIT", f.value.c_str(), es, fr.left, fr.top, fr.right - fr.left, fr.bottom - fr.top, h, HMENU(INT_PTR(100 + i)), inst,
                                 nullptr);
        SendMessageW(f.edit, WM_SETFONT, WPARAM(d.editFont), TRUE);
        SendMessageW(f.edit, EM_LIMITTEXT, WPARAM(f.maxLen), 0);
        SendMessageW(f.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }
    if (d.owner) EnableWindow(d.owner, FALSE);
    ShowWindow(h, SW_SHOW);
    if (!d.fields.empty()) {
        SetFocus(d.fields[0].edit);
        SendMessageW(d.fields[0].edit, EM_SETSEL, 0, -1);
    }
    MSG m;
    while (!d.done) {
        BOOL got = GetMessageW(&m, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(int(m.wParam));   // FragDesk wird beendet: weiterreichen
            d.cancel();
            break;
        }
        if (m.message == WM_KEYDOWN && (m.hwnd == h || IsChild(h, m.hwnd))) {
            if (m.wParam == VK_RETURN) {
                d.submit();
                continue;
            }
            if (m.wParam == VK_ESCAPE) {
                d.cancel();
                continue;
            }
            if (m.wParam == VK_TAB && !d.fields.empty()) {   // nächstes (mit Umschalt: voriges) Feld
                size_t n = d.fields.size(), at = 0;
                for (size_t i = 0; i < n; ++i)
                    if (d.fields[i].edit == GetFocus()) at = i;
                size_t next = (GetKeyState(VK_SHIFT) < 0) ? (at + n - 1) % n : (at + 1) % n;
                SetFocus(d.fields[next].edit);
                SendMessageW(d.fields[next].edit, EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    for (Field& f : d.fields)
        if (f.password && f.edit) SetWindowTextW(f.edit, L"");   // Passwort nicht im Steuerelement stehen lassen
    if (d.owner) {
        EnableWindow(d.owner, TRUE);
        SetForegroundWindow(d.owner);
    }
    DestroyWindow(h);
    DeleteObject(d.fieldBrush);
    DeleteObject(d.editFont);
    return d.ok;
}

std::wstring trimmed(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n\x00A0"), b = s.find_last_not_of(L" \t\r\n\x00A0");
    return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

}  // namespace

bool askBotLogin(HWND owner, const DialogLook& look, std::wstring& user, std::wstring& password) {
    Dialog d;
    d.owner = owner;
    d.th = ui::makeTheme(look.dark);
    d.scale = look.scale;
    d.title = L"Steam-Bot anmelden";
    d.text = L"Mit dem eigenen Steam-Konto des Servers (nicht deinem). Gleich fragt Steam nach einem Code (E-Mail oder Steam-App) oder du "
             L"bestätigst die Anmeldung in der Steam-App.";
    d.okLabel = L"Anmelden";
    Field u;
    u.label = L"Benutzername des Bot-Kontos";
    u.help = L"So, wie du dich bei Steam anmeldest – nicht der Anzeigename.";
    u.value = user;
    Field p;
    p.label = L"Passwort";
    p.help = L"Bleibt nur kurz im Arbeitsspeicher des Servers und landet nie auf der Platte.";
    p.password = true;
    p.maxLen = 256;
    d.fields = {u, p};
    d.validate = [](Dialog& dl) -> std::wstring {
        std::wstring name = trimmed(dl.fields[0].value);
        if (name.empty() || dl.fields[1].value.empty()) return L"Benutzername und Passwort eintragen.";
        if (name.find_first_of(L" \t") != std::wstring::npos) return L"Der Benutzername hat keine Leerzeichen.";
        return L"";
    };
    if (!runDialog(d)) return false;
    user = trimmed(d.fields[0].value);
    password.swap(d.fields[1].value);
    return true;
}

bool askGuardCode(HWND owner, const DialogLook& look, bool fromApp, const std::wstring& hint, std::wstring& code) {
    Dialog d;
    d.owner = owner;
    d.th = ui::makeTheme(look.dark);
    d.scale = look.scale;
    d.title = fromApp ? L"Code aus der Steam-App" : L"Code aus der E-Mail von Steam";
    d.text = hint.empty() ? (fromApp ? L"Steam Guard in der Steam-App auf dem Handy des Bot-Kontos öffnen und den Code eingeben."
                                     : L"Steam hat dem Bot-Konto eine E-Mail mit einem Code geschickt.")
                          : hint;
    d.okLabel = L"Code senden";
    Field c;
    c.label = L"Steam-Guard-Code";
    c.help = L"5 Zeichen, Buchstaben und Ziffern.";
    c.upper = true;
    c.maxLen = 8;
    d.fields = {c};
    d.validate = [](Dialog& dl) -> std::wstring {
        std::wstring v;
        for (wchar_t ch : dl.fields[0].value)
            if (!iswspace(ch)) v += ch;
        bool ok = v.size() == 5 && std::all_of(v.begin(), v.end(), [](wchar_t ch) { return (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'); });
        if (!ok) return L"Der Code hat 5 Zeichen (Buchstaben und Ziffern).";
        dl.fields[0].value = v;
        return L"";
    };
    if (!runDialog(d)) return false;
    code = d.fields[0].value;
    return true;
}

}  // namespace fdw
