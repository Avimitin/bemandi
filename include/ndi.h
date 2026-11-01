#pragma once
#include <windows.h>
#include <string>
#include <Processing.NDI.Lib.h>
#include <Processing.NDI.DynamicLoad.h>

namespace bemandi {
class NdiRuntime {
public:
    explicit NdiRuntime(const std::wstring& path = {});
    ~NdiRuntime();
    const NDIlib_v6* api = nullptr;
private:
    HMODULE module_ = nullptr;
};
class NdiSender {
public:
    NdiSender(const std::wstring& runtime, const std::string& name, unsigned fps);
    ~NdiSender();
    bool connected() const;
    // Async retains data until the next send or flush returns.
    void send(void* data, unsigned width, unsigned height, int pitch, int64_t pts, bool uyvy);
    void flush();
private:
    NdiRuntime runtime_;
    NDIlib_send_instance_t sender_ = nullptr;
    unsigned fps_;
};
class NdiAudioSender {
public:
    NdiAudioSender(const std::wstring& runtime, const std::string& name);
    ~NdiAudioSender();
    bool connected() const;
    void send(float* planar, unsigned frames, int64_t pts);
private:
    NdiRuntime runtime_;
    NDIlib_send_instance_t sender_ = nullptr;
};
}
