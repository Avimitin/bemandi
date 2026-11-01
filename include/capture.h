#pragma once
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include "bemandi_api.h"
#include "core.h"
#include "config.h"
#include "timing.h"

namespace bemandi {
using Microsoft::WRL::ComPtr;
constexpr size_t slot_count = 4;
struct Counters {
    std::atomic<uint64_t> presented{}, captured{}, dropped_busy{}, skipped_cadence{}, skipped_no_receiver{},
        completed{}, bytes{}, errors{}, max_capture_us{}, resets{};
};
extern Counters counters;
void log(const char* text) noexcept;
struct Slot {
    std::atomic<SlotState> state{SlotState::free};
    ComPtr<IDirect3DSurface9> gpu, cpu;
    ComPtr<IDirect3DQuery9> copy_done;
    uint64_t sequence = 0;
    int64_t pts = 0;
};
class NdiSender;
class Capture {
public:
    Capture(IDirect3DDevice9* device, const Config& config);
    ~Capture();
    bool valid() const { return valid_; }
    void frame() noexcept;
    IDirect3DDevice9* device() const { return device_.Get(); }
    unsigned swapchain() const { return config_.swapchain; }
private:
    void work() noexcept;
    void run();
    HRESULT copy_frame(IDirect3DSurface9* source, Slot& slot) noexcept;
    Config config_;
    ComPtr<IDirect3DDevice9> device_;
    std::array<Slot, slot_count> slots_;
    std::unique_ptr<NdiSender> ndi_;
    ComPtr<IDirect3DTexture9> source_texture_;
    ComPtr<IDirect3DSurface9> source_surface_;
    ComPtr<IDirect3DPixelShader9> conversion_;
    ComPtr<IDirect3DStateBlock9> saved_state_;
    DWORD render_target_count_ = 1;
    std::thread worker_;
    std::atomic<bool> stop_{false}, wanted_{false}, failed_{false};
    HANDLE wake_ = nullptr;
    bool valid_ = false;
    Cadence cadence_;
    uint64_t sequence_ = 0;
    int64_t epoch_ = 0;
    std::atomic_flag presenting_ = ATOMIC_FLAG_INIT;
};
}
