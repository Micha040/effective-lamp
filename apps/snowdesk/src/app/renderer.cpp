// SnowDesk – GPU-Zeichnen (siehe renderer.h)
#include "renderer.h"

#include "shaders.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

namespace app {

namespace {

struct FrameConstants {
    float viewW, viewH, scale, pad;
};

bool compile(const char* entry, const char* target, Com<ID3DBlob>& out) {
    Com<ID3DBlob> errors;
    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "snowdesk.hlsl", nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.put(), errors.put());
    if (FAILED(hr)) {
        std::string msg = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : std::string();
        std::wstring wmsg(msg.begin(), msg.end());
        logLine(L"Shader %hs (%hs) nicht übersetzbar: %ls %ls", entry, target, hrText(hr).c_str(), wmsg.c_str());
        return false;
    }
    return true;
}

}  // namespace

bool Renderer::createDevice(D3D_DRIVER_TYPE type) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, flags, levels, 4, D3D11_SDK_VERSION, device_.put(), &got, ctx_.put());
    if (hr == E_INVALIDARG) {   // ältere Laufzeit ohne 11_1
        hr = D3D11CreateDevice(nullptr, type, nullptr, flags, levels + 1, 3, D3D11_SDK_VERSION, device_.put(), &got, ctx_.put());
    }
    if (FAILED(hr)) {
        logLine(L"D3D11CreateDevice (%ls) fehlgeschlagen: %ls", type == D3D_DRIVER_TYPE_WARP ? L"WARP" : L"Hardware", hrText(hr).c_str());
        return false;
    }
    software_ = type == D3D_DRIVER_TYPE_WARP;
    return true;
}

bool Renderer::init(bool composition) {
    shutdown();
    composition_ = composition;
    if (!createDevice(D3D_DRIVER_TYPE_HARDWARE) && !createDevice(D3D_DRIVER_TYPE_WARP)) return false;

    Com<IDXGIDevice1> dxgiDevice;
    HRESULT hr = device_->QueryInterface(__uuidof(IDXGIDevice1), dxgiDevice.putVoid());
    if (FAILED(hr)) {
        logLine(L"IDXGIDevice1 fehlt: %ls", hrText(hr).c_str());
        shutdown();
        return false;
    }
    dxgiDevice_ = dxgiDevice;   // Bild-Warteschlange wird je nach Anzahl der Monitore gesetzt (addSurface)
    Com<IDXGIAdapter> adapter;
    if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.put()))) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(adapter->GetDesc(&desc))) adapter_ = desc.Description;
        adapter->GetParent(__uuidof(IDXGIFactory2), factory_.putVoid());
    }
    if (!factory_) {
        logLine(L"IDXGIFactory2 fehlt (Windows zu alt?)");
        shutdown();
        return false;
    }
    hr = composition ? DCompositionCreateDevice(dxgiDevice.get(), __uuidof(IDCompositionDevice), dcomp_.putVoid()) : S_OK;
    if (FAILED(hr)) {
        logLine(L"DCompositionCreateDevice fehlgeschlagen: %ls", hrText(hr).c_str());
        shutdown();
        return false;
    }
    if (!createPipeline()) {
        shutdown();
        return false;
    }
    logLine(L"Grafik bereit: %ls%ls", adapter_.c_str(), software_ ? L" (Software)" : L"");
    return true;
}

bool Renderer::createPipeline() {
    Com<ID3DBlob> vsS, psS, vsP, psP;
    if (!compile("vsSprite", "vs_4_0", vsS) || !compile("psSprite", "ps_4_0", psS) || !compile("vsPile", "vs_4_0", vsP) ||
        !compile("psPile", "ps_4_0", psP))
        return false;
    HRESULT hr = device_->CreateVertexShader(vsS->GetBufferPointer(), vsS->GetBufferSize(), nullptr, spriteVS_.put());
    if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(psS->GetBufferPointer(), psS->GetBufferSize(), nullptr, spritePS_.put());
    if (SUCCEEDED(hr)) hr = device_->CreateVertexShader(vsP->GetBufferPointer(), vsP->GetBufferSize(), nullptr, pileVS_.put());
    if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(psP->GetBufferPointer(), psP->GetBufferSize(), nullptr, pilePS_.put());
    if (FAILED(hr)) {
        logLine(L"Shader anlegen fehlgeschlagen: %ls", hrText(hr).c_str());
        return false;
    }
    static_assert(sizeof(snow::Sprite) == 32, "Sprite-Layout");
    static_assert(sizeof(snow::PileVertex) == 16, "PileVertex-Layout");
    const D3D11_INPUT_ELEMENT_DESC spriteDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"ALPHA", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"ROT", 0, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SOFT", 0, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SEED", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"KIND", 0, DXGI_FORMAT_R32_UINT, 0, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = device_->CreateInputLayout(spriteDesc, 7, vsS->GetBufferPointer(), vsS->GetBufferSize(), spriteLayout_.put());
    const D3D11_INPUT_ELEMENT_DESC pileDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"DIST", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"HEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (SUCCEEDED(hr)) hr = device_->CreateInputLayout(pileDesc, 3, vsP->GetBufferPointer(), vsP->GetBufferSize(), pileLayout_.put());
    if (FAILED(hr)) {
        logLine(L"Eingabe-Layout fehlgeschlagen: %ls", hrText(hr).c_str());
        return false;
    }

    D3D11_BUFFER_DESC cb;
    ZeroMemory(&cb, sizeof(cb));
    cb.ByteWidth = sizeof(FrameConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cb, nullptr, frameCB_.put());

    // vormultipliziertes Alpha "über" den Inhalt
    D3D11_BLEND_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = device_->CreateBlendState(&bd, blend_.put());

    D3D11_RASTERIZER_DESC rd;
    ZeroMemory(&rd, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = device_->CreateRasterizerState(&rd, raster_.put());
    if (FAILED(hr)) {
        logLine(L"Zeichenzustände fehlgeschlagen: %ls", hrText(hr).c_str());
        return false;
    }
    return true;
}

void Renderer::shutdown() {
    clearSurfaces();
    if (ctx_) {
        ctx_->ClearState();
        ctx_->Flush();
    }
    spriteBuf_.reset();
    pileBuf_.reset();
    spriteCap_ = pileCap_ = 0;
    frameCB_.reset();
    blend_.reset();
    raster_.reset();
    spriteLayout_.reset();
    pileLayout_.reset();
    spriteVS_.reset();
    spritePS_.reset();
    pileVS_.reset();
    pilePS_.reset();
    dcomp_.reset();
    factory_.reset();
    dxgiDevice_.reset();
    ctx_.reset();
    device_.reset();
}

bool Renderer::addSurface(HWND hwnd, int width, int height) {
    if (!device_ || !dcomp_ || !hwnd || width <= 0 || height <= 0) return false;
    Surface s;
    s.hwnd = hwnd;
    s.width = width;
    s.height = height;
    DXGI_SWAP_CHAIN_DESC1 d;
    ZeroMemory(&d, sizeof(d));
    d.Width = (UINT)width;
    d.Height = (UINT)height;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    HRESULT hr = factory_->CreateSwapChainForComposition(device_.get(), &d, nullptr, s.swap.put());
    if (FAILED(hr)) {
        logLine(L"CreateSwapChainForComposition %dx%d fehlgeschlagen: %ls", width, height, hrText(hr).c_str());
        return false;
    }
    hr = dcomp_->CreateTargetForHwnd(hwnd, TRUE, s.target.put());
    if (SUCCEEDED(hr)) hr = dcomp_->CreateVisual(s.visual.put());
    if (SUCCEEDED(hr)) hr = s.visual->SetContent(s.swap.get());
    if (SUCCEEDED(hr)) hr = s.target->SetRoot(s.visual.get());
    if (SUCCEEDED(hr)) hr = dcomp_->Commit();
    if (FAILED(hr)) {
        logLine(L"DirectComposition-Ziel für %p fehlgeschlagen: %ls", (void*)hwnd, hrText(hr).c_str());
        return false;
    }
    Com<ID3D11Texture2D> back;
    hr = s.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), back.putVoid());
    if (SUCCEEDED(hr)) hr = device_->CreateRenderTargetView(back.get(), nullptr, s.rtv.put());
    if (FAILED(hr)) {
        logLine(L"Zeichenziel fehlgeschlagen: %ls", hrText(hr).c_str());
        return false;
    }
    surfaces_.push_back(s);
    // Die Warteschlangen-Grenze gilt fürs ganze Gerät: je Monitor ein Bild Platz, sonst stocken weitere Monitore
    if (dxgiDevice_) dxgiDevice_->SetMaximumFrameLatency((UINT)std::min<size_t>(16, surfaces_.size() + 1));
    // leer (durchsichtig) starten
    std::vector<snow::Sprite> none;
    std::vector<snow::PileVertex> noMesh;
    draw(surfaces_.size() - 1, none, noMesh, 1.0f);
    return true;
}

bool Renderer::addOffscreenSurface(int width, int height) {
    if (!device_ || width <= 0 || height <= 0) return false;
    Surface s;
    s.width = width;
    s.height = height;
    D3D11_TEXTURE2D_DESC td;
    ZeroMemory(&td, sizeof(td));
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, s.tex.put()))) return false;
    if (FAILED(device_->CreateRenderTargetView(s.tex.get(), nullptr, s.rtv.put()))) return false;
    surfaces_.push_back(s);
    return true;
}

bool Renderer::snapshot(size_t index, std::vector<unsigned char>& bgra) {
    if (index >= surfaces_.size() || !surfaces_[index].tex) return false;
    Surface& s = surfaces_[index];
    D3D11_TEXTURE2D_DESC td;
    s.tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, staging.put()))) return false;
    ctx_->CopyResource(staging.get(), s.tex.get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    bgra.resize((size_t)s.width * s.height * 4);
    for (int y = 0; y < s.height; ++y) memcpy(&bgra[(size_t)y * s.width * 4], (const unsigned char*)m.pData + (size_t)y * m.RowPitch, (size_t)s.width * 4);
    ctx_->Unmap(staging.get(), 0);
    return true;
}

void Renderer::clearSurfaces() {
    for (Surface& s : surfaces_) {
        if (s.target) s.target->SetRoot(nullptr);
    }
    if (dcomp_ && !surfaces_.empty()) dcomp_->Commit();
    surfaces_.clear();
    if (ctx_) {
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        ctx_->Flush();
    }
}

bool Renderer::ensureBuffer(Com<ID3D11Buffer>& buf, UINT& capacity, UINT needed, UINT stride) {
    if (buf && capacity >= needed) return true;
    UINT cap = capacity ? capacity : 1024;
    while (cap < needed) cap *= 2;
    D3D11_BUFFER_DESC d;
    ZeroMemory(&d, sizeof(d));
    d.ByteWidth = cap * stride;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf.reset();
    if (FAILED(device_->CreateBuffer(&d, nullptr, buf.put()))) {
        capacity = 0;
        return false;
    }
    capacity = cap;
    return true;
}

Renderer::Result Renderer::present(Surface& s) {
    HRESULT hr = s.swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return Skipped;   // Grafikkarte noch beschäftigt: dieses Bild auslassen
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : hr;
        logLine(L"Grafikgerät verloren: %ls", hrText(reason).c_str());
        return DeviceLost;
    }
    if (FAILED(hr)) {
        static int logged = 0;
        if (logged++ < 5) logLine(L"Present fehlgeschlagen: %ls", hrText(hr).c_str());
        return Failed;
    }
    return Ok;
}

Renderer::Result Renderer::draw(size_t index, const std::vector<snow::Sprite>& sprites, const std::vector<snow::PileVertex>& mesh, float scale) {
    if (!ready() || index >= surfaces_.size()) return Failed;
    Surface& s = surfaces_[index];
    drawTo(s.rtv.get(), s.width, s.height, sprites, mesh, scale);
    if (!s.swap) {
        ctx_->Flush();
        return Ok;
    }
    return present(s);
}

bool Renderer::renderOffscreen(int width, int height, const std::vector<snow::Sprite>& sprites, const std::vector<snow::PileVertex>& mesh,
                               float scale, std::vector<unsigned char>& bgra) {
    if (!device_) return false;
    D3D11_TEXTURE2D_DESC td;
    ZeroMemory(&td, sizeof(td));
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> target, staging;
    Com<ID3D11RenderTargetView> rtv;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, target.put()))) return false;
    if (FAILED(device_->CreateRenderTargetView(target.get(), nullptr, rtv.put()))) return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, staging.put()))) return false;
    drawTo(rtv.get(), width, height, sprites, mesh, scale);
    ctx_->CopyResource(staging.get(), target.get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    bgra.resize((size_t)width * height * 4);
    for (int y = 0; y < height; ++y) memcpy(&bgra[(size_t)y * width * 4], (const unsigned char*)m.pData + (size_t)y * m.RowPitch, (size_t)width * 4);
    ctx_->Unmap(staging.get(), 0);
    return true;
}

void Renderer::drawTo(ID3D11RenderTargetView* rtv, int width, int height, const std::vector<snow::Sprite>& sprites,
                      const std::vector<snow::PileVertex>& mesh, float scale) {
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    ctx_->RSSetViewports(1, &vp);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ctx_->ClearRenderTargetView(rtv, clear);

    if (!sprites.empty() || !mesh.empty()) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(frameCB_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            FrameConstants fc{(float)width, (float)height, scale, 0.0f};
            memcpy(m.pData, &fc, sizeof(fc));
            ctx_->Unmap(frameCB_.get(), 0);
        }
        ID3D11Buffer* cbs[1] = {frameCB_.get()};
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        const float factor[4] = {0, 0, 0, 0};
        ctx_->OMSetBlendState(blend_.get(), factor, 0xFFFFFFFF);
        ctx_->RSSetState(raster_.get());

        if (!mesh.empty() && ensureBuffer(pileBuf_, pileCap_, (UINT)mesh.size(), sizeof(snow::PileVertex))) {
            if (SUCCEEDED(ctx_->Map(pileBuf_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                memcpy(m.pData, mesh.data(), mesh.size() * sizeof(snow::PileVertex));
                ctx_->Unmap(pileBuf_.get(), 0);
                ID3D11Buffer* vb = pileBuf_.get();
                UINT stride = sizeof(snow::PileVertex), offset = 0;
                ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                ctx_->IASetInputLayout(pileLayout_.get());
                ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ctx_->VSSetShader(pileVS_.get(), nullptr, 0);
                ctx_->PSSetShader(pilePS_.get(), nullptr, 0);
                ctx_->Draw((UINT)mesh.size(), 0);
            }
        }
        if (!sprites.empty() && ensureBuffer(spriteBuf_, spriteCap_, (UINT)sprites.size(), sizeof(snow::Sprite))) {
            if (SUCCEEDED(ctx_->Map(spriteBuf_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                memcpy(m.pData, sprites.data(), sprites.size() * sizeof(snow::Sprite));
                ctx_->Unmap(spriteBuf_.get(), 0);
                ID3D11Buffer* vb = spriteBuf_.get();
                UINT stride = sizeof(snow::Sprite), offset = 0;
                ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                ctx_->IASetInputLayout(spriteLayout_.get());
                ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                ctx_->VSSetShader(spriteVS_.get(), nullptr, 0);
                ctx_->PSSetShader(spritePS_.get(), nullptr, 0);
                ctx_->DrawInstanced(4, (UINT)sprites.size(), 0, 0);
            }
        }
    }
}

}  // namespace app
