#include "capture.h"
#include "ndi.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <stdexcept>
#include <chrono>

namespace bemandi {
Counters counters;
int64_t ticks() noexcept { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
int64_t frequency() noexcept {
    static const int64_t value = [] { LARGE_INTEGER n; QueryPerformanceFrequency(&n); return n.QuadPart; }();
    return value;
}
static void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char message[256]; sprintf_s(message, "%s failed: 0x%08lX", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(message);
    }
}
// BT.709 limited range. Each BGRA output texel stores U,Y0,V,Y1 in memory.
static constexpr char shader[] = R"(
sampler2D image : register(s0);
float4 texel : register(c0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float3 a = tex2D(image, uv - float2(texel.x * 0.5, 0)).rgb;
    float3 b = tex2D(image, uv + float2(texel.x * 0.5, 0)).rgb;
    float3 avg = (a + b) * 0.5;
    float y0 = dot(a, float3(0.2126,0.7152,0.0722)) * (219.0/255.0) + 16.0/255.0;
    float y1 = dot(b, float3(0.2126,0.7152,0.0722)) * (219.0/255.0) + 16.0/255.0;
    float u = dot(avg, float3(-0.114572,-0.385428,0.5)) * (224.0/255.0) + 128.0/255.0;
    float v = dot(avg, float3(0.5,-0.454153,-0.045847)) * (224.0/255.0) + 128.0/255.0;
    return float4(v,y0,u,y1);
})";

Capture::Capture(IDirect3DDevice9* device, const Config& config) : config_(config), device_(device) {
    D3DDEVICE_CREATION_PARAMETERS creation{};
    check(device->GetCreationParameters(&creation), "GetCreationParameters");
    if (!(creation.BehaviorFlags & D3DCREATE_MULTITHREADED))
        throw std::runtime_error("Capture requires a D3DCREATE_MULTITHREADED device; no flags are changed");
    ComPtr<IDirect3DSurface9> backbuffer;
    check(device->GetBackBuffer(config.swapchain, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer), "GetBackBuffer");
    D3DSURFACE_DESC desc{}; check(backbuffer->GetDesc(&desc), "GetDesc");
    if (desc.Width != config.width || desc.Height != config.height)
        throw std::runtime_error("Backbuffer dimensions do not match configured capture target");
    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
        throw std::runtime_error("Only 8-bit BGRA/BGRX game backbuffers are supported");
    const bool uyvy = config.format == PixelFormat::uyvy;
    const unsigned storage_width = uyvy ? config.width / 2 : config.width;
    for (auto& slot : slots_) {
        check(device->CreateRenderTarget(storage_width, config.height, D3DFMT_A8R8G8B8,
            D3DMULTISAMPLE_NONE, 0, FALSE, &slot.gpu, nullptr), "CreateRenderTarget");
        check(device->CreateOffscreenPlainSurface(storage_width, config.height, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &slot.cpu, nullptr), "CreateOffscreenPlainSurface");
        check(device->CreateQuery(D3DQUERYTYPE_EVENT, &slot.copy_done), "CreateQuery");
    }
    if (uyvy) {
        check(device->CreateTexture(config.width, config.height, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &source_texture_, nullptr), "CreateTexture");
        check(source_texture_->GetSurfaceLevel(0, &source_surface_), "GetSurfaceLevel");
        ComPtr<ID3DBlob> code, error;
        auto hr = D3DCompile(shader, sizeof(shader)-1, "rgb_to_uyvy", nullptr, nullptr, "main", "ps_3_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
        if (FAILED(hr) && error) log(static_cast<const char*>(error->GetBufferPointer()));
        check(hr, "D3DCompile");
        check(device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &conversion_), "CreatePixelShader");
        check(device->CreateStateBlock(D3DSBT_ALL, &saved_state_), "CreateStateBlock");
        D3DCAPS9 caps{}; check(device->GetDeviceCaps(&caps), "GetDeviceCaps");
        render_target_count_ = std::min<DWORD>(caps.NumSimultaneousRTs, 4);
    }
    // NDI initialization, connection checks, sending, readback, and logging run on the worker.
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    epoch_ = static_cast<int64_t>((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime)
        - 116444736000000000LL - to_100ns(ticks(), frequency());
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wake_) throw std::runtime_error("CreateEvent failed");
    try { worker_ = std::thread(&Capture::work, this); }
    catch (...) { CloseHandle(wake_); wake_ = nullptr; throw; }
    valid_ = true;
}
Capture::~Capture() {
    stop_.store(true, std::memory_order_release);
    if (wake_) SetEvent(wake_);
    if (worker_.joinable()) worker_.join();
    if (wake_) CloseHandle(wake_);
}

HRESULT Capture::copy_frame(IDirect3DSurface9* source, Slot& slot) noexcept {
    if (config_.format == PixelFormat::bgra)
        return device_->StretchRect(source, nullptr, slot.gpu.Get(), nullptr, D3DTEXF_NONE);
    HRESULT hr = device_->StretchRect(source, nullptr, source_surface_.Get(), nullptr, D3DTEXF_NONE);
    if (FAILED(hr)) return hr;
    // State blocks do not save render targets/depth surfaces. Save these separately.
    std::array<ComPtr<IDirect3DSurface9>, 4> targets;
    ComPtr<IDirect3DSurface9> depth;
    for (DWORD i=0; i<render_target_count_; ++i) {
        hr = device_->GetRenderTarget(i, &targets[i]);
        if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
    }
    hr = device_->GetDepthStencilSurface(&depth);
    if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
    hr = saved_state_->Capture(); if (FAILED(hr)) return hr;
    auto restore = [&] {
        device_->SetDepthStencilSurface(nullptr);
        for (DWORD i=1; i<render_target_count_; ++i) device_->SetRenderTarget(i, nullptr);
        device_->SetRenderTarget(0, targets[0].Get());
        for (DWORD i=1; i<render_target_count_; ++i) device_->SetRenderTarget(i, targets[i].Get());
        device_->SetDepthStencilSurface(depth.Get());
        saved_state_->Apply();
    };
    // Check every state-setting operation; always restore if one fails.
    auto set = [&](HRESULT value) { if (FAILED(value) && SUCCEEDED(hr)) hr = value; };
    hr = S_OK;
    set(device_->SetDepthStencilSurface(nullptr));
    for (DWORD i=1; i<render_target_count_; ++i) set(device_->SetRenderTarget(i, nullptr));
    set(device_->SetRenderTarget(0, slot.gpu.Get()));
    D3DVIEWPORT9 viewport{0,0,config_.width/2,config_.height,0.0f,1.0f};
    set(device_->SetViewport(&viewport));
    set(device_->SetVertexShader(nullptr));
    set(device_->SetPixelShader(conversion_.Get()));
    set(device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1));
    set(device_->SetTexture(0, source_texture_.Get()));
    set(device_->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));
    set(device_->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    set(device_->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    set(device_->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
    set(device_->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    set(device_->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
    set(device_->SetRenderState(D3DRS_ZENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_STENCILENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    set(device_->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
    set(device_->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    set(device_->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_FOGENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
    set(device_->SetStreamSourceFreq(0,1));
    set(device_->SetRenderState(D3DRS_DITHERENABLE,FALSE));
    set(device_->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
    const float texel[4]{1.0f/static_cast<float>(config_.width),1.0f/static_cast<float>(config_.height),0,0};
    set(device_->SetPixelShaderConstantF(0,texel,1));
    struct Vertex { float x,y,z,w,u,v; };
    const float w = static_cast<float>(config_.width/2), h = static_cast<float>(config_.height);
    const Vertex quad[]{{-0.5f,-0.5f,0,1,0,0},{w-0.5f,-0.5f,0,1,1,0},{-0.5f,h-0.5f,0,1,0,1},{w-0.5f,h-0.5f,0,1,1,1}};
    if (SUCCEEDED(hr)) {
        hr = device_->BeginScene();
        if (SUCCEEDED(hr)) {
            hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
            const auto end = device_->EndScene();
            if (SUCCEEDED(hr)) hr = end;
        }
    }
    restore();
    return hr;
}

void Capture::frame() noexcept {
    if (presenting_.test_and_set(std::memory_order_acquire)) { ++counters.dropped_busy; return; }
    struct Unlock { std::atomic_flag& flag; ~Unlock(){ flag.clear(std::memory_order_release); } } unlock{presenting_};
    ++counters.presented;
    if (failed_.load(std::memory_order_relaxed) || !wanted_.load(std::memory_order_acquire)) {
        ++counters.skipped_no_receiver; return;
    }
    const auto start = ticks();
    if (!cadence_.due(start,frequency(),config_.fps)) { ++counters.skipped_cadence; return; }
    Slot* selected = nullptr;
    for (auto& slot : slots_) if (claim(slot.state)) { selected=&slot; break; }
    if (!selected) { ++counters.dropped_busy; return; }
    ComPtr<IDirect3DSurface9> source;
    HRESULT hr = device_->GetBackBuffer(config_.swapchain,0,D3DBACKBUFFER_TYPE_MONO,&source);
    if (SUCCEEDED(hr)) hr = copy_frame(source.Get(), *selected);
    if (SUCCEEDED(hr)) hr = selected->copy_done->Issue(D3DISSUE_END);
    if (FAILED(hr)) {
        ++counters.errors;
        selected->state.store(SlotState::free,std::memory_order_release);
        if (hr==D3DERR_DEVICELOST || hr==D3DERR_DEVICENOTRESET) { failed_=true; wanted_=false; }
    }
    else {
        selected->sequence = ++sequence_;
        selected->pts = epoch_ + to_100ns(start, frequency());
        selected->state.store(SlotState::ready,std::memory_order_release);
        ++counters.captured;
        SetEvent(wake_);
    }
    const auto us = static_cast<uint64_t>((ticks()-start)*1000000/frequency());
    auto old = counters.max_capture_us.load(std::memory_order_relaxed);
    while (old < us && !counters.max_capture_us.compare_exchange_weak(old,us,std::memory_order_relaxed)) {}
}
void Capture::work() noexcept {
    try { run(); }
    catch (const std::exception& error) { ++counters.errors; log(error.what()); failed_=true; wanted_=false; }
    catch (...) { ++counters.errors; log("Unexpected capture worker error"); failed_=true; wanted_=false; }
}
void Capture::run() {
    ndi_ = std::make_unique<NdiSender>(config_.ndi_runtime,config_.source_name,config_.fps);
    log(config_.format == PixelFormat::uyvy ? "NDI sender ready: GPU UYVY conversion" : "NDI sender ready: BGRA baseline");
    Slot* pending = nullptr;
    // Always flush retained SDK buffers before unlocking or destroying their storage.
    auto release_pending = [&] {
        if (pending) {
            pending->cpu->UnlockRect();
            pending->state.store(SlotState::free,std::memory_order_release);
            pending=nullptr;
        }
    };
    try {
        int64_t connection_check = 0, report_time = ticks()+frequency()*5;
        while (!stop_.load(std::memory_order_acquire)) {
            const auto now = ticks();
            if (now >= connection_check) {
                const bool connected = ndi_->connected();
                wanted_.store(connected && !failed_.load(std::memory_order_relaxed),std::memory_order_release);
                connection_check = now + frequency()/4;
                if (!connected && pending) { ndi_->flush(); release_pending(); }
            }
            if(now>=report_time) {
                char message[320];
                sprintf_s(message,"capture totals: presented=%llu captured=%llu sent=%llu busy=%llu idle=%llu errors=%llu max_copy_us=%llu",
                    counters.presented.load(),counters.captured.load(),counters.completed.load(),
                    counters.dropped_busy.load(),counters.skipped_no_receiver.load(),counters.errors.load(),counters.max_capture_us.load());
                log(message);
                report_time=now+frequency()*5;
            }
            Slot* next = nullptr;
            for (auto& slot : slots_) {
                if (slot.state.load(std::memory_order_acquire) == SlotState::ready && (!next || slot.sequence < next->sequence)) next = &slot;
            }
            if (!next) { WaitForSingleObject(wake_,2); continue; }
            const auto hr = next->copy_done->GetData(nullptr,0,0);
            if (hr == S_FALSE) { WaitForSingleObject(wake_,1); continue; }
            check(hr,"GPU completion query");
            if (!wanted_.load(std::memory_order_relaxed)) { next->state.store(SlotState::free,std::memory_order_release); continue; }
            check(device_->GetRenderTargetData(next->gpu.Get(), next->cpu.Get()),"GetRenderTargetData");
            D3DLOCKED_RECT locked{};
            check(next->cpu->LockRect(&locked,nullptr,D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK),"LockRect");
            next->state.store(SlotState::inflight,std::memory_order_release);
            ndi_->send(locked.pBits,config_.width,config_.height,locked.Pitch,next->pts,config_.format==PixelFormat::uyvy);
            release_pending(); // The next async submission has completed previous-buffer use.
            pending=next;
            ++counters.completed;
            counters.bytes += static_cast<uint64_t>(config_.width)*config_.height*(config_.format==PixelFormat::uyvy ? 2 : 4);
        }
        ndi_->flush(); release_pending();
        ndi_.reset();
    } catch (...) {
        ndi_->flush(); release_pending(); ndi_.reset(); throw;
    }
}
}
