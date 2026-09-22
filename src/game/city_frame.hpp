#pragma once

#include "assets/payload.hpp"
#include "game/city_controls.hpp"
#include "game/sound_event.hpp"

#include <cstdint>

namespace ghostbusters::game {

// Runtime-owned bytes sampled by the common city-frame path. frame09 is the
// value after the original $70F2 increment; this module never advances it.
struct CityFrameInput {
    std::uint8_t frame09 = 0;
    std::uint8_t gate02 = 0x80;
    std::uint8_t flags47 = 0;
    std::uint8_t delay14 = 0;
    std::uint8_t key17 = 0;
    std::uint8_t random06 = 0;
};

struct CityFramePreResult {
    // False follows the $7103 return and tells the scheduler to skip the
    // notice update, post-status update, and state-18 handler this frame.
    bool continue_frame = false;
    bool reset_requested = false;
    std::uint8_t delay14 = 0;
    std::uint8_t key17 = 0;
    SoundEvents audio_events{};
};

// Implements the gated $70EE-$71F3 city path.  On a true continue_frame the
// caller next invokes state.notices.tick(state.state3a, state.characters).
[[nodiscard]] CityFramePreResult update_city_frame_before_notice(
    const assets::Payload& payload, CityControlsState& state, CityFrameInput input);

// $72BD-$72C7, after status rendering and before post-status PK growth.
void update_city_difficulty(const assets::Payload& payload, CityControlsState& state);

// Implements the city-valid $5E=0 path through $73BD-$746C.  It must run
// after NoticeScroller::tick and before CityControls::tick, using the same
// frame09/random06 sample supplied to the pre-notice call.
void update_city_frame_after_notice(const assets::Payload& payload,
                                    CityControlsState& state,
                                    CityFrameInput input);

} // namespace ghostbusters::game
