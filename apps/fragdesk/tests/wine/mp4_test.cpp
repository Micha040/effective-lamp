// FragDesk – Mp4Writer ohne Bildschirm prüfen: 3 s Testbild (bewegter Verlauf) mit 440-Hz-Ton als MP4 schreiben.
// Läuft unter Windows und Wine (mit winegstreamer). Aufruf: mp4_test.exe <ziel.mp4> [Breite Höhe fps]
#include <objbase.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "../../src/win/capture.h"

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::wstring out = argc > 1 ? argv[1] : L"mp4test.mp4";
    int w = argc > 4 ? _wtoi(argv[2]) : 640, h = argc > 4 ? _wtoi(argv[3]) : 360, fps = argc > 4 ? _wtoi(argv[4]) : 30;
    fdw::Mp4Writer mp4;
    std::wstring err;
    if (!mp4.open(out, w, h, fps, 2000000, true, err)) {
        wprintf(L"FEHLER beim Öffnen: %ls\n", err.c_str());
        return 1;
    }
    std::vector<uint8_t> img(size_t(w) * h * 4);
    const int perFrame = 48000 / fps;
    std::vector<int16_t> pcm(size_t(perFrame) * 2);
    int64_t sample = 0;
    for (int f = 0; f < fps * 3; ++f) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* p = &img[(size_t(y) * w + x) * 4];
                p[0] = uint8_t((x + f * 4) & 255);   // B
                p[1] = uint8_t((y * 255) / h);       // G
                p[2] = uint8_t(f * 255 / (fps * 3)); // R
                p[3] = 255;
            }
        if (!mp4.writeFrame(img.data(), w * 4, int64_t(f) * 10000000 / fps)) {
            wprintf(L"FEHLER bei Bild %d\n", f);
            return 2;
        }
        for (int i = 0; i < perFrame; ++i, ++sample) {
            int16_t v = int16_t(std::sin(2 * 3.14159265358979 * 440 * double(sample) / 48000) * 8000);
            pcm[size_t(i) * 2] = pcm[size_t(i) * 2 + 1] = v;
        }
        if (!mp4.writeAudio(pcm.data(), size_t(perFrame), int64_t(f) * 10000000 / fps)) {
            wprintf(L"FEHLER bei Ton %d\n", f);
            return 3;
        }
    }
    bool ok = mp4.close();
    wprintf(L"%ls: %ls\n", out.c_str(), ok ? L"fertig" : L"FEHLER beim Abschließen");
    CoUninitialize();
    return ok ? 0 : 4;
}
