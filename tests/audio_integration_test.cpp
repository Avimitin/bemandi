#include "audio.h"
#include "ndi.h"
#include "timing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace bemandi { void log(const char* message) noexcept { fprintf(stderr, "%s\n", message); } }
int main() {
    try {
        auto capture = std::make_unique<bemandi::AudioCapture>();
        const auto name = "Bemandi-Audio-Test-" + std::to_string(GetCurrentProcessId());
        capture->start({}, name);
        bemandi::NdiRuntime runtime;
        const auto api = runtime.api;
        NDIlib_find_create_t desc{};
        auto finder = api->find_create_v2(&desc);
        if (!finder) throw std::runtime_error("finder failed");
        NDIlib_recv_instance_t receiver = nullptr;
        for (unsigned tries = 0; !receiver && tries < 100; ++tries) {
            api->find_wait_for_sources(finder, 100);
            uint32_t count = 0;
            const auto sources = api->find_get_current_sources(finder, &count);
            for (uint32_t i = 0; i < count; ++i) {
                if (std::string(sources[i].p_ndi_name).find(name) == std::string::npos) continue;
                NDIlib_recv_create_v3_t receive{};
                receive.source_to_connect_to = sources[i];
                receive.bandwidth = NDIlib_recv_bandwidth_audio_only;
                receiver = api->recv_create_v3(&receive);
                break;
            }
        }
        if (!receiver) throw std::runtime_error("audio source was not discovered");
        std::atomic<bool> done{false};
        std::thread producer([&] {
            uint64_t sequence = 0;
            auto start = bemandi::ticks();
            bool paused = false;
            const unsigned sizes[]{16,32,128,441,1024};
            unsigned block = 0;
            std::array<float, 2048> samples{};
            while (!done.load()) {
                if (!paused && sequence > bemandi::audio_rate * 2) {
                    const auto before = bemandi::ticks();
                    Sleep(200); // Simulate a scene/audio-device pause without a catch-up burst.
                    start += bemandi::ticks() - before;
                    paused = true;
                }
                const auto target = start + static_cast<int64_t>(sequence) * bemandi::frequency() / bemandi::audio_rate;
                if (bemandi::ticks() < target) { Sleep(1); continue; }
                const auto frames = sizes[block++ % 5];
                for (unsigned i = 0; i < frames; ++i) {
                    const float value = static_cast<float>(0.5 * std::sin(6.283185307179586 * 1000 * static_cast<double>(sequence+i) / bemandi::audio_rate));
                    samples[i*2] = value; samples[i*2+1] = -value * 0.5f;
                }
                capture->copy(samples.data(), frames * 2 * sizeof(float));
                sequence += frames;
            }
        });
        unsigned received = 0, bad = 0;
        int64_t previous = 0;
        float peak = 0;
        const auto end = bemandi::timestamp() + 60000000;
        while (bemandi::timestamp() < end) {
            NDIlib_audio_frame_v2_t audio{};
            if (api->recv_capture_v2(receiver, nullptr, &audio, nullptr, 100) != NDIlib_frame_type_audio) continue;
            ++received;
            if (audio.sample_rate != 44100 || audio.no_channels != 2 || audio.no_samples != 441 || audio.timecode <= previous) ++bad;
            if (std::abs(audio.timecode - bemandi::timestamp()) > 10000000) ++bad;
            previous = audio.timecode;
            if (audio.no_channels == 2) {
                const auto right = reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(audio.p_data) + audio.channel_stride_in_bytes);
                for (int i = 0; i < audio.no_samples; ++i) {
                    if (!std::isfinite(audio.p_data[i]) || std::abs(right[i] + audio.p_data[i]*0.5f) > 0.001f) ++bad;
                    peak = std::max(peak, std::abs(audio.p_data[i]));
                }
            }
            api->recv_free_audio_v2(receiver, &audio);
        }
        done.store(true); producer.join();
        api->recv_destroy(receiver); api->find_destroy(finder);
        capture->stop();
        // Start/stop is also used by plugin lifecycle; validate repeat initialization.
        capture->start({}, name); Sleep(100); capture->stop();
        printf("{\"received\":%u,\"bad\":%u,\"peak\":%.6f,\"dropped\":%llu,\"discontinuities\":%llu,\"errors\":%llu}\n",
            received, bad, peak, capture->counters.dropped.load(), capture->counters.discontinuities.load(), capture->counters.errors.load());
        return received >= 200 && !bad && peak > 0.49f && peak < 0.51f && capture->counters.discontinuities.load() >= 1 && !capture->counters.errors.load() ? 0 : 2;
    } catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 1; }
}
