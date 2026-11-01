#pragma once
#include <atomic>
#include <cstdint>

namespace bemandi {
inline int64_t to_100ns(int64_t ticks, int64_t frequency) {
    return (ticks / frequency) * 10000000 + (ticks % frequency) * 10000000 / frequency;
}
enum class SlotState { free, copying, ready, inflight };
// Single capture producer. Never catch up by emitting a burst after a pause.
class Cadence {
public:
    bool due(int64_t now, int64_t frequency, unsigned fps) {
        const int64_t period = frequency / fps;
        if (next_ && now < next_) return false;
        if (!next_ || now - next_ > period) next_ = now + period;
        else next_ += period;
        return true;
    }
    void reset() { next_ = 0; }
private:
    int64_t next_ = 0;
};
inline bool claim(std::atomic<SlotState>& state) {
    auto expected = SlotState::free;
    return state.compare_exchange_strong(expected, SlotState::copying, std::memory_order_acq_rel);
}
}
