#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace bemandi {
struct GameProfile {
    const char* name;
    const char* sha256;
    size_t graph_create_rva, tee_read_rva, tee_vtable_rva;
    size_t main_subgraph_offset, recording_tee_offset;
    std::array<unsigned char, 16> graph_prologue, tee_prologue;
};
// ABI common to the supported profile. A changed signature needs a new adapter,
// not just new addresses. The tee reads stereo float bytes, not a sample count.
using GraphCreate = void* (__fastcall*)(void*, int output_channels, int output_bits);
using TeeRead = uint32_t (__fastcall*)(void*, float*, unsigned bytes);
}
