#include "ndi.h"
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace bemandi {
NdiRuntime::NdiRuntime(const std::wstring& path) {
    std::vector<std::wstring> candidates;
    if (!path.empty()) {
        if (!std::filesystem::path(path).is_absolute())
            throw std::runtime_error("ndi_runtime must be an absolute DLL path");
        candidates.push_back(path);
    } else {
        wchar_t buffer[32768]{};
        DWORD n = GetEnvironmentVariableW(L"NDI_RUNTIME_DIR_V6", buffer, 32768);
        if (n && n < 32768) candidates.push_back((std::filesystem::path(buffer) / L"Processing.NDI.Lib.x64.dll").wstring());
        n = GetEnvironmentVariableW(L"ProgramFiles", buffer, 32768);
        if (n && n < 32768) {
            auto root = std::filesystem::path(buffer) / L"NDI";
            candidates.push_back((root / L"NDI 6 Runtime/v6/Processing.NDI.Lib.x64.dll").wstring());
            candidates.push_back((root / L"NDI 6 Tools/Runtime/Processing.NDI.Lib.x64.dll").wstring());
        }
    }
    for (const auto& candidate : candidates) {
        if (!std::filesystem::path(candidate).is_absolute()) continue;
        module_ = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_) break;
    }
    if (!module_) throw std::runtime_error("NDI 6 runtime not found; install the runtime or set ndi_runtime to its absolute DLL path");
    auto load = reinterpret_cast<const NDIlib_v6*(__cdecl*)()>(GetProcAddress(module_, "NDIlib_v6_load"));
    api = load ? load() : nullptr;
    if (!api || !api->initialize()) {
        FreeLibrary(module_); module_ = nullptr; api = nullptr;
        throw std::runtime_error("NDI runtime initialization failed");
    }
}
NdiRuntime::~NdiRuntime() {
    if (api) api->destroy();
    if (module_) FreeLibrary(module_);
}
NdiSender::NdiSender(const std::wstring& runtime, const std::string& name, unsigned fps)
    : runtime_(runtime), fps_(fps) {
    NDIlib_send_create_t desc{};
    desc.p_ndi_name = name.c_str();
    desc.clock_video = false;
    desc.clock_audio = false;
    sender_ = runtime_.api->send_create(&desc);
    if (!sender_) throw std::runtime_error("NDI sender creation failed");
}
NdiSender::~NdiSender() {
    if (sender_) { flush(); runtime_.api->send_destroy(sender_); }
}
bool NdiSender::connected() const { return runtime_.api->send_get_no_connections(sender_, 0) > 0; }
void NdiSender::flush() { runtime_.api->send_send_video_async_v2(sender_, nullptr); }
void NdiSender::send(void* data, unsigned width, unsigned height, int pitch, int64_t pts, bool uyvy) {
    NDIlib_video_frame_v2_t frame{};
    frame.xres = static_cast<int>(width);
    frame.yres = static_cast<int>(height);
    // Game display capture is opaque; ignore incidental backbuffer alpha.
    frame.FourCC = uyvy ? NDIlib_FourCC_type_UYVY : NDIlib_FourCC_type_BGRX;
    frame.frame_rate_N = static_cast<int>(fps_);
    frame.frame_rate_D = 1;
    frame.picture_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    frame.timecode = pts;
    frame.p_data = static_cast<uint8_t*>(data);
    frame.line_stride_in_bytes = pitch;
    runtime_.api->send_send_video_async_v2(sender_, &frame);
}
}
