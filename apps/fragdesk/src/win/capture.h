// FragDesk – Bild und Ton eines Fensters von außen aufnehmen und als MP4 speichern
//   Bild: DXGI Desktop Duplication (wie OBS „Bildschirmaufnahme“), auf das Fenster zugeschnitten
//   Ton:  WASAPI-Loopback nur für einen Prozess (ab Windows 10 2004)
//   MP4:  Media Foundation (H.264 + AAC, nutzt den Hardware-Encoder der Grafikkarte, falls vorhanden)
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IMFSinkWriter;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IDXGIOutputDuplication;
struct IAudioClient;
struct IAudioCaptureClient;

namespace fdw {

class Mp4Writer {
public:
    ~Mp4Writer();
    // fps, Bitrate in Bit/s; withAudio: 48 kHz, 16 Bit, Stereo
    bool open(const std::wstring& path, int width, int height, int fps, int bitrate, bool withAudio, std::wstring& err);
    bool writeFrame(const uint8_t* bgra, int stride, int64_t time100ns);   // Bild oben links beginnend
    bool writeAudio(const int16_t* samples, size_t frames, int64_t time100ns);
    bool close();
    int width() const { return w_; }
    int height() const { return h_; }
    int fps() const { return fps_; }
    bool hasAudio() const { return hasAudio_; }   // false, wenn der Ton-Encoder fehlte (Video ohne Ton)

private:
    std::mutex mu_;
    IMFSinkWriter* writer_ = nullptr;
    DWORD video_ = 0, audio_ = 0;
    bool hasAudio_ = false, started_ = false, mfStarted_ = false;
    int w_ = 0, h_ = 0, fps_ = 60;
#ifdef FRAGDESK_TESTHOOKS
    FILE* test_ = nullptr;   // nur Test-Build (siehe open)
    std::wstring testShot_;
#endif
};

class ScreenCapture {
public:
    ~ScreenCapture();
    // Ausschnitt in Bildschirmpixeln (muss auf einem Monitor liegen)
    bool open(const RECT& area, std::wstring& err);
    // Holt das aktuelle Bild (wartet höchstens timeoutMs auf ein neues) und kopiert den Ausschnitt nach out (BGRA)
    bool grab(std::vector<uint8_t>& out, int timeoutMs, std::wstring& err);
    int width() const { return w_; }
    int height() const { return h_; }
    void close();

private:
    bool reopen(std::wstring& err);
    // Ersatzweg, wenn Windows keine Desktop Duplication anbietet (manche Laptops mit zwei Grafikkarten, Remotedesktop,
    // Wine): Bild per GDI vom Bildschirm kopieren – langsamer, aber ebenfalls von außen und ohne Mauszeiger
    bool openGdi(std::wstring& err);
    bool fitArea(const RECT& bounds, std::wstring& err);   // Ausschnitt auf bounds begrenzen, gerade Maße für H.264
    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    IDXGIOutputDuplication* dup_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    HDC screenDc_ = nullptr, memDc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ oldBmp_ = nullptr;
    void* bits_ = nullptr;
    RECT area_{}, outputRect_{};
    int w_ = 0, h_ = 0;
    bool haveImage_ = false, sized_ = false;   // sized_: Größe steht seit dem ersten Öffnen fest
    bool gdi_ = false;
};

class ProcessAudio {
public:
    ~ProcessAudio();
    bool start(DWORD pid, std::wstring& err);   // 48 kHz, 16 Bit, Stereo
    // Alle seit dem letzten Aufruf aufgenommenen Samples (Frames à 2 Kanäle)
    void take(std::vector<int16_t>& out);
    void stop();

private:
    void loop();
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    HANDLE event_ = nullptr;
    std::thread thread_;
    std::atomic<bool> run_{false};
    std::mutex mu_;
    std::vector<int16_t> buf_;
};

}  // namespace fdw
