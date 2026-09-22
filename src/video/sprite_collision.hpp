#pragma once

#include "video/sprite_layer.hpp"

namespace ghostbusters::video {

// Test a static SpriteLayer register snapshot in its logical coordinate
// system. The result has one bit per sprite: a bit is set when that enabled
// sprite shares an opaque pixel with another enabled sprite. This is a pure
// geometry helper; VIC collision latches and raster timing are outside its
// scope.
[[nodiscard]] std::uint8_t sprite_collision_mask(const SpriteLayer& layer);

} // namespace ghostbusters::video
