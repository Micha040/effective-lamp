// SnowDesk – GPU-Zeichnen: D3D11 + DirectComposition (durchsichtige Swapchain je Monitor-Fenster)
#pragma once
#include "common.h"

#include "../snowcore.h"

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

namespace app {

class Renderer {
public:
    enum Result { Ok, Skipped, DeviceLost, Failed };

    ~Renderer() { shutdown(); }
    bool init(bool composition = true);   // composition = false: nur für Tests (offscreen)
    void shutdown();
    bool ready() const { return device_ && (dcomp_ || !composition_); }
    bool deviceLost() const { return device_ && device_->GetDeviceRemovedReason() != S_OK; }

    // Eine Zeichenfläche je Ebenen-Fenster (Reihenfolge = Index)
    bool addSurface(HWND hwnd, int width, int height);
    bool addOffscreenSurface(int width, int height);                 // Test ohne Fenster (--headless-test)
    bool snapshot(size_t index, std::vector<unsigned char>& bgra);   // nur Offscreen-Flächen
    void clearSurfaces();
    size_t surfaceCount() const { return surfaces_.size(); }

    Result draw(size_t index, const std::vector<snow::Sprite>& sprites, const std::vector<snow::PileVertex>& mesh, float scale);
    // Für Tests/Diagnose: dasselbe Zeichnen in eine Textur, Ergebnis als BGRA (vormultipliziert)
    bool renderOffscreen(int width, int height, const std::vector<snow::Sprite>& sprites, const std::vector<snow::PileVertex>& mesh,
                         float scale, std::vector<unsigned char>& bgra);
    std::wstring adapterName() const { return adapter_; }
    bool software() const { return software_; }

private:
    struct Surface {
        HWND hwnd = nullptr;
        int width = 0, height = 0;
        Com<IDXGISwapChain1> swap;
        Com<ID3D11Texture2D> tex;   // nur Offscreen
        Com<ID3D11RenderTargetView> rtv;
        Com<IDCompositionTarget> target;
        Com<IDCompositionVisual> visual;
    };
    bool createDevice(D3D_DRIVER_TYPE type);
    bool createPipeline();
    bool ensureBuffer(Com<ID3D11Buffer>& buf, UINT& capacity, UINT needed, UINT stride);
    Result present(Surface& s);
    void drawTo(ID3D11RenderTargetView* rtv, int width, int height, const std::vector<snow::Sprite>& sprites,
                const std::vector<snow::PileVertex>& mesh, float scale);

    Com<ID3D11Device> device_;
    Com<ID3D11DeviceContext> ctx_;
    Com<IDXGIDevice1> dxgiDevice_;
    Com<IDXGIFactory2> factory_;
    Com<IDCompositionDevice> dcomp_;
    Com<ID3D11VertexShader> spriteVS_, pileVS_;
    Com<ID3D11PixelShader> spritePS_, pilePS_;
    Com<ID3D11InputLayout> spriteLayout_, pileLayout_;
    Com<ID3D11Buffer> spriteBuf_, pileBuf_, frameCB_;
    UINT spriteCap_ = 0, pileCap_ = 0;
    Com<ID3D11BlendState> blend_;
    Com<ID3D11RasterizerState> raster_;
    std::vector<Surface> surfaces_;
    std::wstring adapter_;
    bool software_ = false;
    bool composition_ = true;
};

}  // namespace app
