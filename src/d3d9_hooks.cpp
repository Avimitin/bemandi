#include "capture.h"
#include "d3d9_hooks.h"
#include "d3d9_abi.h"
#include <MinHook.h>
#include <mutex>
#include <shared_mutex>
namespace bemandi {
static Config config;
static std::unique_ptr<Capture> capture;
static std::shared_mutex capture_mutex;
static std::mutex hooks_mutex;
static std::atomic<bool> enabled{false};
static thread_local unsigned present_depth = 0, reset_depth = 0;
template<typename Fn, unsigned Slot> struct Hooks {
    struct Record { std::atomic<void*> target{nullptr}; Fn original = nullptr; };
    std::array<Record,8> records{};
    bool add(void* target, void* detour) {
        std::lock_guard lock(hooks_mutex);
        for (auto& r : records) if (r.target.load(std::memory_order_acquire)==target) return true;
        for (auto& r : records) {
            if (r.target.load(std::memory_order_relaxed)) continue;
            void* original = nullptr;
            auto status = MH_CreateHook(target,detour,&original);
            if (status != MH_OK) { log(MH_StatusToString(status)); return false; }
            r.original = reinterpret_cast<Fn>(original);
            r.target.store(target,std::memory_order_release);
            status = MH_EnableHook(target);
            if (status != MH_OK) { log(MH_StatusToString(status)); return false; }
            return true;
        }
        log("Too many D3D9 implementations; hook limit reached"); return false;
    }
    bool add_object(void* object, void* detour) { return add((*static_cast<void***>(object))[Slot], detour); }
    Fn get(void* object) const noexcept {
        auto target = (*static_cast<void***>(object))[Slot];
        for (const auto& r : records) if (r.target.load(std::memory_order_acquire)==target) return r.original;
        return nullptr;
    }
};
using namespace d3d9_abi;
static Create9 real_create9 = nullptr;
static Create9Ex real_create9ex = nullptr;
static Hooks<CreateDevice,d3d9_abi::create_device> create_hooks;
static Hooks<CreateDeviceEx,d3d9_abi::create_device_ex> create_ex_hooks;
static Hooks<Present,d3d9_abi::present> present_hooks;
static Hooks<PresentEx,d3d9_abi::present_ex> present_ex_hooks;
static Hooks<SwapPresent,d3d9_abi::swap_present> swap_hooks;
static Hooks<Reset,d3d9_abi::reset> reset_hooks;
static Hooks<ResetEx,d3d9_abi::reset_ex> reset_ex_hooks;

static void setup(IDirect3DDevice9* device) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    try {
        ComPtr<IDirect3DSurface9> surface;
        if (FAILED(device->GetBackBuffer(config.swapchain,0,D3DBACKBUFFER_TYPE_MONO,&surface))) return;
        D3DSURFACE_DESC desc{};
        if (FAILED(surface->GetDesc(&desc)) || desc.Width!=config.width || desc.Height!=config.height) return;
        capture = std::make_unique<Capture>(device,config);
        log("Capture initialized for the configured backbuffer; waiting for an NDI receiver");
    } catch (const std::exception& e) { ++counters.errors; log(e.what()); }
    catch (...) { ++counters.errors; log("Unexpected capture initialization failure"); }
}
static void frame(IDirect3DDevice9* device, IDirect3DSwapChain9* swap = nullptr) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    std::shared_lock lock(capture_mutex,std::try_to_lock);
    if (!lock.owns_lock()) { ++counters.dropped_busy; return; }
    if (!capture || capture->device()!=device) return;
    if (swap) {
        ComPtr<IDirect3DSwapChain9> selected;
        if (FAILED(device->GetSwapChain(capture->swapchain(),&selected)) || selected.Get()!=swap) return;
    }
    capture->frame();
}
struct Depth { unsigned& value; explicit Depth(unsigned& v):value(v){++value;} ~Depth(){--value;} };
static HRESULT STDMETHODCALLTYPE hook_present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    auto original=present_hooks.get(d); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) frame(d);
    return original(d,a,b,w,r);
}
static HRESULT STDMETHODCALLTYPE hook_present_ex(IDirect3DDevice9Ex* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r,DWORD f) {
    auto original=present_ex_hooks.get(d); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) frame(d);
    return original(d,a,b,w,r,f);
}
static HRESULT STDMETHODCALLTYPE hook_swap(IDirect3DSwapChain9* s,const RECT* a,const RECT* b,HWND w,const RGNDATA* r,DWORD f) {
    auto original=swap_hooks.get(s); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) { ComPtr<IDirect3DDevice9> d; if(SUCCEEDED(s->GetDevice(&d))) frame(d.Get(),s); }
    return original(s,a,b,w,r,f);
}
static void hook_swapchain(IDirect3DDevice9* d) {
    ComPtr<IDirect3DSwapChain9> swap;
    if (SUCCEEDED(d->GetSwapChain(config.swapchain,&swap)))
        swap_hooks.add_object(swap.Get(),reinterpret_cast<void*>(hook_swap));
}
template<typename Call> static HRESULT reset_device(IDirect3DDevice9* d, Call call) {
    Depth depth(reset_depth);
    if (reset_depth>1) return call();
    std::unique_lock lock(capture_mutex);
    const bool ours = capture && capture->device()==d;
    if (ours) capture.reset(); // Join/flush at the reset boundary, before releasing default-pool resources.
    const auto result=call();
    if (SUCCEEDED(result) && enabled.load(std::memory_order_acquire)) {
        ++counters.resets;
        hook_swapchain(d);
        if (ours || !capture) setup(d);
    }
    return result;
}
static HRESULT STDMETHODCALLTYPE hook_reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    auto original=reset_hooks.get(d); if(!original) return D3DERR_INVALIDCALL;
    return reset_device(d,[&]{return original(d,p);});
}
static HRESULT STDMETHODCALLTYPE hook_reset_ex(IDirect3DDevice9Ex* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m) {
    auto original=reset_ex_hooks.get(d); if(!original) return D3DERR_INVALIDCALL;
    return reset_device(d,[&]{return original(d,p,m);});
}
static void device_created(IDirect3DDevice9* d) {
    bool ok=present_hooks.add_object(d,reinterpret_cast<void*>(hook_present));
    ok=reset_hooks.add_object(d,reinterpret_cast<void*>(hook_reset)) && ok;
    ComPtr<IDirect3DDevice9Ex> ex;
    if (SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&ex)))) {
        ok=present_ex_hooks.add_object(ex.Get(),reinterpret_cast<void*>(hook_present_ex)) && ok;
        ok=reset_ex_hooks.add_object(ex.Get(),reinterpret_cast<void*>(hook_reset_ex)) && ok;
    }
    hook_swapchain(d);
    if (!ok) { ++counters.errors; log("D3D9 hook setup incomplete; capture disabled for this device"); return; }
    std::unique_lock lock(capture_mutex);
    // One explicitly selected stream in this first implementation.
    if (!capture) setup(d);
}
static HRESULT STDMETHODCALLTYPE hook_create_device(IDirect3D9* d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    auto original=create_hooks.get(d); if(!original) return D3DERR_INVALIDCALL;
    const auto result=original(d,a,t,w,f,p,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) device_created(*out);
    return result;
}
static HRESULT STDMETHODCALLTYPE hook_create_device_ex(IDirect3D9Ex* d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m,IDirect3DDevice9Ex** out) {
    auto original=create_ex_hooks.get(d); if(!original) return D3DERR_INVALIDCALL;
    const auto result=original(d,a,t,w,f,p,m,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) device_created(*out);
    return result;
}
static IDirect3D9* WINAPI hook_create9(UINT version) {
    auto d=real_create9(version);
    if(d && enabled.load()) create_hooks.add_object(d,reinterpret_cast<void*>(hook_create_device));
    return d;
}
static HRESULT WINAPI hook_create9ex(UINT version,IDirect3D9Ex** out) {
    const auto result=real_create9ex(version,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) {
        create_hooks.add_object(*out,reinterpret_cast<void*>(hook_create_device));
        create_ex_hooks.add_object(*out,reinterpret_cast<void*>(hook_create_device_ex));
    }
    return result;
}
static bool install() {
    static bool installed=false;
    if(installed) return true;
    auto status=MH_Initialize();
    if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) return false;
    auto d3d=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!d3d) return false;
    auto a=reinterpret_cast<void*>(GetProcAddress(d3d,"Direct3DCreate9"));
    auto b=reinterpret_cast<void*>(GetProcAddress(d3d,"Direct3DCreate9Ex"));
    if(!a || !b) return false;
    if(MH_CreateHook(a,reinterpret_cast<void*>(hook_create9),reinterpret_cast<void**>(&real_create9))!=MH_OK) return false;
    if(MH_CreateHook(b,reinterpret_cast<void*>(hook_create9ex),reinterpret_cast<void**>(&real_create9ex))!=MH_OK) return false;
    if(MH_EnableHook(a)!=MH_OK || MH_EnableHook(b)!=MH_OK) return false;
    installed=true;
    return true;
}
bool start_video(const Config& settings) {
    config = settings;
    if (!install()) return false;
    enabled.store(true, std::memory_order_release);
    return true;
}
void stop_video() {
    enabled.store(false, std::memory_order_release);
    std::unique_lock lock(capture_mutex);
    capture.reset();
}
bool video_running() { return enabled.load(std::memory_order_acquire); }
}
