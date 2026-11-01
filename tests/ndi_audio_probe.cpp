#include "ndi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
// Read-only live-source probe; no device setup, routing changes or audio recording.
int main(int argc, char** argv) {
    try {
        const std::string wanted = argc > 1 ? argv[1] : "IIDX Audio";
        const int seconds = argc > 2 ? std::stoi(argv[2]) : 10;
        if (seconds < 1 || seconds > 60) throw std::runtime_error("Duration must be 1..60 seconds");
        bemandi::NdiRuntime runtime;
        auto api = runtime.api;
        NDIlib_find_create_t find{};
        auto finder = api->find_create_v2(&find);
        if (!finder) throw std::runtime_error("NDI discovery failed");
        NDIlib_recv_instance_t receiver = nullptr;
        using Clock = std::chrono::steady_clock;
        const auto end = Clock::now() + std::chrono::seconds(seconds);
        unsigned packets = 0, nonzero = 0, bad = 0;
        uint64_t samples = 0;
        double sum = 0, peak = 0;
        int rate = 0, channels = 0;
        int64_t previous = 0;
        while (Clock::now() < end) {
            if (!receiver) {
                api->find_wait_for_sources(finder, 100);
                uint32_t count = 0;
                const auto sources = api->find_get_current_sources(finder, &count);
                for (uint32_t i = 0; i < count; ++i) {
                    if (std::string(sources[i].p_ndi_name).find(wanted) == std::string::npos) continue;
                    NDIlib_recv_create_v3_t desc{};
                    desc.source_to_connect_to = sources[i];
                    desc.bandwidth = NDIlib_recv_bandwidth_audio_only;
                    receiver = api->recv_create_v3(&desc);
                    fprintf(stderr, "Source: %s\n", sources[i].p_ndi_name);
                    break;
                }
                continue;
            }
            NDIlib_audio_frame_v2_t audio{};
            if (api->recv_capture_v2(receiver, nullptr, &audio, nullptr, 100) != NDIlib_frame_type_audio) continue;
            ++packets; rate = audio.sample_rate; channels = audio.no_channels;
            if (audio.timecode <= previous) ++bad;
            previous = audio.timecode;
            for (int c = 0; c < audio.no_channels; ++c) {
                const auto data = reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(audio.p_data) + c * audio.channel_stride_in_bytes);
                for (int i = 0; i < audio.no_samples; ++i) {
                    const double value = data[i];
                    if (!std::isfinite(value)) { ++bad; continue; }
                    peak = std::max(peak, std::abs(value)); sum += value * value; ++samples;
                    if (std::abs(value) > 0.000001) ++nonzero;
                }
            }
            api->recv_free_audio_v2(receiver, &audio);
        }
        if (receiver) api->recv_destroy(receiver);
        api->find_destroy(finder);
        printf("{\"packets\":%u,\"rate\":%d,\"channels\":%d,\"nonzero_samples\":%u,\"peak\":%.8f,\"rms\":%.8f,\"bad\":%u}\n",
            packets, rate, channels, nonzero, peak, samples ? std::sqrt(sum / static_cast<double>(samples)) : 0, bad);
        return packets && !bad ? 0 : 2;
    } catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 1; }
}
