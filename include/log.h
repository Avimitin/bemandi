#pragma once
#include <filesystem>
namespace bemandi {
void open_log(const std::filesystem::path& path);
void log(const char* text) noexcept;
}
