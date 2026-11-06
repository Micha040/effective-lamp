// DeskCats – App-Paket (unsigniert, "sparse") für den Eintrag im Windows-11-Kontextmenü.
// Gepackt wird mit der Paket-API von Windows (AppxPackaging, Teil von Windows 10/11): sie prüft das Manifest und
// schreibt Blockmap und Inhaltstypen wie MakeAppx – kein Windows SDK nötig.
#include "pack.h"

#include <objidl.h>
#include <shlwapi.h>
#include <urlmon.h>

namespace app {

namespace {

// IAppxFactory / IAppxPackageWriter (nicht in MinGW): nur die benötigten Methoden über die vtable
const CLSID kClsidAppxFactory = {0x5842a140, 0xff9f, 0x4166, {0x8f, 0x5c, 0x62, 0xf5, 0xb7, 0xb0, 0xc7, 0x81}};
const IID kIidAppxFactory = {0xbeb94909, 0xe451, 0x438b, {0xb5, 0xa7, 0xd7, 0x9e, 0x76, 0x7b, 0x75, 0xd8}};

struct AppxPackageSettings {
    BOOL forceZip32;
    IUri* hashMethod;
};
typedef HRESULT(STDMETHODCALLTYPE* CreatePackageWriterFn)(void* self, IStream* output, AppxPackageSettings* settings, void** writer);
typedef HRESULT(STDMETHODCALLTYPE* AddPayloadFileFn)(void* self, LPCWSTR fileName, LPCWSTR contentType, int compression, IStream* input);
typedef HRESULT(STDMETHODCALLTYPE* CloseFn)(void* self, IStream* manifest);
typedef ULONG(STDMETHODCALLTYPE* ReleaseFn)(void* self);

void* slot(void* obj, int index) { return (*reinterpret_cast<void***>(obj))[index]; }
void release(void* obj) {
    if (obj) reinterpret_cast<ReleaseFn>(slot(obj, 2))(obj);
}

std::string utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

bool resourceBytes(int id, const void** data, DWORD* size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return false;
    *data = LockResource(g);
    *size = SizeofResource(nullptr, r);
    return *data && *size;
}

}  // namespace

std::string buildManifest() {
    std::string clsid = utf8(kMenuClsid);
    std::string m;
    m += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    m += "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\" "
         "xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\" "
         "xmlns:uap10=\"http://schemas.microsoft.com/appx/manifest/uap/windows10/10\" "
         "xmlns:desktop4=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10/4\" "
         "xmlns:desktop5=\"http://schemas.microsoft.com/appx/manifest/desktop/windows10/5\" "
         "xmlns:com=\"http://schemas.microsoft.com/appx/manifest/com/windows10\" "
         "xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\" "
         "IgnorableNamespaces=\"uap uap10 desktop4 desktop5 com rescap\">\r\n";
    m += "  <Identity Name=\"" + utf8(kPackageName) + "\" Publisher=\"" + utf8(kPackagePublisher) + "\" Version=\"" + utf8(kPackageVersion) +
         "\" ProcessorArchitecture=\"x64\" />\r\n";
    m += "  <Properties>\r\n"
         "    <DisplayName>DeskCats</DisplayName>\r\n"
         "    <PublisherDisplayName>DeskCats</PublisherDisplayName>\r\n"
         "    <Logo>Assets\\Logo.png</Logo>\r\n"
         "    <uap10:AllowExternalContent>true</uap10:AllowExternalContent>\r\n"
         "  </Properties>\r\n"
         "  <Resources>\r\n"
         "    <Resource Language=\"de-de\" />\r\n"
         "  </Resources>\r\n"
         "  <Dependencies>\r\n"
         "    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.22000.0\" MaxVersionTested=\"10.0.26200.0\" />\r\n"
         "  </Dependencies>\r\n"
         "  <Capabilities>\r\n"
         "    <rescap:Capability Name=\"runFullTrust\" />\r\n"
         "    <rescap:Capability Name=\"unvirtualizedResources\" />\r\n"
         "  </Capabilities>\r\n"
         "  <Applications>\r\n"
         "    <Application Id=\"DeskCats\" Executable=\"DeskCats.exe\" uap10:TrustLevel=\"mediumIL\" uap10:RuntimeBehavior=\"win32App\">\r\n"
         "      <uap:VisualElements AppListEntry=\"none\" DisplayName=\"DeskCats\" Description=\"Katzen auf dem Desktop\" "
         "BackgroundColor=\"transparent\" Square150x150Logo=\"Assets\\Logo.png\" Square44x44Logo=\"Assets\\Logo.png\" />\r\n"
         "      <Extensions>\r\n"
         "        <desktop4:Extension Category=\"windows.fileExplorerContextMenus\">\r\n"
         "          <desktop4:FileExplorerContextMenus>\r\n"
         "            <desktop5:ItemType Type=\"Directory\\Background\">\r\n"
         "              <desktop5:Verb Id=\"DeskCats\" Clsid=\"" + clsid + "\" />\r\n"
         "            </desktop5:ItemType>\r\n"
         "          </desktop4:FileExplorerContextMenus>\r\n"
         "        </desktop4:Extension>\r\n"
         "        <com:Extension Category=\"windows.comServer\">\r\n"
         "          <com:ComServer>\r\n"
         "            <com:SurrogateServer DisplayName=\"DeskCats Kontextmenü\">\r\n"
         "              <com:Class Id=\"" + clsid + "\" Path=\"DeskCatsMenu.dll\" ThreadingModel=\"STA\" />\r\n"
         "            </com:SurrogateServer>\r\n"
         "          </com:ComServer>\r\n"
         "        </com:Extension>\r\n"
         "      </Extensions>\r\n"
         "    </Application>\r\n"
         "  </Applications>\r\n"
         "</Package>\r\n";
    return m;
}

int packMenu(const std::wstring& outDir) {
    std::wstring out = outDir;
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out += L'\\';
    out += L"DeskCatsMenu.msix";
    DeleteFileW(out.c_str());

    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    void* factory = nullptr;
    void* writer = nullptr;
    IStream* output = nullptr;
    IStream* logo = nullptr;
    IStream* manifest = nullptr;
    IUri* hash = nullptr;
    HRESULT hr = S_OK;
    const char* step = "";
    do {
        step = "CoCreateInstance(AppxFactory)";
        hr = CoCreateInstance(kClsidAppxFactory, nullptr, CLSCTX_INPROC_SERVER, kIidAppxFactory, &factory);
        if (FAILED(hr)) break;
        step = "Ausgabedatei";
        hr = SHCreateStreamOnFileEx(out.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &output);
        if (FAILED(hr)) break;
        step = "CreateUri";
        hr = CreateUri(L"http://www.w3.org/2001/04/xmlenc#sha256", 0, 0, &hash);
        if (FAILED(hr)) break;
        AppxPackageSettings settings{TRUE, hash};
        step = "CreatePackageWriter";
        hr = reinterpret_cast<CreatePackageWriterFn>(slot(factory, 3))(factory, output, &settings, &writer);
        if (FAILED(hr)) break;

        const void* png = nullptr;
        DWORD pngSize = 0;
        step = "Logo-Ressource";
        if (!resourceBytes(2, &png, &pngSize)) {
            hr = E_FAIL;
            break;
        }
        logo = SHCreateMemStream(reinterpret_cast<const BYTE*>(png), pngSize);
        if (!logo) {
            hr = E_OUTOFMEMORY;
            break;
        }
        step = "AddPayloadFile(Logo)";
        hr = reinterpret_cast<AddPayloadFileFn>(slot(writer, 3))(writer, L"Assets\\Logo.png", L"image/png", 1 /*NORMAL*/, logo);
        if (FAILED(hr)) break;

        std::string xml = buildManifest();
        manifest = SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), (UINT)xml.size());
        if (!manifest) {
            hr = E_OUTOFMEMORY;
            break;
        }
        step = "Close (Manifest-Prüfung)";
        hr = reinterpret_cast<CloseFn>(slot(writer, 4))(writer, manifest);
    } while (false);

    if (manifest) manifest->Release();
    if (logo) logo->Release();
    release(writer);
    if (hash) hash->Release();
    if (output) output->Release();
    release(factory);
    if (SUCCEEDED(init)) CoUninitialize();

    if (FAILED(hr)) {
        logLine(L"Paket bauen fehlgeschlagen bei %hs: %ls", step, hrText(hr).c_str());
        DeleteFileW(out.c_str());
        return 1;
    }
    logLine(L"Kontextmenü-Paket geschrieben: %ls", out.c_str());
    return 0;
}

}  // namespace app
