#pragma once
#include "audio_queue.h"
#include <string>
#include <thread>

namespace bemandi {
struct AudioConfig {
    bool enabled = true;
    std::string source_name = "IIDX Audio";
};
struct AudioCounters {
    std::atomic<uint64_t> observed{}, callbacks{}, frames{}, sent{}, dropped{}, stale{}, invalid{}, discontinuities{}, errors{};
};
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    void start(const std::wstring& runtime, const std::string& name);
    void stop();
    // Game audio thread: fixed bound, no allocation, logging, waiting or SDK calls.
    void copy(const float* samples, unsigned bytes) noexcept;
    AudioCounters counters;
private:
    void work(std::wstring runtime, std::string name) noexcept;
    AudioQueue<> queue_;
    std::thread worker_;
    std::atomic<bool> stop_{true}, wanted_{false};
    std::atomic_flag producing_ = ATOMIC_FLAG_INIT;
    uint64_t sequence_ = 0;
};
// Game adapter owns process-lifetime capture storage so stop cannot free a buffer
// while a game callback is finishing. Calls serialized by plugin lifecycle mutex.
void start_game_audio(const AudioConfig& config, const std::wstring& runtime) noexcept;
void stop_game_audio() noexcept;
}
