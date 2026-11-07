// winsock2 vor windows.h (über die anderen Kopfdateien)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "mobile_host.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>

#include "../api/http.h"
#include "../api/mobile.h"
#include "../third_party/qrcodegen.hpp"
#include "app.h"
#include "common.h"
#include "recorder.h"

namespace fdw {

namespace {

// ---- Antworten im UI-Thread ---------------------------------------------------------------------------------------
// Der Server-Thread legt einen Auftrag an, schickt ihn per PostMessage ans Fenster und wartet. Läuft er zu lange nicht
// an, wird er abgesagt (FragDesk hängt oder schließt gerade). Hat er angefangen, wartet der Server-Thread auf das Ende –
// der Auftrag greift auf dessen Anfrage und Antwort zu.
struct Job {
    const std::function<void(fdapi::Source&)>* fn = nullptr;
    std::mutex mu;
    std::condition_variable cv;
    enum State { Pending, Running, Done, Cancelled } state = Pending;
};

// Alles, was die Threads des Servers brauchen. Lebt, solange noch eine Verbindung läuft (nie Zugriff auf App).
struct Shared {
    std::atomic<HWND> hwnd{nullptr};
    fdapi::Auth auth;
    fdapi::Host host;
    std::atomic<ULONGLONG> cs2At{0};
    std::atomic<bool> cs2{false};
};

class AppSource : public fdapi::Source {
public:
    explicit AppSource(App& a) : app(a) {}
    std::string me() override { return app.me; }
    std::string meName() override { return narrow(app.meName); }
    bool loaded() override { return app.lib.loaded(); }
    std::vector<const fd::Match*> matches() override { return app.matchPtrs(); }
    int generation() override { return app.lib.generation; }
    bool detail(const std::string& checksum, fd::Match& out) override {
        std::wstring err;
        return app.lib.loadDetail(checksum, out, err);
    }
    std::vector<fdapi::ClipInfo> clips() override {
        std::vector<fdapi::ClipInfo> v;
        for (const Clip& c : app.clips) {
            fdapi::ClipInfo ci;
            ci.path = narrow(c.path);
            ci.id = fdapi::clipId(ci.path);
            ci.title = narrow(c.title);
            ci.match = c.match;
            ci.highlight = c.highlight;
            ci.map = c.map;
            ci.created = c.created;
            ci.size = c.size;
            ci.seconds = c.seconds;
            v.push_back(std::move(ci));
        }
        return v;
    }
    int newDemos() override {
        int n = 0;
        for (const auto& d : app.lib.demos)
            if (d.state == DemoFile::State::New || d.state == DemoFile::State::Failed) ++n;
        return n;
    }

private:
    App& app;
};

// ---- App-Dateien aus den Ressourcen (res/web.rc) --------------------------------------------------------------------
struct WebAsset {
    const char* path;
    int id;
};
const WebAsset kWebAssets[] = {
#include "web_assets.inc"
};

bool loadAsset(const std::string& path, std::string& body) {
    int id = 0;
    for (const auto& a : kWebAssets)
        if (path == a.path) id = a.id;
    if (!id) return false;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    const void* p = g ? LockResource(g) : nullptr;
    if (!p) return false;
    body.assign(static_cast<const char*>(p), SizeofResource(nullptr, r));
    return true;
}

fdapi::FileReader openFile(const std::string& path, int64_t& size) {
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li)) {
        CloseHandle(h);
        return nullptr;
    }
    size = li.QuadPart;
    std::shared_ptr<void> file(h, CloseHandle);
    return [file](int64_t off, char* buf, int64_t len) -> int64_t {
        OVERLAPPED ov = {};   // Lesen an fester Stelle: mehrere Anfragen auf dieselbe Datei stören sich nicht
        ov.Offset = DWORD(uint64_t(off) & 0xFFFFFFFFu);
        ov.OffsetHigh = DWORD(uint64_t(off) >> 32);
        DWORD got = 0;
        if (!ReadFile(file.get(), buf, DWORD(std::min<int64_t>(len, 1 << 24)), &got, &ov)) return -1;
        return got;
    };
}

// ---- Vorschaubild: ein Bild nach 1 s aus dem Video (Media Foundation), 480 px breit als JPEG (WIC) ------------------
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

bool encodeJpeg(std::vector<BYTE>& px, UINT w, UINT h, std::string& jpeg) {
    Com<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&f.p))))
        return false;
    Com<IWICBitmap> bmp;
    if (FAILED(f->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGR, w * 4, UINT(px.size()), px.data(), &bmp))) return false;
    UINT tw = std::min<UINT>(480, w), th = std::max<UINT>(1, UINT(uint64_t(h) * tw / w));
    Com<IWICBitmapScaler> sc;
    Com<IWICFormatConverter> cv;
    Com<IStream> st;
    Com<IWICBitmapEncoder> enc;
    Com<IWICBitmapFrameEncode> fr;
    Com<IPropertyBag2> props;
    if (FAILED(f->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(bmp.p, tw, th, WICBitmapInterpolationModeFant))) return false;
    if (FAILED(f->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(sc.p, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return false;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) || FAILED(enc->Initialize(st.p, WICBitmapEncoderNoCache))) return false;
    if (FAILED(enc->CreateNewFrame(&fr, &props))) return false;
    PROPBAG2 opt = {};
    opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R4;
    v.fltVal = 0.82f;
    if (props) props->Write(1, &opt, &v);
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    if (FAILED(fr->Initialize(props.p)) || FAILED(fr->SetSize(tw, th)) || FAILED(fr->SetPixelFormat(&pf)) || FAILED(fr->WriteSource(cv.p, nullptr)) ||
        FAILED(fr->Commit()) || FAILED(enc->Commit()))
        return false;
    STATSTG ss = {};
    HGLOBAL hg = nullptr;
    if (FAILED(st->Stat(&ss, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(st.p, &hg))) return false;
    const void* data = GlobalLock(hg);
    if (!data) return false;
    jpeg.assign(static_cast<const char*>(data), size_t(ss.cbSize.QuadPart));
    GlobalUnlock(hg);
    return !jpeg.empty();
}

bool grabFrame(const std::wstring& path, std::string& jpeg) {
    Com<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return false;
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // liefert RGB32 statt NV12
    Com<IMFSourceReader> rd;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attr.p, &rd))) return false;
    const DWORD vs = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    rd->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    rd->SetStreamSelection(vs, TRUE);
    Com<IMFMediaType> want;
    if (FAILED(MFCreateMediaType(&want))) return false;
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(rd->SetCurrentMediaType(vs, nullptr, want.p))) return false;
    PROPVARIANT pos;
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = 10000000;   // 1 s – davor ist oft noch das Ladebild
    rd->SetCurrentPosition(GUID_NULL, pos);
    Com<IMFSample> sample;
    for (int i = 0; i < 60 && !sample; ++i) {
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        if (FAILED(rd->ReadSample(vs, 0, &idx, &flags, &ts, &sample))) return false;
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
    }
    if (!sample) return false;
    Com<IMFMediaType> cur;
    if (FAILED(rd->GetCurrentMediaType(vs, &cur))) return false;
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &w, &h);
    LONG stride = LONG(MFGetAttributeUINT32(cur.p, MF_MT_DEFAULT_STRIDE, w * 4));
    Com<IMFMediaBuffer> buf;
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;
    BYTE* data = nullptr;
    DWORD len = 0;
    if (FAILED(buf->Lock(&data, nullptr, &len))) return false;
    DWORD absStride = DWORD(stride < 0 ? -stride : stride);
    bool fits = absStride >= w * 4 && uint64_t(absStride) * h <= len;
    std::vector<BYTE> px(fits ? size_t(w) * h * 4 : 0);
    if (fits)
        for (UINT32 y = 0; y < h; ++y) {
            const BYTE* src = data + size_t(stride < 0 ? h - 1 - y : y) * absStride;   // negativ: Zeilen von unten nach oben
            memcpy(&px[size_t(y) * w * 4], src, size_t(w) * 4);
        }
    buf->Unlock();
    return fits && encodeJpeg(px, w, h, jpeg);
}

}  // namespace

bool videoThumbnail(const std::wstring& video, const std::string& clipId, std::string& jpeg, bool cachedOnly) {
    std::wstring dir = joinPath(dataDir(), L"thumbs"), cache = joinPath(dir, widen(clipId) + L".jpg");
    auto cached = [&] { return fileExists(cache) && fileTime(cache) >= fileTime(video) && readFile(cache, jpeg, 4u << 20) && !jpeg.empty(); };
    if (cachedOnly) return cached();   // die Datei entsteht atomar: Lesen geht ohne Sperre
    // Immer nur ein Vorschaubild gleichzeitig (Handy-App und Abgleich mit dem Server). Hängt Media Foundation an einem
    // Video fest, warten die anderen nicht ewig.
    static std::timed_mutex mu;
    std::unique_lock<std::timed_mutex> lk(mu, std::chrono::seconds(30));
    if (!lk.owns_lock()) return false;
    if (cached()) return true;
    if (!fileExists(video)) return false;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        ok = grabFrame(video, jpeg);
        MFShutdown();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    if (ok) {
        createDirs(dir);
        writeFileAtomic(cache, jpeg);
    } else logLine(L"Handy-App: kein Vorschaubild für %ls", video.c_str());
    return ok;
}

// ---- MobileHost ---------------------------------------------------------------------------------------------------
struct MobileHost::Impl {
    App& app;
    std::shared_ptr<Shared> sh = std::make_shared<Shared>();
    fdapi::Server server;
    std::wstring error;
    size_t knownDevices = 0;
    mutable std::vector<MobileAddress> addrCache;
    mutable ULONGLONG addrAt = 0;
    explicit Impl(App& a) : app(a) {}
};

MobileHost::MobileHost(App& app) : d_(std::make_unique<Impl>(app)) {
    std::shared_ptr<Shared> sh = d_->sh;
    Shared* raw = sh.get();   // die Lambdas gehören zu sh selbst – ein shared_ptr darin wäre ein Kreis
    fdapi::Host& host = sh->host;
    host.version = kVersionA;
    host.kind = "pc";
    host.auth = &sh->auth;
    host.withSource = [raw](const std::function<void(fdapi::Source&)>& fn) -> bool {
        HWND hwnd = raw->hwnd.load();
        if (!hwnd) return false;
        auto job = std::make_shared<Job>();
        job->fn = &fn;
        auto* msg = new std::shared_ptr<Job>(job);
        if (!PostMessageW(hwnd, WM_APP_MOBILE, 0, reinterpret_cast<LPARAM>(msg))) {
            delete msg;
            return false;
        }
        std::unique_lock<std::mutex> lk(job->mu);
        if (!job->cv.wait_for(lk, std::chrono::seconds(10), [&] { return job->state == Job::Done; })) {
            if (job->state == Job::Pending) {
                job->state = Job::Cancelled;
                return false;
            }
            job->cv.wait(lk, [&] { return job->state == Job::Done; });
        }
        return true;
    };
    host.asset = loadAsset;
    host.openFile = openFile;
    host.thumbnail = [](const fdapi::ClipInfo& clip, std::string& jpeg) { return videoThumbnail(widen(clip.path), clip.id, jpeg); };
    host.mediaBlocked = [raw]() -> std::string {
        ULONGLONG now = GetTickCount64();
        if (now - raw->cs2At.load() > 5000) {   // höchstens alle 5 s nachsehen
            raw->cs2 = isProcessRunning(L"cs2.exe");
            raw->cs2At = now;
        }
        return raw->cs2 ? "Auf deinem PC läuft gerade CS2. Damit dein Spiel nicht ruckelt, gibt es Videos erst wieder, wenn CS2 zu ist." : std::string();
    };
    sh->auth.onChange = [raw] {
        HWND hwnd = raw->hwnd.load();
        if (hwnd) PostMessageW(hwnd, WM_APP_MOBILE, 1, 0);
    };
}

MobileHost::~MobileHost() { stop(); }

void MobileHost::apply() {
    Impl& d = *d_;
    const MobileSettings& s = d.app.settings.mobile;
    d.addrAt = 0;   // Adressen neu bestimmen (z. B. eigene Adresse geändert)
    d.sh->hwnd = d.app.hwnd;
    std::vector<fdapi::Device> devs;
    for (const auto& md : s.devices) devs.push_back({md.token, md.name, md.created, md.lastSeen});
    d.sh->auth.setDevices(devs);
    d.knownDevices = devs.size();
    if (!s.enabled || s.server || d.app.renderMode || !d.app.hwnd) {   // im Modus „Eigener Server“ lauscht FragDesk nicht
        stop();
        return;
    }
    if (d.server.running() && d.server.port() == s.port) return;
    d.server.stop();
    std::string err;
    std::shared_ptr<Shared> sh = d.sh;
    if (d.server.start("0.0.0.0", s.port, [sh](const fdapi::Request& rq, fdapi::Response& rs) { fdapi::handle(sh->host, rq, rs); }, err)) {
        d.error.clear();
        logLine(L"Handy-App: läuft auf Port %d", s.port);
    } else {
        d.error = widen(err);
        logLine(L"Handy-App: %ls", d.error.c_str());
    }
}

void MobileHost::stop() {
    d_->sh->hwnd = nullptr;   // neue Anfragen gehen sofort nicht mehr an das Fenster
    d_->sh->auth.stopPairing();
    if (d_->server.running()) logLine(L"Handy-App: angehalten");
    d_->server.stop();
}

bool MobileHost::running() const { return d_->server.running(); }
int MobileHost::port() const { return d_->server.running() ? d_->server.port() : d_->app.settings.mobile.port; }
const std::wstring& MobileHost::error() const { return d_->error; }

std::vector<MobileAddress> MobileHost::addresses() const {
    Impl& d = *d_;
    ULONGLONG now = GetTickCount64();
    if (d.addrAt && now - d.addrAt < 5000) return d.addrCache;   // beim Zeichnen nicht jedes Mal die Netzwerke abfragen
    std::vector<MobileAddress> out, lan;
    const MobileSettings& s = d.app.settings.mobile;
    if (!s.address.empty()) out.push_back({widen(s.address), L"Eigene Adresse"});
    ULONG size = 32 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    std::wstring portText = std::to_wstring(this->port());
    for (auto* a = r == NO_ERROR ? reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()) : nullptr; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        std::wstring name = lowerCase(std::wstring(a->FriendlyName ? a->FriendlyName : L"") + L" " + (a->Description ? a->Description : L""));
        bool ts = name.find(L"tailscale") != std::wstring::npos;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            wchar_t txt[24];
            swprintf(txt, 24, L"%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
            std::wstring url = L"http://" + std::wstring(txt) + L":" + portText + L"/";
            bool cgnat = (ip & 0xFFC00000u) == 0x64400000u;   // 100.64.0.0/10 (Tailscale)
            bool priv = (ip >> 24) == 10 || (ip & 0xFFF00000u) == 0xAC100000u || (ip & 0xFFFF0000u) == 0xC0A80000u;
            if (ts && cgnat) out.push_back({url, L"Tailscale"});
            else if (priv && a->FirstGatewayAddress) lan.push_back({url, L"Heimnetz"});   // ohne Gateway: virtuelle Netze (Hyper-V …)
        }
    }
    out.insert(out.end(), lan.begin(), lan.end());
    d.addrCache = out;
    d.addrAt = now;
    return out;
}

bool MobileHost::tailscale() const {
    for (const auto& a : addresses())
        if (a.label == L"Tailscale") return true;
    return false;
}

std::string MobileHost::startPairing() {
    std::string code = d_->sh->auth.startPairing(10);
    if (d_->app.hwnd) SetTimer(d_->app.hwnd, kMobileTimer, 1000, nullptr);
    return code;
}

void MobileHost::stopPairing() { d_->sh->auth.stopPairing(); }
std::string MobileHost::pairingCode() const { return d_->sh->auth.pairingCode(); }
int64_t MobileHost::pairingUntil() const { return d_->sh->auth.pairingUntil(); }

std::vector<MobileDevice> MobileHost::devices() const {
    std::vector<MobileDevice> v;
    for (const auto& d : d_->sh->auth.devices()) v.push_back({d.token, d.name, d.created, d.lastSeen});
    return v;
}

void MobileHost::removeDevice(const std::string& token) { d_->sh->auth.remove(token); }   // meldet sich über onChange

LRESULT MobileHost::onMessage(WPARAM wp, LPARAM lp) {
    Impl& d = *d_;
    if (wp == 0) {   // Anfrage beantworten
        std::unique_ptr<std::shared_ptr<Job>> msg(reinterpret_cast<std::shared_ptr<Job>*>(lp));
        if (!msg) return 0;
        std::shared_ptr<Job> job = *msg;
        {
            std::lock_guard<std::mutex> lk(job->mu);
            if (job->state == Job::Cancelled) return 0;
            job->state = Job::Running;
        }
        try {
            AppSource src(d.app);
            (*job->fn)(src);
        } catch (...) {
            logLine(L"Handy-App: Fehler beim Beantworten einer Anfrage");
        }
        {
            std::lock_guard<std::mutex> lk(job->mu);
            job->state = Job::Done;
        }
        job->cv.notify_all();
        return 0;
    }
    // Geräteliste geändert: speichern; neues Gerät melden
    std::vector<MobileDevice> devs = devices();
    if (devs.size() > d.knownDevices && !devs.empty()) d.app.notify(L"Handy verbunden: " + widen(devs.back().name));
    d.knownDevices = devs.size();
    d.app.settings.mobile.devices = devs;
    d.app.saveSettings();
    d.app.invalidate();
    return 0;
}

std::vector<std::vector<bool>> qrModules(const std::string& text) {
    std::vector<std::vector<bool>> rows;
    try {
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int n = qr.getSize();
        rows.assign(size_t(n), std::vector<bool>(size_t(n)));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) rows[size_t(y)][size_t(x)] = qr.getModule(x, y);
    } catch (...) {
        rows.clear();
    }
    return rows;
}

}  // namespace fdw
