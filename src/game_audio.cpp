#include "audio.h"
#include "game_profiles.h"
#include "game_audio_snapshot.h"
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace bemandi {
void log(const char*) noexcept;
namespace {
const GameProfile* profile = nullptr;
unsigned char* game_base = nullptr;
GraphCreate real_graph_create = nullptr;
TeeRead real_tee_read = nullptr;
std::atomic<void*> selected_tee{};
std::atomic<AudioCapture*> active_capture{};
std::atomic<uint64_t> graph_generation{};
// Deliberately retained with the pinned hook code. No callback can observe freed storage.
AudioCapture* capture = nullptr;

std::string file_hash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open game DLL for audio build verification");
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("Cannot initialize game SHA256 verification");
    struct Cleanup { BCRYPT_HASH_HANDLE value; ~Cleanup() { BCryptDestroyHash(value); } } cleanup{hash};
    std::array<unsigned char, 65536> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (BCryptHashData(hash, buffer.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
            throw std::runtime_error("Game SHA256 verification failed");
    }
    if (!file.eof()) throw std::runtime_error("Cannot read game DLL for audio verification");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("Cannot finish game SHA256 verification");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
void* pointer_at(void* object, size_t offset) noexcept {
    return *reinterpret_cast<void**>(static_cast<unsigned char*>(object) + offset);
}
void* __fastcall hook_graph_create(void* graph, int channels, int bits) {
    graph_generation.fetch_add(1, std::memory_order_acq_rel);
    selected_tee.store(nullptr, std::memory_order_release);
    auto result = real_graph_create(graph, channels, bits);
    auto subgraph = pointer_at(graph, profile->main_subgraph_offset);
    auto tee = subgraph ? pointer_at(subgraph, profile->recording_tee_offset) : nullptr;
    if (tee && pointer_at(tee, 0) == game_base + profile->tee_vtable_rva) {
        selected_tee.store(tee, std::memory_order_release);
        log("Game audio tap selected: main stereo mixer, before recorder encoding and cabinet master gain");
    } else log("Game audio graph did not match verified layout; audio capture inactive");
    graph_generation.fetch_add(1, std::memory_order_release);
    return result;
}
void attach_existing_graph() noexcept {
    const auto generation = graph_generation.load(std::memory_order_acquire);
    if (generation & 1) return; // A hooked constructor will select it when complete.
    const auto read = [](uintptr_t address) noexcept -> uintptr_t {
        uintptr_t value = 0;
        SIZE_T copied = 0;
        if (!address || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            &value, sizeof(value), &copied) || copied != sizeof(value)) return 0;
        return value;
    };
    const auto tee = existing_audio_tee(reinterpret_cast<uintptr_t>(game_base), *profile, read);
    if (!tee || generation != graph_generation.load(std::memory_order_acquire)) return;
    void* empty = nullptr;
    if (selected_tee.compare_exchange_strong(empty, reinterpret_cast<void*>(tee), std::memory_order_release))
        log("Game audio tap selected: existing main stereo graph (created before Spice SDK initialization)");
}
uint32_t __fastcall hook_tee_read(void* tee, float* samples, unsigned bytes) {
    const auto result = real_tee_read(tee, samples, bytes);
    if (tee == selected_tee.load(std::memory_order_acquire)) {
        if (auto sink = active_capture.load(std::memory_order_acquire)) sink->copy(samples, bytes);
    }
    return result;
}
bool install_audio_hooks() {
    if (real_graph_create && real_tee_read) return true;
    auto game = GetModuleHandleW(L"bm2dx.dll");
    if (!game) { log("Audio disabled: bm2dx.dll is not loaded (video remains available)"); return false; }
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(game, path, 32768);
    if (!length || length >= 32768) return false;
    profile = find_game_profile(file_hash(path));
    if (!profile) { log("Audio disabled: unrecognized game DLL SHA256; add a verified game profile"); return false; }
    game_base = reinterpret_cast<unsigned char*>(game);
    auto graph = game_base + profile->graph_create_rva;
    auto tee = game_base + profile->tee_read_rva;
    if (std::memcmp(graph, profile->graph_prologue.data(), profile->graph_prologue.size()) ||
        std::memcmp(tee, profile->tee_prologue.data(), profile->tee_prologue.size())) {
        log("Audio disabled: hook entry bytes differ from verified build"); return false;
    }
    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(graph, reinterpret_cast<void*>(hook_graph_create), reinterpret_cast<void**>(&real_graph_create)) != MH_OK) return false;
    if (MH_CreateHook(tee, reinterpret_cast<void*>(hook_tee_read), reinterpret_cast<void**>(&real_tee_read)) != MH_OK) {
        MH_RemoveHook(graph); real_graph_create = nullptr; return false;
    }
    // Queue both so no constructor can run with only half of the adapter installed.
    status = MH_QueueEnableHook(tee);
    if (status == MH_OK) status = MH_QueueEnableHook(graph);
    if (status == MH_OK) status = MH_ApplyQueued();
    if (status != MH_OK) {
        MH_DisableHook(graph); MH_DisableHook(tee);
        MH_RemoveHook(graph); MH_RemoveHook(tee);
        real_graph_create = nullptr; real_tee_read = nullptr; return false;
    }
    log(profile->name);
    log("Game audio hooks installed");
    // Spice can start ASIO before invoking plugin SDK entry points. Waiting only
    // for future graph creation leaves an advertised source with no PCM forever.
    attach_existing_graph();
    if (!selected_tee.load(std::memory_order_acquire)) log("No existing audio graph matched; waiting for sound graph creation");
    return true;
}
}
void start_game_audio(const AudioConfig& config, const std::wstring& runtime) noexcept {
    if (!config.enabled) { log("Separate NDI audio disabled in configuration"); return; }
    try {
        if (!install_audio_hooks()) { log("Native audio tap unavailable; video capture can continue"); return; }
        if (!capture) capture = new AudioCapture;
        capture->start(runtime, config.source_name);
        active_capture.store(capture, std::memory_order_release);
    } catch (const std::exception& e) { log(e.what()); }
    catch (...) { log("Unexpected game audio initialization failure"); }
}
void stop_game_audio() noexcept {
    active_capture.store(nullptr, std::memory_order_release);
    if (capture) capture->stop();
}
}
