// Test der Kontextmenü-DLL (läuft unter Windows und Wine): COM-Objekt laden, Einträge/Zustände auflisten,
// optional einen Eintrag ausführen. Aufruf: menu_test.exe <Pfad zur DLL> [invoke-index]
#include <windows.h>
#include <shobjidl.h>

#include <cstdio>
#include <cwchar>

static const CLSID kClsid = {0x9d533fe8, 0xf14f, 0x473d, {0x89, 0x63, 0x92, 0x30, 0x41, 0xd1, 0xd1, 0xef}};

typedef HRESULT(__stdcall* GetClassObjectFn)(REFCLSID, REFIID, void**);

static void show(IExplorerCommand* c, const char* indent) {
    LPWSTR title = nullptr, icon = nullptr;
    EXPCMDSTATE st = 0;
    EXPCMDFLAGS fl = 0;
    HRESULT ht = c->GetTitle(nullptr, &title);
    HRESULT hi = c->GetIcon(nullptr, &icon);
    c->GetState(nullptr, TRUE, &st);
    c->GetFlags(&fl);
    printf("%s\"%ls\" state=%lu flags=%lu icon=%ls (%08lx/%08lx)\n", indent, title ? title : L"?", (unsigned long)st, (unsigned long)fl,
           icon ? icon : L"-", (unsigned long)ht, (unsigned long)hi);
    if (title) CoTaskMemFree(title);
    if (icon) CoTaskMemFree(icon);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 9;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        printf("FEHLER: DLL nicht ladbar (%lu)\n", GetLastError());
        return 1;
    }
    auto get = (GetClassObjectFn)(void*)GetProcAddress(dll, "DllGetClassObject");
    auto canUnload = (HRESULT(__stdcall*)())(void*)GetProcAddress(dll, "DllCanUnloadNow");
    if (!get || !canUnload) {
        printf("FEHLER: Exporte fehlen\n");
        return 2;
    }
    IClassFactory* factory = nullptr;
    HRESULT hr = get(kClsid, IID_IClassFactory, (void**)&factory);
    printf("DllGetClassObject: %08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 3;
    IUnknown* dummy = nullptr;
    printf("falsche CLSID abgelehnt: %s\n", get(IID_IUnknown, IID_IClassFactory, (void**)&dummy) == CLASS_E_CLASSNOTAVAILABLE ? "ja" : "NEIN");
    IExplorerCommand* root = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IExplorerCommand, (void**)&root);
    printf("CreateInstance: %08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 4;
    show(root, "");
    IEnumExplorerCommand* e = nullptr;
    hr = root->EnumSubCommands(&e);
    printf("EnumSubCommands: %08lx\n", (unsigned long)hr);
    int invoke = argc > 2 ? _wtoi(argv[2]) : -1;
    int count = 0;
    if (SUCCEEDED(hr) && e) {
        IExplorerCommand* sub[3];
        ULONG got = 0;
        // in Zweiergruppen abholen (prüft auch Teil-Ergebnisse)
        while (e->Next(2, sub, &got) == S_OK || got > 0) {
            for (ULONG i = 0; i < got; ++i) {
                show(sub[i], "  ");
                if (count == invoke) printf("  -> Invoke: %08lx\n", (unsigned long)sub[i]->Invoke(nullptr, nullptr));
                sub[i]->Release();
                ++count;
            }
            if (got < 2) break;
        }
        e->Release();
    }
    root->Release();
    factory->Release();
    printf("Untereinträge: %d, DllCanUnloadNow: %s\n", count, canUnload() == S_OK ? "ja" : "NEIN (Leck)");
    CoUninitialize();
    return count == 6 ? 0 : 5;
}
