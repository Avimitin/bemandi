#pragma once
#include "game_profile.h"

namespace bemandi {
struct AudioGraphSnapshot {
    uintptr_t sound = 0, graph = 0, subgraph = 0, tee = 0;
    bool operator==(const AudioGraphSnapshot&) const = default;
};
// Startup-only observer. Reader returns zero for unreadable addresses. Never
// invokes game constructors or registers callbacks. No traversal on audio thread.
template<class Read> AudioGraphSnapshot audio_graph_snapshot(uintptr_t base, const GameProfile& p, Read read) {
    AudioGraphSnapshot s;
    const auto singleton = base + p.sound_singleton_rva;
    if (read(singleton) != base + p.singleton_vtable_rva) return {};
    s.sound = read(singleton + p.singleton_sound_offset);
    if (!s.sound || read(s.sound) != base + p.sound_vtable_rva) return {};
    s.graph = read(s.sound + p.sound_graph_offset);
    if (!s.graph || read(s.graph) != base + p.graph_vtable_rva) return {};
    s.subgraph = read(s.graph + p.main_subgraph_offset);
    if (!s.subgraph) return {};
    s.tee = read(s.subgraph + p.recording_tee_offset);
    if (!s.tee || read(s.tee) != base + p.tee_vtable_rva) return {};
    return s;
}
template<class Read> uintptr_t existing_audio_tee(uintptr_t base, const GameProfile& p, Read read) {
    const auto first = audio_graph_snapshot(base, p, read);
    if (!first.tee) return 0;
    const auto second = audio_graph_snapshot(base, p, read);
    return first == second ? first.tee : 0;
}
}
