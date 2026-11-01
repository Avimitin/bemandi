#include "audio.h"
#include "ndi.h"
#include "timing.h"
#include "core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

namespace bemandi {
void log(const char*) noexcept;
AudioCapture::AudioCapture() { (void)timestamp(); }
AudioCapture::~AudioCapture() { stop(); }
void AudioCapture::start(const std::wstring& runtime, const std::string& name) {
    stop();
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread(&AudioCapture::work, this, runtime, name);
}
void AudioCapture::stop() {
    wanted_.store(false, std::memory_order_release);
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}
void AudioCapture::copy(const float* samples, unsigned bytes) noexcept {
    ++counters.observed;
    if (!wanted_.load(std::memory_order_acquire)) return;
    ++counters.callbacks;
    if (!samples || !bytes || bytes % (sizeof(float) * audio_channels) || bytes > audio_max_frames * audio_channels * sizeof(float)) {
        ++counters.invalid; return;
    }
    // Defensive guard if a future driver calls from multiple threads. No spinning.
    if (producing_.test_and_set(std::memory_order_acquire)) { ++counters.dropped; return; }
    const unsigned frames = bytes / (sizeof(float) * audio_channels);
    const auto sequence = sequence_;
    sequence_ += frames;
    if (queue_.push(samples, frames, timestamp(), sequence)) counters.frames += frames;
    else ++counters.dropped;
    producing_.clear(std::memory_order_release);
}
void AudioCapture::work(std::wstring runtime, std::string name) noexcept {
    try {
        NdiAudioSender sender(runtime, name);
        log("NDI audio sender ready: separate stereo 44100 Hz PCM source");
        std::array<float, audio_packet_frames * audio_channels> planar{};
        unsigned filled = 0;
        uint64_t expected = 0, anchor_sequence = 0;
        int64_t packet_pts = 0, anchor_pts = 0, last_block_pts = 0, last_packet_end = 0;
        auto report = timestamp() + 50000000, check = int64_t{0};
        bool connected = false;
        int connections = 0;
        while (!stop_.load(std::memory_order_acquire)) {
            const auto now = timestamp();
            if (now >= check) {
                connections = sender.connections();
                connected = connections > 0;
                wanted_.store(connected, std::memory_order_release);
                check = now + 2500000;
            }
            if (now >= report) {
                char message[512];
                sprintf_s(message, "bemandi audio: receivers=%d tap_callbacks=%llu callbacks=%llu frames=%llu packets=%llu dropped=%llu stale=%llu invalid=%llu discontinuities=%llu",
                    connections, counters.observed.load(), counters.callbacks.load(), counters.frames.load(), counters.sent.load(), counters.dropped.load(),
                    counters.stale.load(), counters.invalid.load(), counters.discontinuities.load());
                log(message); report = now + 50000000;
            }
            const auto* block = queue_.front();
            if (!block) { Sleep(1); continue; }
            // Never play a growing backlog after a stalled receiver. Resume at current time.
            if (!connected || now - block->pts > 1000000) {
                filled = 0; anchor_pts = 0; ++counters.stale; queue_.pop(); continue;
            }
            const auto predicted = anchor_pts + to_100ns(static_cast<int64_t>(block->sequence - anchor_sequence), audio_rate);
            // Audio hardware and QPC are separate clocks. Bound long-session drift
            // without resampling: discontinuities re-anchor instead of accumulating.
            if (!anchor_pts || block->sequence != expected || block->pts - last_block_pts > 1000000 ||
                std::abs(block->pts - predicted) > 300000) {
                if (anchor_pts) ++counters.discontinuities;
                filled = 0; anchor_pts = std::max(block->pts, last_packet_end); anchor_sequence = block->sequence;
            }
            expected = block->sequence + block->frames;
            last_block_pts = block->pts;
            for (unsigned i = 0; i < block->frames; ++i) {
                if (!filled) packet_pts = anchor_pts + to_100ns(static_cast<int64_t>(block->sequence + i - anchor_sequence), audio_rate);
                for (unsigned c = 0; c < audio_channels; ++c) {
                    const auto sample = block->samples[i * audio_channels + c];
                    // Native recorder also limits at integer conversion. No resampling or lossy codec.
                    planar[c * audio_packet_frames + filled] = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
                }
                if (++filled == audio_packet_frames) {
                    sender.send(planar.data(), audio_packet_frames, packet_pts);
                    last_packet_end = packet_pts + to_100ns(audio_packet_frames, audio_rate);
                    ++counters.sent; filled = 0;
                }
            }
            queue_.pop();
        }
    } catch (const std::exception& e) { ++counters.errors; log(e.what()); }
    catch (...) { ++counters.errors; log("Unexpected audio sender failure"); }
    wanted_.store(false, std::memory_order_release);
}
}
