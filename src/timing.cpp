#include "timing.h"
#include "core.h"
#include <windows.h>
namespace bemandi {
int64_t ticks() noexcept { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
int64_t frequency() noexcept {
    static const int64_t value = [] { LARGE_INTEGER n; QueryPerformanceFrequency(&n); return n.QuadPart; }();
    return value;
}
int64_t clock_epoch() noexcept {
    static const int64_t epoch = [] {
        FILETIME time{}; GetSystemTimeAsFileTime(&time);
        return static_cast<int64_t>((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime)
            - 116444736000000000LL - to_100ns(ticks(), frequency());
    }();
    return epoch;
}
int64_t timestamp() noexcept { return clock_epoch() + to_100ns(ticks(), frequency()); }
}
