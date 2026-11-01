#include "config.h"
#include <windows.h>
#include <stdexcept>
namespace bemandi {
static std::wstring read_string(const wchar_t* ini,const wchar_t* key,const wchar_t* fallback,const wchar_t* section=L"video") {
    wchar_t value[32768]{};
    const auto length=GetPrivateProfileStringW(section,key,fallback,value,32768,ini);
    if(length>=32767) throw std::runtime_error("INI value too long");
    return value;
}
Config read_config(const std::filesystem::path& ini) {
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
    c.audio.enabled=GetPrivateProfileIntW(L"audio",L"enabled",1,path)!=0;
    const auto audio_name=read_string(path,L"source_name",L"IIDX Audio",L"audio");
    const int audio_bytes=WideCharToMultiByte(CP_UTF8,0,audio_name.c_str(),static_cast<int>(audio_name.size()),nullptr,0,nullptr,nullptr);
    if(audio_bytes<1 || audio_bytes>200) throw std::runtime_error("Audio source_name must contain 1-200 UTF-8 bytes");
    c.audio.source_name.resize(audio_bytes);
    WideCharToMultiByte(CP_UTF8,0,audio_name.c_str(),static_cast<int>(audio_name.size()),c.audio.source_name.data(),audio_bytes,nullptr,nullptr);
    if(c.audio.enabled && c.audio.source_name==c.source_name) throw std::runtime_error("Audio and video source names must differ");
    return c;
}
}
