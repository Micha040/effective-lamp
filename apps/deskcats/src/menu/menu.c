// DeskCats – Eintrag "Katzen" im Windows-11-Kontextmenü des Desktops (IExplorerCommand mit Untermenü).
// Explorer lädt diese DLL über das DeskCats-App-Paket (siehe src/win/pack.cpp). Sie läuft im Explorer-Prozess und ist
// deshalb klein und vorsichtig: Menüaufbau liest nur zwei Registry-Werte, Klicks werden per WM_COPYDATA an das laufende
// DeskCats weitergereicht (oder DeskCats wird gestartet) – kein Warten, kein Netzwerk.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

// {9D533FE8-F14F-473D-8963-923041D1D1EF} – muss zu kMenuClsid in src/win/pack.h passen
static const CLSID kClsid = {0x9d533fe8, 0xf14f, 0x473d, {0x89, 0x63, 0x92, 0x30, 0x41, 0xd1, 0xd1, 0xef}};
static const IID kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIidClassFactory = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIidExplorerCommand = {0xa08ce4d0, 0xfa25, 0x44ab, {0xb5, 0x7c, 0xc7, 0xb1, 0xc3, 0x23, 0xe0, 0xb9}};
static const IID kIidEnumExplorerCommand = {0xa88826f8, 0x186f, 0x4987, {0xaa, 0xde, 0xea, 0x0c, 0xef, 0x8f, 0xbf, 0xe8}};
static const KNOWNFOLDERID kFolderDesktop = {0xb4bfcc3a, 0xdb2c, 0x424c, {0xb0, 0x29, 0x7f, 0xe9, 0x9a, 0x87, 0xc6, 0x41}};
static const KNOWNFOLDERID kFolderPublicDesktop = {0xc4aa340d, 0xf20f, 0x4863, {0xaf, 0xef, 0xf8, 0x7e, 0xf2, 0xe6, 0xba, 0x25}};

#define HOST_CLASS L"DeskCatsHost"
#define REG_KEY L"Software\\DeskCats"
#define APP_EXE L"DeskCats.exe"
#define COPYDATA_ID 0x43415453 /* 'CATS' */

enum Kind { KindRoot = -1, KindFeed = 0, KindPlay, KindCall, KindSleep, KindShow, KindSettings, KindCount };

static HMODULE g_module;
static volatile LONG g_locks;

static BOOL SameGuid(const GUID *a, const GUID *b) {
    const BYTE *x = (const BYTE *)a, *y = (const BYTE *)b;
    for (int i = 0; i < (int)sizeof(GUID); i++) {
        if (x[i] != y[i]) return FALSE;
    }
    return TRUE;
}

// ---- Zustand ----------------------------------------------------------------------------------------------------

static DWORD ReadDword(const WCHAR *name, DWORD fallback) {
    DWORD value = fallback, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, name, RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS) return fallback;
    return value;
}

static HWND RunningHost(void) { return FindWindowW(HOST_CLASS, NULL); }

static BOOL CatsVisible(void) { return RunningHost() != NULL && ReadDword(L"Visible", 1) != 0; }
static BOOL CatsSleeping(void) { return CatsVisible() && ReadDword(L"Sleeping", 0) != 0; }

static BOOL PathNextToModule(const WCHAR *name, WCHAR *buffer, DWORD size) {
    DWORD length = GetModuleFileNameW(g_module, buffer, size);
    if (length == 0 || length >= size) return FALSE;
    while (length > 0 && buffer[length - 1] != L'\\') length--;
    if (length == 0 || length + (DWORD)lstrlenW(name) >= size) return FALSE;
    lstrcpyW(buffer + length, name);
    return TRUE;
}

static BOOL SamePath(const WCHAR *a, KNOWNFOLDERID const *folder) {
    PWSTR known = NULL;
    BOOL same = FALSE;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, NULL, &known)) && known) {
        same = lstrcmpiW(a, known) == 0;
        CoTaskMemFree(known);
    }
    return same;
}

// Nur auf dem Desktop anzeigen (der Eintrag gilt technisch für jeden Ordner-Hintergrund).
// Im Zweifel anzeigen; ausblenden nur, wenn sicher ein anderer Ordner gemeint ist.
static BOOL IsDesktop(IShellItemArray *items) {
    if (!items) return TRUE;
    DWORD count = 0;
    if (FAILED(IShellItemArray_GetCount(items, &count)) || count != 1) return TRUE;
    IShellItem *item = NULL;
    if (FAILED(IShellItemArray_GetItemAt(items, 0, &item)) || !item) return TRUE;
    BOOL result = TRUE;
    PIDLIST_ABSOLUTE pidl = NULL;
    if (SUCCEEDED(SHGetIDListFromObject((IUnknown *)item, &pidl)) && pidl) {
        if (pidl->mkid.cb == 0) {
            result = TRUE;   // Wurzel des Namensraums = Desktop
        } else {
            LPWSTR path = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                result = SamePath(path, &kFolderDesktop) || SamePath(path, &kFolderPublicDesktop);
                CoTaskMemFree(path);
            } else {
                result = FALSE;   // virtueller Ordner (Dieser PC, Papierkorb, …)
            }
        }
        CoTaskMemFree(pidl);
    }
    IShellItem_Release(item);
    return result;
}

// ---- Befehl ausführen ---------------------------------------------------------------------------------------------

static const WCHAR *CommandFor(int kind) {
    switch (kind) {
        case KindFeed: return L"feed";
        case KindPlay: return L"play";
        case KindCall: return L"call";
        case KindSleep: return CatsSleeping() ? L"wake" : L"sleep";
        case KindShow: return CatsVisible() ? L"hide" : L"show";
        case KindSettings: return L"settings";
        default: return NULL;
    }
}

static HRESULT Run(int kind) {
    const WCHAR *cmd = CommandFor(kind);
    if (!cmd) return E_INVALIDARG;
    HWND host = RunningHost();
    if (host) {
        DWORD pid = 0;
        GetWindowThreadProcessId(host, &pid);
        AllowSetForegroundWindow(pid);   // Explorer hat den Vordergrund; DeskCats darf sein Fenster nach vorne holen
        COPYDATASTRUCT cds;
        cds.dwData = COPYDATA_ID;
        cds.cbData = (DWORD)((lstrlenW(cmd) + 1) * sizeof(WCHAR));
        cds.lpData = (PVOID)cmd;
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(host, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) && result) return S_OK;
    }
    if (kind == KindSleep) return S_FALSE;   // läuft nicht: niemand zum Schlafenschicken
    WCHAR exe[1024];
    if (!PathNextToModule(APP_EXE, exe, ARRAYSIZE(exe))) return E_FAIL;
    WCHAR line[1200];
    if (lstrlenW(exe) + lstrlenW(cmd) + 16 >= (int)ARRAYSIZE(line)) return E_FAIL;
    lstrcpyW(line, L"\"");
    lstrcatW(line, exe);
    lstrcatW(line, L"\" --cmd ");
    lstrcatW(line, cmd);
    WCHAR folder[1024];
    lstrcpynW(folder, exe, ARRAYSIZE(folder));
    for (int i = lstrlenW(folder) - 1; i >= 0 && folder[i] != L'\\'; i--) folder[i] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    SecureZeroMemory(&si, sizeof(si));
    SecureZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, folder, &si, &pi)) return HRESULT_FROM_WIN32(GetLastError());
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}

// ---- IExplorerCommand -------------------------------------------------------------------------------------------

typedef struct Command {
    IExplorerCommand iface;
    volatile LONG refs;
    int kind;
} Command;

typedef struct Enum {
    IEnumExplorerCommand iface;
    volatile LONG refs;
    ULONG pos;
} Enum;

static IExplorerCommandVtbl g_commandVtbl;
static IEnumExplorerCommandVtbl g_enumVtbl;

static HRESULT NewCommand(int kind, IExplorerCommand **out) {
    Command *c = (Command *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Command));
    if (!c) return E_OUTOFMEMORY;
    c->iface.lpVtbl = &g_commandVtbl;
    c->refs = 1;
    c->kind = kind;
    InterlockedIncrement(&g_locks);
    *out = &c->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_QueryInterface(IExplorerCommand *self, REFIID riid, void **result) {
    if (!result) return E_POINTER;
    if (SameGuid(riid, &kIidUnknown) || SameGuid(riid, &kIidExplorerCommand)) {
        *result = self;
        IExplorerCommand_AddRef(self);
        return S_OK;
    }
    *result = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Command_AddRef(IExplorerCommand *self) { return (ULONG)InterlockedIncrement(&((Command *)self)->refs); }

static ULONG STDMETHODCALLTYPE Command_Release(IExplorerCommand *self) {
    LONG refs = InterlockedDecrement(&((Command *)self)->refs);
    if (refs == 0) {
        HeapFree(GetProcessHeap(), 0, self);
        InterlockedDecrement(&g_locks);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE Command_GetTitle(IExplorerCommand *self, IShellItemArray *items, LPWSTR *name) {
    (void)items;
    if (!name) return E_POINTER;
    *name = NULL;
    const WCHAR *title;
    switch (((Command *)self)->kind) {
        case KindRoot: title = L"Katzen"; break;
        case KindFeed: title = L"Füttern"; break;
        case KindPlay: title = L"Spielen"; break;
        case KindCall: title = L"Herrufen"; break;
        case KindSleep: title = CatsSleeping() ? L"Aufwecken" : L"Schlafen schicken"; break;
        case KindShow: title = CatsVisible() ? L"Katzen ausblenden" : L"Katzen anzeigen"; break;
        case KindSettings: title = L"Einstellungen …"; break;
        default: return E_FAIL;
    }
    return SHStrDupW(title, name);
}

static HRESULT STDMETHODCALLTYPE Command_GetIcon(IExplorerCommand *self, IShellItemArray *items, LPWSTR *icon) {
    (void)items;
    if (!icon) return E_POINTER;
    *icon = NULL;
    if (((Command *)self)->kind != KindRoot) return E_NOTIMPL;
    WCHAR path[1024];
    if (!PathNextToModule(APP_EXE L",0", path, ARRAYSIZE(path))) return E_FAIL;
    return SHStrDupW(path, icon);
}

static HRESULT STDMETHODCALLTYPE Command_GetToolTip(IExplorerCommand *self, IShellItemArray *items, LPWSTR *tip) {
    (void)self;
    (void)items;
    if (tip) *tip = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE Command_GetCanonicalName(IExplorerCommand *self, GUID *name) {
    if (!name) return E_POINTER;
    *name = kClsid;
    name->Data4[7] = (BYTE)(name->Data4[7] + 1 + ((Command *)self)->kind);   // je Eintrag eindeutig
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetState(IExplorerCommand *self, IShellItemArray *items, BOOL okToBeSlow, EXPCMDSTATE *state) {
    (void)okToBeSlow;
    if (!state) return E_POINTER;
    int kind = ((Command *)self)->kind;
    *state = ECS_ENABLED;
    if (kind == KindRoot) {
        if (!IsDesktop(items)) *state = ECS_HIDDEN;
        return S_OK;
    }
    if ((kind == KindSleep || kind == KindCall) && !CatsVisible()) *state = ECS_DISABLED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_Invoke(IExplorerCommand *self, IShellItemArray *items, IBindCtx *context) {
    (void)items;
    (void)context;
    int kind = ((Command *)self)->kind;
    if (kind == KindRoot) return S_OK;
    return Run(kind);
}

static HRESULT STDMETHODCALLTYPE Command_GetFlags(IExplorerCommand *self, EXPCMDFLAGS *flags) {
    if (!flags) return E_POINTER;
    *flags = ((Command *)self)->kind == KindRoot ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_EnumSubCommands(IExplorerCommand *self, IEnumExplorerCommand **commands) {
    if (!commands) return E_POINTER;
    *commands = NULL;
    if (((Command *)self)->kind != KindRoot) return E_NOTIMPL;
    Enum *e = (Enum *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Enum));
    if (!e) return E_OUTOFMEMORY;
    e->iface.lpVtbl = &g_enumVtbl;
    e->refs = 1;
    e->pos = 0;
    InterlockedIncrement(&g_locks);
    *commands = &e->iface;
    return S_OK;
}

static IExplorerCommandVtbl g_commandVtbl = {
    Command_QueryInterface, Command_AddRef,   Command_Release, Command_GetTitle, Command_GetIcon,         Command_GetToolTip,
    Command_GetCanonicalName, Command_GetState, Command_Invoke, Command_GetFlags, Command_EnumSubCommands,
};

// ---- IEnumExplorerCommand (die sechs Untereinträge) ------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Enum_QueryInterface(IEnumExplorerCommand *self, REFIID riid, void **result) {
    if (!result) return E_POINTER;
    if (SameGuid(riid, &kIidUnknown) || SameGuid(riid, &kIidEnumExplorerCommand)) {
        *result = self;
        IEnumExplorerCommand_AddRef(self);
        return S_OK;
    }
    *result = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Enum_AddRef(IEnumExplorerCommand *self) { return (ULONG)InterlockedIncrement(&((Enum *)self)->refs); }

static ULONG STDMETHODCALLTYPE Enum_Release(IEnumExplorerCommand *self) {
    LONG refs = InterlockedDecrement(&((Enum *)self)->refs);
    if (refs == 0) {
        HeapFree(GetProcessHeap(), 0, self);
        InterlockedDecrement(&g_locks);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE Enum_Next(IEnumExplorerCommand *self, ULONG count, IExplorerCommand **out, ULONG *fetched) {
    Enum *e = (Enum *)self;
    ULONG got = 0;
    if (!out) return E_POINTER;
    while (got < count && e->pos < KindCount) {
        if (FAILED(NewCommand((int)e->pos, &out[got]))) break;
        e->pos++;
        got++;
    }
    if (fetched) *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE Enum_Skip(IEnumExplorerCommand *self, ULONG count) {
    Enum *e = (Enum *)self;
    e->pos = e->pos + count > KindCount ? KindCount : e->pos + count;
    return e->pos < KindCount ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE Enum_Reset(IEnumExplorerCommand *self) {
    ((Enum *)self)->pos = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Enum_Clone(IEnumExplorerCommand *self, IEnumExplorerCommand **out) {
    (void)self;
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static IEnumExplorerCommandVtbl g_enumVtbl = {
    Enum_QueryInterface, Enum_AddRef, Enum_Release, Enum_Next, Enum_Skip, Enum_Reset, Enum_Clone,
};

// ---- IClassFactory ----------------------------------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Factory_QueryInterface(IClassFactory *self, REFIID riid, void **result) {
    if (!result) return E_POINTER;
    if (SameGuid(riid, &kIidUnknown) || SameGuid(riid, &kIidClassFactory)) {
        *result = self;
        IClassFactory_AddRef(self);
        return S_OK;
    }
    *result = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Factory_AddRef(IClassFactory *self) {
    (void)self;
    return (ULONG)InterlockedIncrement(&g_locks);
}

static ULONG STDMETHODCALLTYPE Factory_Release(IClassFactory *self) {
    (void)self;
    return (ULONG)InterlockedDecrement(&g_locks);
}

static HRESULT STDMETHODCALLTYPE Factory_CreateInstance(IClassFactory *self, IUnknown *outer, REFIID riid, void **result) {
    (void)self;
    if (!result) return E_POINTER;
    *result = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    IExplorerCommand *cmd = NULL;
    HRESULT hr = NewCommand(KindRoot, &cmd);
    if (FAILED(hr)) return hr;
    hr = Command_QueryInterface(cmd, riid, result);
    Command_Release(cmd);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Factory_LockServer(IClassFactory *self, BOOL lock) {
    (void)self;
    if (lock) InterlockedIncrement(&g_locks);
    else InterlockedDecrement(&g_locks);
    return S_OK;
}

static IClassFactoryVtbl g_factoryVtbl = {
    Factory_QueryInterface, Factory_AddRef, Factory_Release, Factory_CreateInstance, Factory_LockServer,
};
static IClassFactory g_factory = {&g_factoryVtbl};

// ---- DLL-Exporte -------------------------------------------------------------------------------------------------

HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void **result) {
    if (!result) return E_POINTER;
    *result = NULL;
    if (!SameGuid(clsid, &kClsid)) return CLASS_E_CLASSNOTAVAILABLE;
    return Factory_QueryInterface(&g_factory, riid, result);
}

HRESULT __stdcall DllCanUnloadNow(void) { return g_locks == 0 ? S_OK : S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
