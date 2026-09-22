#pragma once

#include <vector>

namespace ghostbusters::game {

// Sound intents emitted by game-side state machines. Audio backends decide
// how (or whether) to realize these events.
enum class SoundEvent { text_tone, key_click, equipment_motor_start, equipment_motor_stop };
using SoundEvents = std::vector<SoundEvent>;

} // namespace ghostbusters::game
