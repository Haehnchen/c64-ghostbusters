#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/pal_counter.hpp"
#include "game/runtime_clock.hpp"
#include "game/zuul.hpp"
#include "video/sprite_collision.hpp"
#include "video/sprite_layer.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::Zuul;
using ghostbusters::game::ZuulTickResult;

ghostbusters::video::SpriteLayer scene_sprites(
    const ghostbusters::game::EquipmentSpriteState& state)
{
    ghostbusters::video::SpriteLayer layer;
    layer.multicolor1 = state.shared_multicolor_1;
    layer.multicolor2 = state.shared_multicolor_2;
    for (std::size_t i = 0; i < layer.sprites.size(); ++i) {
        auto& sprite = layer.sprites[i];
        std::copy_n(state.bitmap_data[i].begin(), sprite.bitmap.size(),
                    sprite.bitmap.begin());
        sprite.x = static_cast<std::int16_t>(state.x[i] * 2 + 1 - 24);
        sprite.y = static_cast<std::int16_t>(state.y[i] - 50);
        sprite.color = state.colors[i];
        sprite.enabled = (state.enabled_mask & (1U << i)) != 0;
        sprite.multicolor = (state.multicolor_mask & (1U << i)) != 0;
        sprite.expand_x = (state.x_expand_mask & (1U << i)) != 0;
        sprite.expand_y = (state.y_expand_mask & (1U << i)) != 0;
        sprite.behind_foreground = (state.priority_mask & (1U << i)) != 0;
    }
    return layer;
}

struct RouteResult {
    std::uint8_t state;
    std::uint8_t gatekeeper_remaining;
    std::uint8_t keymaster_remaining;
    std::uint8_t backup_men;
    ghostbusters::game::AccountBalanceBytes balance;
    unsigned frames;
    unsigned speech3;
    unsigned speech4;
};

RouteResult run_route(const Payload& payload, std::uint8_t wait_phase,
                      std::uint8_t initial_frame)
{
    DriveControlsState initial;
    initial.city.state3a = 0x28;
    initial.city.balance57 = {0x00, 0x10, 0x00};
    BuildingControlsRegisters building{0, 0, {0, 0}, 0, {0, 0}};
    Zuul zuul(payload, initial, building, {0, 0, 0, 0, 0, 0, 0, 0});

    std::uint8_t counter = 0;
    std::uint8_t frame = initial_frame;
    std::uint8_t collision_latch = 0;
    unsigned speech3 = 0;
    unsigned speech4 = 0;
    unsigned frames = 0;

    auto consume = [&](const ZuulTickResult& result) {
        if (result.collision_read || result.collision_clear_read) {
            collision_latch = 0;
        }
        for (const auto& event : result.audio_events) {
            if (event.command == 3) ++speech3;
            if (event.command == 4) ++speech4;
        }
    };

    for (; frames < 4096 && zuul.raw_state() <= 0x2C; ++frames) {
        auto& city = zuul.state().city;
        city.sprites.enabled_mask = 0xFF;
        collision_latch = static_cast<std::uint8_t>(
            collision_latch |
            ghostbusters::video::sprite_collision_mask(scene_sprites(city.sprites)));
        if (city.countdown7c != 0) --city.countdown7c;

        std::uint8_t joystick = 0xFF;
        if (zuul.raw_state() == 0x29 &&
            zuul.registers().gate_phase_ea77 == 0) {
            const auto target_x = city.sprites.target_x[5];
            const auto target_y = city.sprites.target_y[5];
            if (target_x > 0x5C) joystick = 0xFB;
            else if (target_x < 0x5C) joystick = 0xF7;
            else if (target_y < 0xC0 || (counter & 0x7FU) == wait_phase) {
                joystick = 0xFE;
            }
        }

        const auto result = zuul.tick({counter, frame, joystick, collision_latch});
        consume(result);
        const bool speech_frame = result.blocking_request;
        if (speech_frame) consume(zuul.resume_speech());
        if (!speech_frame) {
            counter = ghostbusters::game::advance_pal_counter(counter);
            frame = ghostbusters::game::advance_game_frame(frame, 0);
        }
    }

    return {zuul.raw_state(), zuul.registers().gatekeeper_ea78,
            zuul.registers().keymaster_ea79, zuul.state().city.backup_men3d,
            zuul.state().city.balance57, frames, speech3, speech4};
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        // A bounded search over all counter phases and both $09 parities found
        // phase 6/parity 0. Keep the discovered controller deterministic so a
        // change in sprite pixels, latch ordering, or PAL cadence fails here.
        const auto route_matches = [](const RouteResult& result) {
            return result.state == 0x36 && result.gatekeeper_remaining == 0 &&
                   result.keymaster_remaining == 2 && result.backup_men == 1 &&
                   result.speech3 == 0 && result.speech4 == 1 &&
                   result.balance.byte57 == 0x00 &&
                   result.balance.byte58 == 0x60 &&
                   result.balance.byte59 == 0x00 && result.frames == 1086;
        };
        const auto even = run_route(payload, 6, 0);
        const auto odd = run_route(payload, 6, 1);
        if (!route_matches(even) || !route_matches(odd)) {
            const auto& result = route_matches(even) ? odd : even;
            std::cerr << "Zuul success route diverged: state="
                      << static_cast<unsigned>(result.state)
                      << " gate=" << static_cast<unsigned>(result.gatekeeper_remaining)
                      << " collision=" << static_cast<unsigned>(result.keymaster_remaining)
                      << " men=" << static_cast<unsigned>(result.backup_men)
                      << " speech3=" << result.speech3
                      << " speech4=" << result.speech4
                      << " frames=" << result.frames << '\n';
            return 1;
        }
        std::cout << "Zuul collision-free success routes passed for both $09 parities\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
