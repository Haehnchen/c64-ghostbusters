#include "game/input_frame.hpp"

namespace ghostbusters::game {

CommonVolume update_pause(std::uint8_t& flags47,
    std::uint8_t state3a, std::uint8_t raw_key19)
{
    if (state3a >= 0x2A) return CommonVolume::unchanged;
    if (raw_key19 != 0x3F || state3a < 0x12) {
        flags47 &= 0xBF;
        return (flags47 & 0x80) == 0 ? CommonVolume::restore_cache : CommonVolume::unchanged;
    }
    if ((flags47 & 0x40) != 0) return CommonVolume::unchanged;
    flags47 = static_cast<std::uint8_t>((flags47 ^ 0x80) | 0x40);
    return CommonVolume::mute;
}

InputFrameResult update_input_frame(InputFrameState& state,
    std::uint8_t counter08, std::uint8_t state3a, std::uint8_t raw_key19)
{
    InputFrameResult result;
    if (state.mode20 == 1) {
        state.mode20 = 2;
        state.gate02 = 0x80;
        state.cycle03 = state.value04 = state.value05 = 0;
        result.reset = GameReset::full;
        return result;
    }
    if (counter08 == 0) {
        if ((state.gate02 & 0x80) == 0) {
            state.cycle03 = static_cast<std::uint8_t>((state.cycle03 + 1) & 7);
            if (state.cycle03 == 0) {
                state.gate02 = 0xC0;
                result.reset = GameReset::partial;
                return result;
            }
        }
        ++state.idle46;
        if (state.idle46 == 0) {
            ++state.idle45;
            if (state.idle45 >= 4) state.idle45 = 0x80;
        }
    }
    result.volume = update_pause(state.flags47, state3a, raw_key19);
    if ((state.flags47 & 0x80) != 0) return result;
    ++state.frame09;
    if ((state.gate02 & 0x80) == 0) return result;
    if (state.delay14 != 0) {
        if (--state.delay14 == 0) result.reset = GameReset::partial;
        return result;
    }
    result.continue_frame = true;
    return result;
}

} // namespace ghostbusters::game
