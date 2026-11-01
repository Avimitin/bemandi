#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace bemandi {
inline constexpr unsigned audio_rate = 44100, audio_channels = 2;
inline constexpr unsigned audio_packet_frames = 441, audio_max_frames = 4096;
struct AudioBlock {
    int64_t pts = 0;
    uint64_t sequence = 0;
    unsigned frames = 0;
    std::array<float, audio_max_frames * audio_channels> samples{};
};
// One producer and one consumer. Storage is allocated/touched before hooking.
// The producer never waits and never overwrites a buffer the consumer owns.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional padding separates producer/consumer cache lines.
#endif
template<unsigned Capacity = 128> class AudioQueue {
public:
    bool push(const float* data, unsigned frames, int64_t pts, uint64_t sequence) noexcept {
        if (!data || !frames || frames > audio_max_frames) return false;
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == Capacity) return false;
        auto& block = blocks_[w % Capacity];
        block.pts = pts; block.sequence = sequence; block.frames = frames;
        std::memcpy(block.samples.data(), data, frames * audio_channels * sizeof(float));
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    const AudioBlock* front() const noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        return r == write_.load(std::memory_order_acquire) ? nullptr : &blocks_[r % Capacity];
    }
    void pop() noexcept { read_.fetch_add(1, std::memory_order_release); }
private:
    std::array<AudioBlock, Capacity> blocks_{};
    alignas(64) std::atomic<uint64_t> write_{};
    alignas(64) std::atomic<uint64_t> read_{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
