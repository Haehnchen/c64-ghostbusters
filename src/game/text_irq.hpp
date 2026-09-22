#pragma once
#include "assets/payload.hpp"
#include "game/game_reset.hpp"

namespace ghostbusters::game {
// Normal raster IRQ with state $3A < $12: memory/VIC endpoint effects of
// $8ED4-$8FB8, including $9756 and $97A1. Audio follows in its own adapter.
void update_text_irq(const assets::Payload& payload, GameResetState& state);
}
