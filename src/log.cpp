#include "log.h"
#include <windows.h>
#include <mutex>
#include <cstdio>
#include <share.h>
namespace bemandi {
static std::mutex log_mutex;
static FILE* logfile = nullptr;
void open_log(const std::filesystem::path& path) {
    std::lock_guard lock(log_mutex);
    if (!logfile) logfile = _wfsopen(path.c_str(), L"a", _SH_DENYNO);
}
void log(const char* text) noexcept {
    std::lock_guard lock(log_mutex);
    if (logfile) { fprintf(logfile, "%s\n", text); fflush(logfile); }
    OutputDebugStringA(text); OutputDebugStringA("\n");
}
}
