#include "assets/payload.hpp"
#include "assets/city_data.hpp"
#include "game/city_frame.hpp"
#include "game/runtime_clock.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::CityFrameInput;
using ghostbusters::game::update_city_frame_after_notice;
using ghostbusters::game::update_city_frame_before_notice;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_gates(const Payload& payload)
{
    CityControlsState state;
    auto result = update_city_frame_before_notice(payload, state, {0, 0x80, 0x80, 0, 0x42, 0});
    check(!result.continue_frame && !result.reset_requested && result.key17 == 0x42,
          "$47 sign gate returns before common city work");

    result = update_city_frame_before_notice(payload, state, {0, 0, 0, 0, 0, 0});
    check(!result.continue_frame, "$02 sign gate returns before common city work");

    result = update_city_frame_before_notice(payload, state, {0, 0x80, 0, 2, 0, 0});
    check(!result.continue_frame && !result.reset_requested && result.delay14 == 1,
          "nonterminal $14 delay decrements and returns");
    result = update_city_frame_before_notice(payload, state, {0, 0x80, 0, 1, 0, 0});
    check(!result.continue_frame && result.reset_requested && result.delay14 == 0,
          "terminal $14 delay requests the original global reset");
    check(result.audio_events.empty(), "city-frame gates do not invent audio events");
}

void test_colors_and_resource_notice(const Payload& payload)
{
    CityControlsState state;
    state.state3a = 0x12;
    state.backpack_charge3e = 0x09;
    state.empty_traps6b = 4;
    state.backup_men3d = 3;
    state.building_status_c8[16] = 3;
    const auto result = update_city_frame_before_notice(
        payload, state, {0, 0x80, 0, 0, 0x20, 0});
    check(result.continue_frame && result.key17 == 0 && !result.reset_requested,
          "eligible common frame consumes the resources-space key");
    check(state.characters.colors[0x32A] == 0x0D &&
              state.characters.colors[0x1CC] == 1 &&
              state.characters.colors[0x32E] == 1,
          "state 18 paints the selected building column and fixed labels");

    const auto notice = state.notices.snapshot();
    check(notice.cached_status4c == 1 && notice.length4a == 67 && notice.position4b == 1 &&
              notice.buffer[0x12] == 0 && notice.buffer[0x13] == 0x39 &&
              notice.buffer[0x24] == 0x34 && notice.buffer[0x35] == 0x33 &&
              notice.buffer[67] == 0xFF,
          "resource notice contains original translated text and substituted values");
}

void test_status_and_pk_progression(const Payload& payload)
{
    CityControlsState pre_city;
    pre_city.state3a = 0x11;
    pre_city.building_status_c8[0] = 0x10;
    pre_city.pk_low5a = 0x12;
    update_city_frame_after_notice(payload, pre_city, {0, 0x80, 0, 0, 0, 0});
    check(pre_city.building_status_c8[0] == 0x10 && pre_city.pk_low5a == 0x12,
          "$73BD dispatches states below 18 before status and PK updates");

    CityControlsState state;
    state.building_status_c8[0] = 0x10;
    state.building_status_c8[1] = 0xFF;
    update_city_frame_after_notice(payload, state, {0, 0x80, 0, 0, 0, 0});
    check(state.building_status_c8[0] == 0x20 && state.building_status_c8[1] == 0 &&
              state.pk_high5b == 3 && state.pk_low5a == 1,
          "$09 zero advances statuses, awards 300 PK, then advances passive PK");

    CityControlsState carry;
    carry.pk_low5a = 0x99;
    carry.pk_high5b = 0x49;
    carry.map_types_ea28[5] = 1;
    update_city_frame_after_notice(payload, carry, {0, 0x80, 0, 0, 0, 5});
    check(carry.pk_low5a == 0 && carry.pk_high5b == 0x50 &&
              carry.pending_alert80 == 5 && carry.building_status_c8[5] == 0xC8,
          "passive PK BCD carry crosses 5000 and schedules the original alert candidate");

    CityControlsState finale;
    finale.pk_low5a = 0x12;
    finale.finale_active81 = 1;
    update_city_frame_after_notice(payload, finale, {0, 0x80, 0, 0, 0, 0});
    check(finale.pk_low5a == 0x12, "finale suppresses passive PK growth");

    CityControlsState masked;
    masked.pk_low5a = 0x12;
    update_city_frame_after_notice(payload, masked, {0x10, 0x80, 0, 0, 0, 0});
    check(masked.pk_low5a == 0x12, "frame/movement mask suppresses passive PK growth");
}

void test_steady_color_vs_blink(const Payload& payload)
{
    const ghostbusters::assets::CityData city(payload);
    CityControlsState state;
    state.state3a = 0x12;
    state.building_status_c8[1] = 0x15; // Detected early stage, steady purple.
    state.building_status_c8[3] = 0x1F; // Revealed catch-ready stage, red/green.
    state.characters.multicolor = true;
    // Pixel-pair 3 uses color-RAM bits0..2 in the city's multicolor mode.
    state.characters.charset.fill(0xFF);
    const auto purple_cell = static_cast<unsigned>(city.building_color_base(1)) + 0x2C;
    const auto blinking_cell = static_cast<unsigned>(city.building_color_base(3)) + 0x2C;
    for (std::uint8_t frame = 0; frame < 64; ++frame) {
        (void)update_city_frame_before_notice(payload, state, {frame, 0x80, 0, 0, 0, 0});
        if ((frame & 3) != 3) continue; // All four columns have now been painted.
        const auto blink_color = static_cast<std::uint8_t>((frame & 0x10) ? 0x0A : 0x0D);
        check(state.characters.colors[purple_cell] == 0x0C &&
                  state.characters.colors[blinking_cell] == blink_color,
              "early-stage purple is steady; catch-ready building alternates red and green");
        const auto image = ghostbusters::video::render_characters(state.characters);
        const auto pixel = [](unsigned cell) { return (cell / 40) * 8 * 320 + (cell % 40) * 8; };
        check(image[pixel(purple_cell)] == 4 && image[pixel(blinking_cell)] == (blink_color & 7),
              "city color RAM uses multicolor purple4/red2/green5, not hires colors12/10/13");
    }
    check((state.building_status_c8[1] & 0x0C) != 0x0C &&
              (state.building_status_c8[3] & 0x0C) == 0x0C,
          "steady purple and blink correspond to different capture eligibility in normal stages");
}

void test_natural_haunting_lifecycle(const Payload& payload)
{
    const ghostbusters::assets::CityData city(payload);
    CityControlsState state;
    for (unsigned i = 0; i < 20; ++i)
        state.map_types_ea28[i] = city.map_type(i);
    std::uint8_t frame = 0, random = 1;
    int haunted = -1;
    bool activated = false;
    // No injected building statuses: run the common clock until a building
    // becomes globally visible even without a detector/player nearby.
    for (unsigned tick = 0; tick < 13000 && haunted < 0; ++tick) {
        frame = ghostbusters::game::advance_game_frame(frame, 0);
        random = ghostbusters::game::advance_game_random(random);
        const CityFrameInput input{frame, 0x80, 0, 0, 0, random};
        (void)update_city_frame_before_notice(payload, state, input);
        ghostbusters::game::update_city_difficulty(payload, state);
        update_city_frame_after_notice(payload, state, input);
        for (unsigned i = 0; i < 20; ++i) {
            activated |= state.building_status_c8[i] != 0;
            if ((state.building_status_c8[i] & 0x0F) == 0x0F) haunted = static_cast<int>(i);
        }
        check(state.building_status_c8[0x11] == 0 && state.building_status_c8[0x0A] == 0,
              "ordinary hauntings exclude headquarters and Zuul");
    }
    check(activated && haunted >= 0, "empty city naturally develops a visible haunting");
    const auto phase = static_cast<std::uint8_t>(haunted & 3);
    (void)update_city_frame_before_notice(payload, state, {phase, 0x80, 0, 0, 0, random});
    const auto bright = state.characters.colors;
    (void)update_city_frame_before_notice(payload, state,
        {static_cast<std::uint8_t>(phase | 0x10), 0x80, 0, 0, 0, random});
    check(bright != state.characters.colors, "naturally matured building visibly blinks");
    check((state.building_status_c8[haunted] & 0x0C) == 0x0C,
          "natural haunting satisfies the building capture eligibility bits");

    CityControlsState blocked;
    blocked.map_types_ea28[1] = 1;
    blocked.building_status_c8[0] = 0x10;
    update_city_frame_after_notice(payload, blocked, {1, 0x80, 0, 0, 0, 0});
    check(blocked.building_status_c8[1] == 0, "existing immature wave blocks another wave");
    blocked.building_status_c8[0] = 0;
    blocked.finale_active81 = 1;
    update_city_frame_after_notice(payload, blocked, {1, 0x80, 0, 0, 0, 0});
    check(blocked.building_status_c8[1] == 0, "finale blocks ordinary haunting activation");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_gates(payload);
        test_colors_and_resource_notice(payload);
        test_status_and_pk_progression(payload);
        test_steady_color_vs_blink(payload);
        test_natural_haunting_lifecycle(payload);
        std::cout << "native city-frame tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
