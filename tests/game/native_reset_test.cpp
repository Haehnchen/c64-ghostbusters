#include "assets/payload.hpp"
#include "game/game_reset.hpp"
#include "game/text_irq.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    try {
        using namespace ghostbusters;
        const auto payload = assets::Payload::embedded();
        constexpr std::array<std::uint8_t, 18> initial{
            31, 31, 15, 0, 15, 8, 1, 0, 8, 63, 3, 153, 0, 0, 0, 0, 0, 0};
        constexpr std::array<std::uint8_t, 30> map{
            1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4,
            5, 6, 1, 2, 6, 1, 3, 4, 5, 4, 5, 6, 2, 3};
        for (const auto kind : {game::GameReset::none, game::GameReset::partial,
                                game::GameReset::full}) {
            game::GameResetState state;
            state.zero.fill(0xA5);
            state.runtime.fill(0xB6);
            state.vic.fill(0xC7);
            state.sid.fill(0xD8);
            state.characters.screen.fill(0xE9);
            state.characters.colors.fill(0xFA);
            const auto before = state;
            const auto result = game::apply_game_reset(payload, kind, state);
            if (kind == game::GameReset::none) {
                require(result.sid_writes.empty() && state.zero == before.zero &&
                            state.runtime == before.runtime && state.vic == before.vic &&
                            state.sid == before.sid &&
                            state.characters.screen == before.characters.screen &&
                            state.characters.colors == before.characters.colors,
                        "No reset must preserve state and emit no audio writes");
                continue;
            }
            const auto copied = kind == game::GameReset::full ? 18U : 12U;
            for (std::size_t i = 0; i < 18; ++i) {
                require(state.zero[0x33 + i] ==
                            (i == 4 ? 0 : i < copied ? initial[i] : 0xA5),
                        "Reset must preserve the partial tail and clear the input latch");
            }
            for (std::size_t i = 0; i < state.runtime.size(); ++i) {
                require(state.runtime[i] == (i >= 0x28 && i < 0x46 ? map[i - 0x28] : 0xB6),
                        "Reset must copy map types without changing retained runtime data");
            }
            for (std::size_t i = 0x45; i < state.zero.size(); ++i) {
                const auto expected = i == 0xCF ? 0x1F : i == 0xD0 ? 0x1C :
                    i == 0xD7 ? 0x14 : i >= 0x88 && i <= 0x8A ? 0xFF :
                    i < 0xDC ? 0 : 0xA5;
                require(state.zero[i] == expected,
                        "Reset must preserve clear bounds, sentinels, and display constants");
            }
            for (std::size_t i = 0; i < state.vic.size(); ++i)
                require(state.vic[i] == (i < 16 || i == 0x15 ? 0 : i == 0x1C ? 3 : 0xC7),
                        "Reset must preserve unrelated display registers");
            for (std::size_t i = 0; i < state.sid.size(); ++i)
                require(state.sid[i] == (i < 16 ? 0 : 0xD8),
                        "Reset must preserve audio registers outside the cleared voices");
            require(result.sid_writes.size() == 16, "Reset must emit 16 SID writes");
            for (std::size_t i = 0; i < 16; ++i) {
                require(result.sid_writes[i] == game::ResetSidWrite{
                            static_cast<std::uint8_t>(15 - i), 0},
                        "Reset audio writes must retain descending register order");
            }
            for (std::size_t i = 0; i < state.characters.screen.size(); ++i) {
                require(state.characters.screen[i] == (i < 960 ? 0 : 0xE9) &&
                            state.characters.colors[i] == (i < 960 ? 0 : 0xFA),
                        "Reset must clear 960 cells and preserve both tails");
            }
        }
        game::GameResetState state;
        std::uint32_t color_hash = 2166136261U;
        // Base and mirrored sprite IDs are valid; unrelated table bytes are not.
        for (unsigned base = 0; base < 68; base += 8) {
            for (unsigned sprite = 0; sprite < 8; ++sprite)
                state.zero[0x2B + sprite] = static_cast<std::uint8_t>(base + sprite < 68 ? base + sprite : 0);
            game::update_text_irq(payload, state);
            for (unsigned sprite = 0; sprite < 8 && base + sprite < 68; ++sprite)
                color_hash = (color_hash ^ state.vic[0x27 + sprite]) * 16777619U;
        }
        require(color_hash == 0x5EE72742U,
                "All native sprite colors retain their pinned register-visible nibbles");
        std::cout << "native reset tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
