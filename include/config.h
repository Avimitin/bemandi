#pragma once
#include "audio.h"
#include <filesystem>
#include <string>
namespace bemandi {
enum class PixelFormat { uyvy, bgra };
struct Config {
    PixelFormat format = PixelFormat::uyvy;
    unsigned fps = 120, width = 1920, height = 1080, swapchain = 0;
    std::wstring ndi_runtime;
    std::string source_name = "IIDX Main";
    AudioConfig audio;
};
Config read_config(const std::filesystem::path& ini);
}
