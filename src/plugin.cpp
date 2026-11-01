#include "capture.h"
#include <MinHook.h>
#include <spicesdk.h>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>

namespace bemandi {
static Config config;
static std::unique_ptr<Capture> capture;
static std::shared_mutex capture_mutex;
static std::mutex log_mutex, hooks_mutex, lifecycle_mutex;
static std::atomic<bool> enabled{false};
static FILE* logfile = nullptr;
static HMODULE self_module = nullptr;
static SPICE_SDK_V0 spice{};
static thread_local unsigned present_depth = 0, reset_depth = 0;

void log(const char* text) noexcept {
    std::lock_guard lock(log_mutex);
    if (logfile) { fprintf(logfile,"%s\n",text); fflush(logfile); }
    OutputDebugStringA(text); OutputDebugStringA("\n");
}
template<typename Fn> struct Hooks {
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
    Fn get(void* object, unsigned slot) const noexcept {
        auto target = (*static_cast<void***>(object))[slot];
        for (const auto& r : records) if (r.target.load(std::memory_order_acquire)==target) return r.original;
        return nullptr;
    }
};
using Create9 = IDirect3D9*(WINAPI*)(UINT);
using Create9Ex = HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
using CreateDevice = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using CreateDeviceEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using PresentEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using SwapPresent = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using ResetEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
static Create9 real_create9 = nullptr;
static Create9Ex real_create9ex = nullptr;
static Hooks<CreateDevice> create_hooks;
static Hooks<CreateDeviceEx> create_ex_hooks;
static Hooks<Present> present_hooks;
static Hooks<PresentEx> present_ex_hooks;
static Hooks<SwapPresent> swap_hooks;
static Hooks<Reset> reset_hooks;
static Hooks<ResetEx> reset_ex_hooks;

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
    auto original=present_hooks.get(d,17); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) frame(d);
    return original(d,a,b,w,r);
}
static HRESULT STDMETHODCALLTYPE hook_present_ex(IDirect3DDevice9Ex* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r,DWORD f) {
    auto original=present_ex_hooks.get(d,121); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) frame(d);
    return original(d,a,b,w,r,f);
}
static HRESULT STDMETHODCALLTYPE hook_swap(IDirect3DSwapChain9* s,const RECT* a,const RECT* b,HWND w,const RGNDATA* r,DWORD f) {
    auto original=swap_hooks.get(s,3); if (!original) return D3DERR_INVALIDCALL;
    Depth depth(present_depth);
    if (present_depth==1) { ComPtr<IDirect3DDevice9> d; if(SUCCEEDED(s->GetDevice(&d))) frame(d.Get(),s); }
    return original(s,a,b,w,r,f);
}
static void hook_swapchain(IDirect3DDevice9* d) {
    ComPtr<IDirect3DSwapChain9> swap;
    if (SUCCEEDED(d->GetSwapChain(config.swapchain,&swap)))
        swap_hooks.add((*reinterpret_cast<void***>(swap.Get()))[3],reinterpret_cast<void*>(hook_swap));
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
    auto original=reset_hooks.get(d,16); if(!original) return D3DERR_INVALIDCALL;
    return reset_device(d,[&]{return original(d,p);});
}
static HRESULT STDMETHODCALLTYPE hook_reset_ex(IDirect3DDevice9Ex* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m) {
    auto original=reset_ex_hooks.get(d,132); if(!original) return D3DERR_INVALIDCALL;
    return reset_device(d,[&]{return original(d,p,m);});
}
static void device_created(IDirect3DDevice9* d) {
    auto vt=*reinterpret_cast<void***>(d);
    bool ok=present_hooks.add(vt[17],reinterpret_cast<void*>(hook_present));
    ok=reset_hooks.add(vt[16],reinterpret_cast<void*>(hook_reset)) && ok;
    ComPtr<IDirect3DDevice9Ex> ex;
    if (SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&ex)))) {
        auto ext=*reinterpret_cast<void***>(ex.Get());
        ok=present_ex_hooks.add(ext[121],reinterpret_cast<void*>(hook_present_ex)) && ok;
        ok=reset_ex_hooks.add(ext[132],reinterpret_cast<void*>(hook_reset_ex)) && ok;
    }
    hook_swapchain(d);
    if (!ok) { ++counters.errors; log("D3D9 hook setup incomplete; capture disabled for this device"); return; }
    std::unique_lock lock(capture_mutex);
    // One explicitly selected stream in this first implementation.
    if (!capture) setup(d);
}
static HRESULT STDMETHODCALLTYPE hook_create_device(IDirect3D9* d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    auto original=create_hooks.get(d,16); if(!original) return D3DERR_INVALIDCALL;
    const auto result=original(d,a,t,w,f,p,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) device_created(*out);
    return result;
}
static HRESULT STDMETHODCALLTYPE hook_create_device_ex(IDirect3D9Ex* d,UINT a,D3DDEVTYPE t,HWND w,DWORD f,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m,IDirect3DDevice9Ex** out) {
    auto original=create_ex_hooks.get(d,20); if(!original) return D3DERR_INVALIDCALL;
    const auto result=original(d,a,t,w,f,p,m,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) device_created(*out);
    return result;
}
static IDirect3D9* WINAPI hook_create9(UINT version) {
    auto d=real_create9(version);
    if(d && enabled.load()) create_hooks.add((*reinterpret_cast<void***>(d))[16],reinterpret_cast<void*>(hook_create_device));
    return d;
}
static HRESULT WINAPI hook_create9ex(UINT version,IDirect3D9Ex** out) {
    const auto result=real_create9ex(version,out);
    if(SUCCEEDED(result) && out && *out && enabled.load()) {
        auto vt=*reinterpret_cast<void***>(*out);
        create_hooks.add(vt[16],reinterpret_cast<void*>(hook_create_device));
        create_ex_hooks.add(vt[20],reinterpret_cast<void*>(hook_create_device_ex));
    }
    return result;
}
static std::wstring read_string(const wchar_t* ini,const wchar_t* key,const wchar_t* fallback) {
    wchar_t value[32768]{};
    const auto length=GetPrivateProfileStringW(L"video",key,fallback,value,32768,ini);
    if(length>=32767) throw std::runtime_error("INI value too long");
    return value;
}
static Config read_config(const std::filesystem::path& ini) {
    Config c;
    if (!std::filesystem::exists(ini)) throw std::runtime_error("bemandi.ini not found; copy config/bemandi.ini next to the DLL");
    const auto path=ini.c_str();
    c.fps=GetPrivateProfileIntW(L"video",L"fps",120,path);
    c.width=GetPrivateProfileIntW(L"video",L"width",1920,path);
    c.height=GetPrivateProfileIntW(L"video",L"height",1080,path);
    c.swapchain=GetPrivateProfileIntW(L"video",L"swapchain",0,path);
    if(c.fps<1 || c.fps>240 || c.width<2 || c.width>7680 || c.width%2 || c.height<2 || c.height>4320 || c.swapchain>3)
        throw std::runtime_error("Invalid fps, dimensions or swapchain in INI");
    const auto format=read_string(path,L"format",L"uyvy");
    if(format==L"uyvy") c.format=PixelFormat::uyvy;
    else if(format==L"bgra") c.format=PixelFormat::bgra;
    else throw std::runtime_error("format must be uyvy or bgra");
    c.ndi_runtime=read_string(path,L"ndi_runtime",L"");
    const auto name=read_string(path,L"source_name",L"IIDX Main");
    const int bytes=WideCharToMultiByte(CP_UTF8,0,name.c_str(),static_cast<int>(name.size()),nullptr,0,nullptr,nullptr);
    if(bytes<1 || bytes>200) throw std::runtime_error("source_name must contain 1-200 UTF-8 bytes");
    c.source_name.resize(bytes);
    WideCharToMultiByte(CP_UTF8,0,name.c_str(),static_cast<int>(name.size()),c.source_name.data(),bytes,nullptr,nullptr);
    return c;
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
}

extern "C" __declspec(dllexport) int __cdecl bemandi_start(const wchar_t* ini_path) {
    using namespace bemandi;
    std::lock_guard lifecycle(lifecycle_mutex);
    if(enabled.load()) return 1;
    try {
        wchar_t module_path[32768]{};
        if(!GetModuleFileNameW(self_module,module_path,32768)) return 0;
        auto folder=std::filesystem::path(module_path).parent_path();
        if(!logfile) _wfopen_s(&logfile,(folder/L"bemandi.log").c_str(),L"a");
        config=read_config(ini_path ? std::filesystem::absolute(ini_path) : folder/L"bemandi.ini");
        // Hooks are process-lifetime and remain transparent after stop. Never unload their code.
        HMODULE retained=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&bemandi_start),&retained)) return 0;
        if(!install()) { log("Cannot install D3D9 creation hooks"); return 0; }
        enabled.store(true,std::memory_order_release);
        log("bemandi started: standard NDI video only; waiting for a newly created D3D9 device");
        return 1;
    } catch(const std::exception& e) { log(e.what()); return 0; }
    catch(...) { log("Unexpected initialization failure"); return 0; }
}
extern "C" __declspec(dllexport) void __cdecl bemandi_stop() {
    using namespace bemandi;
    std::lock_guard lifecycle(lifecycle_mutex);
    enabled.store(false,std::memory_order_release);
    std::unique_lock lock(capture_mutex);
    capture.reset();
    log("bemandi stopped; presentation hooks now pass through");
}
extern "C" __declspec(dllexport) int __cdecl bemandi_get_stats(BemandiStats* out) {
    using namespace bemandi;
    if(!out || out->size<sizeof(BemandiStats)) return 0;
    out->running=enabled.load();
#define COPY_COUNTER(n) out->n=counters.n.load(std::memory_order_relaxed)
    COPY_COUNTER(presented); COPY_COUNTER(captured); COPY_COUNTER(dropped_busy); COPY_COUNTER(skipped_cadence);
    COPY_COUNTER(skipped_no_receiver); COPY_COUNTER(completed); COPY_COUNTER(bytes); COPY_COUNTER(errors);
    COPY_COUNTER(max_capture_us); COPY_COUNTER(resets);
#undef COPY_COUNTER
    return 1;
}
SPICE_SDK_ENTRY_POINT spice_sdk_entry_point(spice_sdk_init_func* init) {
    bemandi::spice.size=sizeof(bemandi::spice);
    if(!init || init(0,bemandi_stop,&bemandi::spice)!=SPICE_SDK_STATUS_SUCCESS) return 0;
    return bemandi_start(nullptr);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { bemandi::self_module=module; DisableThreadLibraryCalls(module); }
    // No thread creation/join, SDK calls, COM work, or hook setup under the loader lock.
    return TRUE;
}
