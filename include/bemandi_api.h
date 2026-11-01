#pragma once
#include <stdint.h>
#include <wchar.h>

// start/stop are called outside DllMain. The module is retained until process exit.
struct BemandiStats {
    uint32_t size;
    uint32_t running;
    uint64_t presented;
    uint64_t captured;
    uint64_t dropped_busy;
    uint64_t skipped_cadence;
    uint64_t skipped_no_receiver;
    uint64_t completed;
    uint64_t bytes;
    uint64_t errors;
    uint64_t max_capture_us;
    uint64_t resets;
};
using BemandiStart = int(__cdecl*)(const wchar_t* ini_path);
using BemandiStop = void(__cdecl*)();
using BemandiGetStats = int(__cdecl*)(BemandiStats*);
