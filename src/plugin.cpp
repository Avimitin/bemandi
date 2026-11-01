#include "capture.h"
#include "d3d9_hooks.h"
#include "log.h"
#include <spicesdk.h>
#include <filesystem>
#include <mutex>
namespace bemandi {
static std::mutex lifecycle_mutex;
static HMODULE self_module = nullptr;
static SPICE_SDK_V0 spice{};
}

extern "C" __declspec(dllexport) int __cdecl bemandi_start(const wchar_t* ini_path) {
    using namespace bemandi;
    std::lock_guard lifecycle(lifecycle_mutex);
    if(video_running()) return 1;
    try {
        wchar_t module_path[32768]{};
        if(!GetModuleFileNameW(self_module,module_path,32768)) return 0;
        auto folder=std::filesystem::path(module_path).parent_path();
        open_log(folder/L"bemandi.log");
        const auto config=read_config(ini_path ? std::filesystem::absolute(ini_path) : folder/L"bemandi.ini");
        // Hooks are process-lifetime and remain transparent after stop. Never unload their code.
        HMODULE retained=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&bemandi_start),&retained)) return 0;
        if(!start_video(config)) { log("Cannot install D3D9 creation hooks"); return 0; }
        start_game_audio(config.audio, config.ndi_runtime);
        log("bemandi started: standard NDI video + optional separate audio; waiting for a newly created D3D9 device");
        return 1;
    } catch(const std::exception& e) { log(e.what()); return 0; }
    catch(...) { log("Unexpected initialization failure"); return 0; }
}
extern "C" __declspec(dllexport) void __cdecl bemandi_stop() {
    using namespace bemandi;
    std::lock_guard lifecycle(lifecycle_mutex);
    stop_game_audio();
    stop_video();
    log("bemandi stopped; presentation hooks now pass through");
}
extern "C" __declspec(dllexport) int __cdecl bemandi_get_stats(BemandiStats* out) {
    using namespace bemandi;
    if(!out || out->size<sizeof(BemandiStats)) return 0;
    out->running=video_running();
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
