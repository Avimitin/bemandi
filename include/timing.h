#pragma once
#include <cstdint>
namespace bemandi {
int64_t ticks() noexcept;
int64_t frequency() noexcept;
// One common QPC-to-UTC epoch for video and audio; never follows wall clock jumps.
int64_t clock_epoch() noexcept;
int64_t timestamp() noexcept;
}
