#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace ghostbusters::game {

// PAL C64: 63 cycles/line, 312 lines/frame, nominal 985248 CPU cycles/second.
// Integer accumulator keeps game ticks independent of host rendering frequency.
class PalClock {
public:
    std::uint32_t advance(std::chrono::nanoseconds elapsed)
    {
        // Pause/suspend does not cause an unbounded catch-up loop.
        const auto bounded = std::clamp<std::int64_t>(elapsed.count(), 0, 250'000'000);
        remainder_ += static_cast<std::uint64_t>(bounded) * 985248;
        constexpr std::uint64_t frame = 63ULL * 312 * 1'000'000'000;
        const auto ticks = static_cast<std::uint32_t>(remainder_ / frame);
        remainder_ %= frame;
        return ticks;
    }

private:
    std::uint64_t remainder_ = 0;
};

} // namespace ghostbusters::game
