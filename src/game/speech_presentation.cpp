#include "game/speech_presentation.hpp"
#include <algorithm>

namespace ghostbusters::game {
void begin_gameplay_speech(video::CharacterFrame& frame)
{
    std::fill_n(frame.charset.begin() + 0x7F8, 8, 0xFF);
    std::fill_n(frame.screen.begin() + 0x370, 0x78, 0xFF);
    std::fill_n(frame.colors.begin() + 0x370, 0x78, 0);
}
void end_gameplay_speech(video::CharacterFrame& frame)
{
    std::fill_n(frame.screen.begin() + 0x370, 0x78, 0);
    std::fill_n(frame.colors.begin() + 0x370, 0x78, 1);
}
}
