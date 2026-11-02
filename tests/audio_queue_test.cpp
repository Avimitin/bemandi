#include "audio_queue.h"
#include "game_profiles.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
int main() {
    auto q = std::make_unique<bemandi::AudioQueue<4>>();
    float samples[4]{0.25f,-0.5f,0.5f,-0.25f};
    REQUIRE(!q->push(nullptr, 2, 0, 0));
    REQUIRE(!q->push(samples, 0, 0, 0));
    REQUIRE(!q->push(samples, bemandi::audio_max_frames + 1, 0, 0));
    for (unsigned i = 0; i < 4; ++i) REQUIRE(q->push(samples, 2, i * 100, i * 2));
    REQUIRE(!q->push(samples, 2, 999, 999));
    const auto* owned = q->front();
    REQUIRE(owned && owned->sequence == 0 && owned->samples[1] == -0.5f);
    samples[1] = 1.0f; // Queued data owns its memory.
    REQUIRE(owned->samples[1] == -0.5f);
    for (unsigned i = 0; i < 4; ++i) {
        REQUIRE(q->front()->sequence == i * 2 && q->front()->pts == i * 100);
        q->pop();
    }
    REQUIRE(!q->front());
    constexpr unsigned count = 200000;
    std::thread producer([&] {
        for (unsigned i = 0; i < count; ++i) {
            float value[2]{static_cast<float>(i), -static_cast<float>(i)};
            while (!q->push(value, 1, i, i)) std::this_thread::yield();
        }
    });
    for (unsigned i = 0; i < count; ++i) {
        const bemandi::AudioBlock* b;
        while (!(b = q->front())) std::this_thread::yield();
        REQUIRE(b->sequence == i && b->pts == i && b->frames == 1);
        REQUIRE(b->samples[0] == static_cast<float>(i) && b->samples[1] == -static_cast<float>(i));
        q->pop();
    }
    producer.join();
    REQUIRE(!bemandi::find_game_profile("unrecognized"));
    REQUIRE(bemandi::find_game_profile("c98e183ec81ecb9e1cb13c237f9c9cb0b3f65b6fdaff6fd5e6a526008c546534")
        == &bemandi::games::iidx34_c98e183e);
    REQUIRE(bemandi::find_game_profile("3755587f44785effe5008f74cfe7dccbdf423ad09feead48e00a90f78a42fe83")
        == &bemandi::games::iidx34_3755587f);
    for (const auto* profile : bemandi::game_profiles) {
        const std::string_view hash(profile->sha256);
        REQUIRE(hash.size() == 64);
        REQUIRE(bemandi::find_game_profile(hash) == profile);
        REQUIRE(!bemandi::find_game_profile(hash.substr(0, 8))); // Prefixes cannot enable a tap.
        std::string altered(hash);
        altered.back() = altered.back() == '0' ? '1' : '0';
        REQUIRE(!bemandi::find_game_profile(altered));
    }
    puts("audio queue ownership, overflow, wraparound, concurrency and profile selection passed");
}
