#include "game/game_reset.hpp"

#include "assets/city_data.hpp"
#include "assets/ui_data.hpp"


#include <algorithm>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kFullStateBytes = 0x12;
constexpr std::size_t kPartialStateBytes = 0x0C;

[[nodiscard]] ResetResult apply_native_game_reset(
    const assets::Payload& payload, const GameReset kind, GameResetState& state)
{
    ResetResult result;
    if (kind == GameReset::none) return result;

    const auto initial_state = assets::UiData(payload).reset_initial_state();
    const assets::CityData city_data(payload);
    const auto copied = kind == GameReset::full ? kFullStateBytes
                                                : kPartialStateBytes;
    std::copy_n(initial_state.begin(), copied, state.zero.begin() + 0x33);

    std::fill(state.zero.begin() + 0x45, state.zero.begin() + 0xDC, 0);
    std::fill_n(state.zero.begin() + 0x88, 3, 0xFF);

    state.vic[0x15] = 0;
    // Audio consumers depend on descending voice-register writes; volume survives.
    for (int reg = 0x0F; reg >= 0; --reg) {
        state.vic[reg] = 0;
        state.sid[reg] = 0;
        result.sid_writes.push_back({static_cast<std::uint8_t>(reg), 0});
    }

    for (std::size_t index = 0; index < 0x1E; ++index) {
        state.runtime[0x28 + index] = city_data.map_type(index);
    }
    state.vic[0x1C] = 3;
    state.zero[0xCF] = 0x1F;
    state.zero[0xD0] = 0x1C;
    state.zero[0xD7] = 0x14;

    // Only 960 cells are cleared; the final 64 bytes retain sprite/status data.
    std::fill_n(state.characters.screen.begin(), 0x3C0, 0);
    std::fill_n(state.characters.colors.begin(), 0x3C0, 0);
    state.zero[0x37] = 0;
    return result;
}

} // namespace

ResetResult apply_game_reset(const assets::Payload& payload,
                             const GameReset kind,
                             GameResetState& state)
{
    return apply_native_game_reset(payload, kind, state);
}

} // namespace ghostbusters::game
