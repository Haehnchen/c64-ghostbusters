#pragma once
#include "video/character_frame.hpp"

namespace ghostbusters::game {
// Game-mode presentation writes of $8DB9; title mode has a different wrapper.
// The surrounding runtime pauses foreground and raster-IRQ work between them.
void begin_gameplay_speech(video::CharacterFrame& frame);
void end_gameplay_speech(video::CharacterFrame& frame);
}
