#include "game_audio_snapshot.h"
#include "game_profiles.h"
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
int main() {
    const auto& p = bemandi::games::iidx34_c98e183e;
    constexpr uintptr_t base = 0x180000000, sound = 0x1000, graph = 0x2000, sub = 0x3000, tee = 0x4000;
    std::unordered_map<uintptr_t, uintptr_t> memory;
    auto read = [&](uintptr_t address) { auto it = memory.find(address); return it == memory.end() ? uintptr_t{0} : it->second; };
    REQUIRE(!bemandi::existing_audio_tee(base, p, read)); // No sound system yet.
    memory = {{base+p.sound_singleton_rva, base+p.singleton_vtable_rva},
        {base+p.sound_singleton_rva+p.singleton_sound_offset, sound}, {sound, base+p.sound_vtable_rva},
        {sound+p.sound_graph_offset, graph}, {graph, base+p.graph_vtable_rva},
        {graph+p.main_subgraph_offset, sub}, {sub+p.recording_tee_offset, tee}, {tee, base+p.tee_vtable_rva}};
    REQUIRE(bemandi::existing_audio_tee(base, p, read) == tee); // Constructor ran before plugin start.
    for (const auto& pair : memory) {
        const auto saved = pair.second;
        memory[pair.first] = 0;
        REQUIRE(!bemandi::existing_audio_tee(base, p, read)); // Every broken/unreadable link fails closed.
        memory[pair.first] = saved;
    }
    memory[tee] = 1;
    REQUIRE(!bemandi::existing_audio_tee(base, p, read));
    memory[tee] = base+p.tee_vtable_rva;
    unsigned passes = 0;
    auto changing = [&](uintptr_t address) {
        if (address == base+p.sound_singleton_rva && ++passes == 2) memory[graph+p.main_subgraph_offset] = 0;
        return read(address);
    };
    REQUIRE(!bemandi::existing_audio_tee(base, p, changing)); // Graph changes during snapshot.
    puts("Existing graph, uninitialized graph, bad layouts and concurrent replacement passed");
}
