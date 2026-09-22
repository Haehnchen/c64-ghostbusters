#pragma once

#include "assets/payload.hpp"
#include "game/drive_controls.hpp"

#include <cstdint>

namespace ghostbusters::game {

// Complete native boundary for $7011-$7060. The caller owns the surrounding
// frame scheduler and passes the real VIC-II $D016 value by reference.
void update_drive_frame(const assets::Payload& payload, DriveControlsState& state,
                        std::uint8_t& vic_control2_d016);

} // namespace ghostbusters::game
