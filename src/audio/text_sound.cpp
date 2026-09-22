#include "audio/text_sound.hpp"

namespace ghostbusters::audio {

std::vector<SidWrite> TextSound::text_tone()
{
    std::vector<SidWrite> writes;
    writes.reserve(volume_ == 0x0F ? 8 : 9);

    // $7306-$733A, preserving the 6502 store order from SceneAudio.
    writes.push_back({0x12, 0x08});
    if (volume_ != 0x0F) {
        writes.push_back({0x18, 0x0F});
        volume_ = 0x0F;
    }
    writes.push_back({0x0E, 0x12});
    writes.push_back({0x0F, 0x34});
    writes.push_back({0x10, 0x80});
    writes.push_back({0x11, 0x0F});
    writes.push_back({0x13, 0x03});
    writes.push_back({0x14, 0x00});
    writes.push_back({0x12, 0x41});
    return writes;
}

std::vector<SidWrite> TextSound::key_click()
{
    return {{0x12, 0x00}, {0x12, 0x41}};
}

} // namespace ghostbusters::audio
