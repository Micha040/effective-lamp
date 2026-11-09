// FragDesk – Aufnahme und MP4 (siehe capture.h)
#include "capture.h"

// Reihenfolge wichtig: Media Foundation (strmif.h) vor audioclient.h (ksmedia.h), sonst doppelte Typen in MinGW
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"

namespace fdw {

namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring hrText(const wchar_t* what, HRESULT hr) {
    wchar_t b[160];
    swprintf(b, 160, L"%ls (Fehler 0x%08lX)", what, static_cast<unsigned long>(hr));
    return b;
}

}  // namespace

// ---- MP4 ------------------------------------------------------------------------------------------------------
Mp4Writer::~Mp4Writer() {
    if (writer_ || mfStarted_) close();
}

static HRESULT setupWriter(IMFSinkWriter* w, int width, int height, int fps, int bitrate, bool audio, DWORD& video, DWORD& audioIdx,
                           std::wstring& err) {
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(bitrate));
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, UINT32(width), UINT32(height));
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, UINT32(fps), 1);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    HRESULT hr = w->AddStream(out, &video);
    release(out);
    if (FAILED(hr)) {
        err = hrText(L"H.264-Encoder nicht verfügbar (Windows N: „Media Feature Pack“ installieren)", hr);
        return hr;
    }
    IMFMediaType* in = nullptr;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(width * 4));   // positiv = Bild beginnt oben
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, UINT32(width), UINT32(height));
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, UINT32(fps), 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = w->SetInputMediaType(video, in, nullptr);
    release(in);
    if (FAILED(hr)) {
        err = hrText(L"Videoformat wird nicht unterstützt", hr);
        return hr;
    }
    if (!audio) return S_OK;
    IMFMediaType* aout = nullptr;
    MFCreateMediaType(&aout);
    aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
    hr = w->AddStream(aout, &audioIdx);
    release(aout);
    if (FAILED(hr)) return hr;
    IMFMediaType* ain = nullptr;
    MFCreateMediaType(&ain);
    ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
    hr = w->SetInputMediaType(audioIdx, ain, nullptr);
    release(ain);
    return hr;
}

// Sink Writer ohne Finalize aufgeben: erst die Senke schließen – sonst bleibt die Datei offen, und der nächste Versuch
// (etwa der Windows-Encoder nach dem der Grafikkarte) scheitert mit „Datei wird verwendet“ (0x80070020)
static void discardWriter(IMFSinkWriter*& w) {
    if (!w) return;
    IMFMediaSink* sink = nullptr;
    if (SUCCEEDED(w->GetServiceForStream(MF_SINK_WRITER_MEDIASINK, GUID_NULL, __uuidof(IMFMediaSink), reinterpret_cast<void**>(&sink))) && sink) {
        sink->Shutdown();
        sink->Release();
    }
    release(w);
}

bool Mp4Writer::open(const std::wstring& path, int width, int height, int fps, int bitrate, bool withAudio, std::wstring& err) {
    std::lock_guard<std::mutex> lock(mu_);
    w_ = width & ~1;
    h_ = height & ~1;
    fps_ = fps;
#ifdef FRAGDESK_TESTHOOKS
    // Nur im Test-Build für Wine (kann kein H.264): statt des Videos je Bild eine Zeile „Zeit Anteil-Ladebildschirm“ –
    // die CS2-Attrappe malt ihren Ladebildschirm lila (170, 30, 160)
    wchar_t flag[8];
    if (GetEnvironmentVariableW(L"FRAGDESK_TEST_FRAMES", flag, 8)) {
        test_ = _wfopen((path + L".frames.txt").c_str(), L"w");
        testShot_ = path + L".first.ppm";
        if (FILE* f = _wfopen(path.c_str(), L"wb")) fclose(f);
        hasAudio_ = false;
        started_ = test_ != nullptr;
        if (!started_) err = L"Testdatei kann nicht angelegt werden";
        return started_;
    }
#endif
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        err = hrText(L"Media Foundation startet nicht (Windows N: „Media Feature Pack“ installieren)", hr);
        return false;
    }
    mfStarted_ = true;
    // Versuche: Hardware-Encoder der Grafikkarte, dann der von Windows (z. B. wenn OBS oder ShadowPlay den
    // Hardware-Encoder belegen) – jeweils erst mit, dann ohne Ton
    struct Try {
        bool hw, audio;
    };
    std::vector<Try> tries = {{true, withAudio}, {false, withAudio}};
    if (withAudio) tries.insert(tries.end(), {{true, false}, {false, false}});
    std::wstring lastErr;
    for (const Try& t : tries) {
        hasAudio_ = t.audio;
        IMFAttributes* attr = nullptr;
        MFCreateAttributes(&attr, 2);
        if (attr) attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, t.hw ? TRUE : FALSE);
        DeleteFileW(path.c_str());
        hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attr, &writer_);
        release(attr);
        if (FAILED(hr)) {
            err = hrText(L"Videodatei kann nicht angelegt werden", hr);
            return false;
        }
        std::wstring e;
        hr = setupWriter(writer_, w_, h_, fps_, bitrate, hasAudio_, video_, audio_, e);
        if (SUCCEEDED(hr)) hr = writer_->BeginWriting();
        if (SUCCEEDED(hr)) {
            started_ = true;
            if (!t.hw || t.audio != withAudio) logLine(L"MP4: %ls-Encoder%ls", t.hw ? L"Hardware" : L"Windows", t.audio ? L"" : L", ohne Ton");
            return true;
        }
        discardWriter(writer_);
        DeleteFileW(path.c_str());   // keine leere Datei zurücklassen
        lastErr = e.empty() ? hrText(L"Videoaufnahme kann nicht beginnen", hr) : e;
        logLine(L"MP4-Versuch (Hardware %d, Ton %d) gescheitert: %ls", int(t.hw), int(t.audio), lastErr.c_str());
    }
    err = lastErr;
    return false;
}

bool Mp4Writer::writeFrame(const uint8_t* bgra, int stride, int64_t t) {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef FRAGDESK_TESTHOOKS
    if (test_) {
        int64_t n = 0, lila = 0, sum[3] = {0, 0, 0};
        for (int y = 0; y < h_; y += 4)
            for (int x = 0; x < w_; x += 4, ++n) {
                const uint8_t* p = bgra + size_t(y) * stride + size_t(x) * 4;
                if (std::abs(p[2] - 170) < 24 && std::abs(p[1] - 30) < 24 && std::abs(p[0] - 160) < 24) ++lila;
                for (int c = 0; c < 3; ++c) sum[c] += p[2 - c];
            }
        if (n == 0) n = 1;
        fprintf(test_, "%lld %.3f rgb(%lld,%lld,%lld)\n", static_cast<long long>(t), double(lila) / double(n), static_cast<long long>(sum[0] / n),
                static_cast<long long>(sum[1] / n), static_cast<long long>(sum[2] / n));
        if (!testShot_.empty()) {   // erstes Bild als PPM daneben (zum Ansehen)
            if (FILE* f = _wfopen(testShot_.c_str(), L"wb")) {
                fprintf(f, "P6 %d %d 255\n", w_, h_);
                for (int y = 0; y < h_; ++y)
                    for (int x = 0; x < w_; ++x) {
                        const uint8_t* p = bgra + size_t(y) * stride + size_t(x) * 4;
                        uint8_t rgb[3] = {p[2], p[1], p[0]};
                        fwrite(rgb, 1, 3, f);
                    }
                fclose(f);
            }
            testShot_.clear();
        }
        return true;
    }
#endif
    if (!writer_ || !started_) return false;
    DWORD size = DWORD(w_ * 4 * h_);
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(size, &buf))) return false;
    BYTE* dst = nullptr;
    buf->Lock(&dst, nullptr, nullptr);
    for (int y = 0; y < h_; ++y) memcpy(dst + size_t(y) * w_ * 4, bgra + size_t(y) * stride, size_t(w_) * 4);
    buf->Unlock();
    buf->SetCurrentLength(size);
    IMFSample* s = nullptr;
    MFCreateSample(&s);
    s->AddBuffer(buf);
    s->SetSampleTime(t);
    s->SetSampleDuration(10000000LL / fps_);
    HRESULT hr = writer_->WriteSample(video_, s);
    release(s);
    release(buf);
    return SUCCEEDED(hr);
}

bool Mp4Writer::writeAudio(const int16_t* samples, size_t frames, int64_t t) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!writer_ || !started_ || !hasAudio_ || frames == 0) return false;
    DWORD size = DWORD(frames * 4);
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(size, &buf))) return false;
    BYTE* dst = nullptr;
    buf->Lock(&dst, nullptr, nullptr);
    memcpy(dst, samples, size);
    buf->Unlock();
    buf->SetCurrentLength(size);
    IMFSample* s = nullptr;
    MFCreateSample(&s);
    s->AddBuffer(buf);
    s->SetSampleTime(t);
    s->SetSampleDuration(int64_t(frames) * 10000000LL / 48000);
    HRESULT hr = writer_->WriteSample(audio_, s);
    release(s);
    release(buf);
    return SUCCEEDED(hr);
}

bool Mp4Writer::close() {
    std::lock_guard<std::mutex> lock(mu_);
    bool ok = true;
#ifdef FRAGDESK_TESTHOOKS
    if (test_) {
        fclose(test_);
        test_ = nullptr;
        started_ = false;
        return true;
    }
#endif
    if (writer_) {
        if (started_) ok = SUCCEEDED(writer_->Finalize());
        release(writer_);
    }
    if (mfStarted_) MFShutdown();
    mfStarted_ = false;
    started_ = false;
    return ok;
}

// ---- Bild -----------------------------------------------------------------------------------------------------
ScreenCapture::~ScreenCapture() { close(); }

void ScreenCapture::close() {
    release(staging_);
    release(dup_);
    release(ctx_);
    release(dev_);
    if (memDc_) {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        DeleteDC(memDc_);
    }
    if (bmp_) DeleteObject(bmp_);
    if (screenDc_) ReleaseDC(nullptr, screenDc_);
    memDc_ = screenDc_ = nullptr;
    bmp_ = nullptr;
    oldBmp_ = nullptr;
    bits_ = nullptr;
    gdi_ = false;
    haveImage_ = false;
}

bool ScreenCapture::fitArea(const RECT& bounds, std::wstring& err) {
    RECT r;
    IntersectRect(&r, &area_, &bounds);
    int w = (r.right - r.left) & ~1, h = (r.bottom - r.top) & ~1;
    if (w < 64 || h < 64) {
        err = L"CS2-Fenster liegt nicht sichtbar auf dem Bildschirm";
        return false;
    }
    if (sized_ && (w != w_ || h != h_)) {   // nach einem Wechsel der Auflösung: das Video hat eine feste Größe
        err = L"Die Bildgröße hat sich während der Aufnahme geändert (Auflösung oder Vollbild gewechselt)";
        return false;
    }
    w_ = w;
    h_ = h;
    sized_ = true;
    area_ = {r.left, r.top, r.left + w_, r.top + h_};
    return true;
}

bool ScreenCapture::openGdi(std::wstring& err) {
    close();
    RECT screen = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    screen.right = screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    screen.bottom = screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (!fitArea(screen, err)) return false;
    screenDc_ = GetDC(nullptr);
    memDc_ = screenDc_ ? CreateCompatibleDC(screenDc_) : nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w_;
    bi.bmiHeader.biHeight = -h_;   // von oben nach unten
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp_ = memDc_ ? CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0) : nullptr;
    if (!bmp_ || !bits_) {
        close();
        err = L"Bildschirmaufnahme nicht möglich (kein Zwischenspeicher)";
        return false;
    }
    oldBmp_ = SelectObject(memDc_, bmp_);
    gdi_ = true;
    return true;
}

bool ScreenCapture::open(const RECT& area, std::wstring& err) {
    area_ = area;
    return reopen(err);
}

bool ScreenCapture::reopen(std::wstring& err) {
    if (gdi_) return openGdi(err);   // einmal auf GDI gewechselt: dabei bleiben
    close();
    // Desktop Duplication fehlt: GDI versuchen
    auto fallback = [&](const std::wstring& why) {
        logLine(L"Bildschirmaufnahme per GDI – %ls", why.c_str());
        return openGdi(err);
    };
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return fallback(L"DXGI nicht verfügbar");
    POINT center = {(area_.left + area_.right) / 2, (area_.top + area_.bottom) / 2};
    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput* o = nullptr;
        for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_OUTPUT_DESC d;
            o->GetDesc(&d);
            if (PtInRect(&d.DesktopCoordinates, center)) {
                output = o;
                outputRect_ = d.DesktopCoordinates;
                break;
            }
            o->Release();
        }
        if (!output) release(adapter);
    }
    release(factory);
    if (!output) return fallback(L"kein DXGI-Bildschirm für das CS2-Fenster");
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev_, &fl,
                                   &ctx_);
    release(adapter);
    if (FAILED(hr)) {
        release(output);
        return fallback(hrText(L"Direct3D 11 nicht verfügbar", hr));
    }
    IDXGIOutput1* out1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&out1));
    release(output);
    if (FAILED(hr)) return fallback(L"Windows ohne Desktop Duplication");
    hr = out1->DuplicateOutput(dev_, &dup_);
    release(out1);
    if (FAILED(hr)) {
        if (hr == E_ACCESSDENIED) {   // Sperrbildschirm/UAC: GDI sähe auch nur Schwarz
            err = L"Bildschirmaufnahme gerade nicht erlaubt (Sperrbildschirm oder UAC?)";
            return false;
        }
        return fallback(hrText(L"Desktop Duplication nicht möglich", hr));
    }
    if (!fitArea(outputRect_, err)) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = UINT(w_);
    td.Height = UINT(h_);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = dev_->CreateTexture2D(&td, nullptr, &staging_);
    if (FAILED(hr)) {
        err = hrText(L"Zwischenspeicher für das Bild fehlt", hr);
        return false;
    }
    return true;
}

bool ScreenCapture::grab(std::vector<uint8_t>& out, int timeoutMs, std::wstring& err) {
    if (gdi_) {
        // ohne CAPTUREBLT (das ließe den Mauszeiger flackern); Spiele im Fenster liefert der Bildschirm fertig zusammengesetzt
        if (!BitBlt(memDc_, 0, 0, w_, h_, screenDc_, area_.left, area_.top, SRCCOPY)) {
            err = L"Bild konnte nicht gelesen werden (GDI)";
            return false;
        }
        GdiFlush();
        out.resize(size_t(w_) * h_ * 4);
        memcpy(out.data(), bits_, out.size());
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;   // GDI lässt Alpha leer
        return true;
    }
    if (!dup_) return false;
    DXGI_OUTDUPL_FRAME_INFO info;
    IDXGIResource* res = nullptr;
    HRESULT hr = dup_->AcquireNextFrame(UINT(std::max(0, timeoutMs)), &info, &res);
    if (hr == DXGI_ERROR_ACCESS_LOST) {   // Auflösung/Vollbild gewechselt: neu verbinden
        if (!reopen(err)) return false;
        hr = dup_->AcquireNextFrame(UINT(std::max(0, timeoutMs)), &info, &res);
    }
    if (SUCCEEDED(hr)) {
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
            D3D11_TEXTURE2D_DESC d;
            tex->GetDesc(&d);
            if (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                D3D11_BOX box;
                box.left = UINT(area_.left - outputRect_.left);
                box.top = UINT(area_.top - outputRect_.top);
                box.right = box.left + UINT(w_);
                box.bottom = box.top + UINT(h_);
                box.front = 0;
                box.back = 1;
                ctx_->CopySubresourceRegion(staging_, 0, 0, 0, 0, tex, 0, &box);
                haveImage_ = true;
            } else {
                err = L"Bildschirm im HDR-Modus – bitte HDR für die Aufnahme ausschalten";
            }
            tex->Release();
        }
        res->Release();
        dup_->ReleaseFrame();
    } else if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
        err = hrText(L"Bild konnte nicht gelesen werden", hr);
        return false;
    }
    if (!haveImage_) return false;
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx_->Map(staging_, 0, D3D11_MAP_READ, 0, &map))) return false;
    out.resize(size_t(w_) * h_ * 4);
    for (int y = 0; y < h_; ++y) memcpy(&out[size_t(y) * w_ * 4], static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch, size_t(w_) * 4);
    ctx_->Unmap(staging_, 0);
    return true;
}

// ---- Ton ------------------------------------------------------------------------------------------------------
namespace {

// aus audioclientactivationparams.h (fehlt in MinGW)
enum ActivationType { kActivationDefault = 0, kActivationProcessLoopback = 1 };
enum LoopbackMode { kIncludeProcessTree = 0, kExcludeProcessTree = 1 };
struct ProcessLoopbackParams {
    DWORD targetProcessId;
    LoopbackMode mode;
};
struct ActivationParams {
    ActivationType type;
    ProcessLoopbackParams loopback;
};

const IID kIID_IAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~ActivateHandler() { CloseHandle(done); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler)) || IsEqualIID(riid, kIID_IAgileObject)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return ULONG(r);
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done);
        return S_OK;
    }

private:
    LONG ref_ = 1;
};

typedef HRESULT(WINAPI* ActivateFn)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                    IActivateAudioInterfaceAsyncOperation**);

}  // namespace

ProcessAudio::~ProcessAudio() { stop(); }

bool ProcessAudio::start(DWORD pid, std::wstring& err) {
    HMODULE mm = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto activate = mm ? reinterpret_cast<ActivateFn>(reinterpret_cast<void*>(GetProcAddress(mm, "ActivateAudioInterfaceAsync"))) : nullptr;
    if (!activate) {
        err = L"Ton-Aufnahme braucht Windows 10 (2004) oder neuer";
        return false;
    }
    ActivationParams params = {kActivationProcessLoopback, {pid, kIncludeProcessTree}};
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof params;
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    auto* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = activate(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr)) {
        if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0) hr = E_FAIL;
        else {
            HRESULT ar = E_FAIL;
            IUnknown* unk = nullptr;
            hr = op->GetActivateResult(&ar, &unk);
            if (SUCCEEDED(hr)) hr = ar;
            if (SUCCEEDED(hr) && unk) hr = unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
            if (unk) unk->Release();
        }
    }
    if (op) op->Release();
    handler->Release();
    if (FAILED(hr) || !client_) {
        err = hrText(L"Ton von CS2 kann nicht aufgenommen werden", hr);
        return false;
    }
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 48000 * 4;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 2000000, 0, &fmt,
                             nullptr);
    if (FAILED(hr)) {
        err = hrText(L"Ton-Aufnahme startet nicht", hr);
        release(client_);
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    client_->SetEventHandle(event_);
    hr = client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_));
    if (FAILED(hr) || FAILED(client_->Start())) {
        err = hrText(L"Ton-Aufnahme startet nicht", hr);
        stop();
        return false;
    }
    run_ = true;
    thread_ = std::thread(&ProcessAudio::loop, this);
    return true;
}

void ProcessAudio::loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (run_) {
        WaitForSingleObject(event_, 100);
        UINT32 packet = 0;
        while (run_ && SUCCEEDED(capture_->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            {
                std::lock_guard<std::mutex> lock(mu_);
                size_t old = buf_.size();
                buf_.resize(old + size_t(frames) * 2);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::fill(buf_.begin() + old, buf_.end(), int16_t(0));
                else memcpy(&buf_[old], data, size_t(frames) * 4);
                if (buf_.size() > 48000 * 2 * 30) buf_.erase(buf_.begin(), buf_.end() - 48000 * 2 * 30);   // höchstens 30 s puffern
            }
            capture_->ReleaseBuffer(frames);
        }
    }
    CoUninitialize();
}

void ProcessAudio::take(std::vector<int16_t>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    out.swap(buf_);
    buf_.clear();
}

void ProcessAudio::stop() {
    run_ = false;
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    release(capture_);
    release(client_);
    if (event_) CloseHandle(event_);
    event_ = nullptr;
}

}  // namespace fdw
