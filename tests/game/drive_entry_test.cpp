#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/drive_entry.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::DriveEntry;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

CityControlsState make_state(std::uint8_t route_length, std::uint8_t bait)
{
    CityControlsState state;
    state.state3a = 0x13;
    state.route_length66 = route_length;
    state.bait_active68 = bait;
    state.characters.screen.fill(0xA5);
    state.characters.colors.fill(0xB6);
    state.characters.charset.fill(0xC7);
    state.characters.background = 0x03;
    state.characters.multicolor1 = 0x04;
    state.characters.multicolor2 = 0x05;
    state.characters.multicolor = true;
    state.sprites.pointers.fill(0xD1);
    state.sprite_control_c0.fill(0xE2);
    state.sprites.x.fill(0x11);
    state.sprites.y.fill(0x22);
    state.sprites.target_x.fill(0x33);
    state.shadow_x_ea46.fill(0x71);
    state.shadow_y_ea47.fill(0x81);
    for (std::size_t i = 0; i < state.sprites.target_y.size(); ++i) {
        state.sprites.target_y[i] = static_cast<std::uint8_t>(0x40 + i);
    }
    state.sprites.x_expand_mask = 0x08;
    state.sprites.y_expand_mask = 0x09;
    state.sprites.priority_mask = 0x0A;
    state.sprites.multicolor_mask = 0x0B;
    state.sprites.shared_multicolor_1 = 0x0C;
    state.sprites.shared_multicolor_2 = 0x0D;
    return state;
}

DriveEntry::SavedVehicleCharset make_saved_charset()
{
    DriveEntry::SavedVehicleCharset saved{};
    for (std::size_t i = 0; i < saved.size(); ++i) {
        saved[i] = static_cast<std::uint8_t>((i * 37U + 0x19U) & 0xFFU);
    }
    return saved;
}

void check_restored_charset(const DriveEntry& drive,
                           const DriveEntry::SavedVehicleCharset& saved)
{
    check(std::equal(saved.begin(), saved.end(), drive.characters().charset.begin() + 0x0200),
          "state 19 restores the complete modified vehicle charset");
    check(std::all_of(drive.characters().charset.begin(),
                      drive.characters().charset.begin() + 0x0200,
                      [](std::uint8_t value) { return value == 0xC7; }),
          "state 19 leaves the base charset untouched");
}

void test_state19_and_state20(const Payload& payload)
{
    const auto saved = make_saved_charset();
    const auto scene_data = ghostbusters::game::shared_sprite_data(payload);
    constexpr std::array<std::uint8_t, 8> initial_x{0, 0, 0, 0, 0x36, 0x6D, 0x6D, 0x36};
    constexpr std::array<std::uint8_t, 8> initial_y{};
    constexpr std::array<std::uint8_t, 8> initial_pointers{0, 0, 4, 5, 12, 12, 12, 12};
    constexpr std::array<std::uint8_t, 8> initial_colors{0, 0, 1, 2, 1, 1, 1, 1};
    auto input = make_state(66, 1);
    const auto target_x = input.sprites.target_x;
    const auto target_y = input.sprites.target_y;
    const auto control_pointers = input.sprite_control_c0;
    DriveEntry drive(payload, input, saved, 2);

    check(drive.stage() == DriveEntry::Stage::entering,
          "drive entry starts before state 19");
    drive.tick();
    check(drive.stage() == DriveEntry::Stage::preparing && drive.state().state3a == 0x14,
          "state 19 ends at state 20");
    check(drive.distance67() == 71 && drive.state().route_length66 == 0,
          "state 19 adds five to the route length");
    check(drive.state().bait_active68 == 0,
          "state 19 consumes an active bait formation");
    std::array<std::uint8_t, 8> expected_bait_shadow{};
    for (std::size_t i = 0; i < 4; ++i) {
        expected_bait_shadow[i * 2] = input.sprites.target_x[i + 4];
        expected_bait_shadow[i * 2 + 1] = input.sprites.target_y[i + 4];
    }
    check(drive.saved_bait_targets() == expected_bait_shadow,
          "state 19 saves the interleaved target pairs for sprites 4 to 7");
    for (std::size_t i = 0; i < 4; ++i) {
        check(drive.state().shadow_x_ea46[i + 4] == expected_bait_shadow[i * 2] &&
                  drive.state().shadow_y_ea47[i + 4] == expected_bait_shadow[i * 2 + 1],
              "state 19 updates the matching EA4E-EA55 shadow fields");
    }
    check(std::all_of(drive.sprites().pointers.begin(), drive.sprites().pointers.end(),
                      [](std::uint8_t value) { return value == 0; }),
          "state 19 clears active sprite pointers");
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        check(std::equal(scene_data.begin(), scene_data.begin() + 64,
                         drive.sprites().bitmap_data[sprite].begin()) &&
                  drive.sprites().colors[sprite] == 0,
              "state 19 resolves the cleared pointer to sprite-zero graphics");
    }
    check(drive.sprites().target_x == target_x && drive.sprites().target_y == target_y &&
              drive.state().sprite_control_c0 == control_pointers,
          "state 19 preserves sprite targets and control pointers");
    check(std::all_of(drive.characters().screen.begin(),
                      drive.characters().screen.begin() + 0x03C0,
                      [](std::uint8_t value) { return value == 0; }) &&
              drive.characters().screen[0x03C0] == 0xA5,
          "state 19 clears only the first 960 screen cells");
    check(std::all_of(drive.characters().colors.begin(),
                      drive.characters().colors.begin() + 0x0370,
                      [](std::uint8_t value) { return value == 0; }) &&
              std::all_of(drive.characters().colors.begin() + 0x0370,
                          drive.characters().colors.begin() + 0x0397,
                          [](std::uint8_t value) { return value == 1; }) &&
              drive.characters().colors[0x03C0] == 0xB6,
          "state 19 preserves the status row color contract and color tail");
    check(drive.characters().background == 0x0C &&
              drive.characters().multicolor1 == 1 &&
              drive.characters().multicolor2 == 6,
          "state 19 installs the selected vehicle VIC colors");
    check_restored_charset(drive, saved);
    check(drive.state1a() == 0 && drive.state1b() == 0 && drive.state1c() == 0 &&
              drive.state17() == 0,
          "state 19 clears its transient zero-page fields");

    drive.state().key17 = 0x42;
    drive.tick();
    check(drive.stage() == DriveEntry::Stage::ready && drive.state().state3a == 0x15,
          "state 20 ends at the active-drive boundary");
    check(drive.state17() == 0,
          "state 20's common state transition clears the pending key");
    check(drive.vehicle_position63() == 0x64,
          "state 20 initializes the vehicle position");
    check(std::all_of(drive.characters().colors.begin(),
                      drive.characters().colors.begin() + 0x0370,
                      [](std::uint8_t value) { return value == 8; }) &&
              std::all_of(drive.characters().colors.begin() + 0x0370,
                          drive.characters().colors.begin() + 0x0397,
                          [](std::uint8_t value) { return value == 1; }),
          "state 20 fills 880 colors and preserves status-row colors");

    for (std::size_t column = 0; column < 12; ++column) {
        const auto first = static_cast<std::uint8_t>(64 + 16 * column);
        for (std::size_t row = 0; row < 16; ++row) {
            const auto offset = (4 + row) * 40 + 25 + column;
            check(drive.characters().screen[offset] ==
                      static_cast<std::uint8_t>(first + row) &&
                      drive.characters().colors[offset] == 8,
                  "state 20 draws the 12 by 16 vehicle grid at column 25");
        }
    }
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        check(drive.sprites().x[sprite] ==
                  initial_x[sprite] &&
                  drive.sprites().y[sprite] ==
                  initial_y[sprite] &&
                  drive.sprites().pointers[sprite] ==
                  initial_pointers[sprite],
              "state 20 restores current sprite coordinates and pointers");
        const auto pointer = drive.sprites().pointers[sprite];
        const auto source = static_cast<std::size_t>(pointer) * 64U;
        check(std::equal(scene_data.begin() + static_cast<std::ptrdiff_t>(source),
                         scene_data.begin() + static_cast<std::ptrdiff_t>(source + 64U),
                         drive.sprites().bitmap_data[sprite].begin()) &&
                  drive.sprites().colors[sprite] ==
                      initial_colors[sprite],
              "state 20 resolves each pointer against the startup sprite image");
    }
    check(drive.sprites().target_x == target_x && drive.sprites().target_y == target_y &&
              drive.state().sprite_control_c0 == control_pointers,
          "state 20 preserves target coordinates and control pointers");
    check(drive.sprites().y_expand_mask == 0xF0 && drive.sprites().priority_mask == 0xF0 &&
              drive.sprites().x_expand_mask == 0x08,
          "state 20 sets only the original sprite expansion and priority masks");
    check_restored_charset(drive, saved);

    const auto state_after_ready = drive.state();
    const auto charset_after_ready = drive.characters().charset;
    drive.tick();
    check(drive.stage() == DriveEntry::Stage::ready &&
              drive.characters().charset == charset_after_ready &&
              drive.state().characters.screen == state_after_ready.characters.screen,
          "active state 21 boundary is a no-op");
}

void test_distance_saturation(const Payload& payload)
{
    const auto saved = make_saved_charset();
    for (const auto length : std::array<std::uint8_t, 4>{0, 250, 251, 255}) {
        DriveEntry drive(payload, make_state(length, 0), saved, 0);
        drive.tick();
        const auto expected = length <= 250 ? static_cast<std::uint8_t>(length + 5) : 0xFF;
        check(drive.distance67() == expected && drive.state().route_length66 == 0,
              "state 19 distance saturation boundary");
    }
}

void test_no_bait_save(const Payload& payload)
{
    const auto saved = make_saved_charset();
    auto state = make_state(0, 0);
    state.sprites.target_y.fill(0x99);
    const auto shadow_x = state.shadow_x_ea46;
    const auto shadow_y = state.shadow_y_ea47;
    DriveEntry drive(payload, state, saved, 1);
    drive.tick();
    std::array<std::uint8_t, 8> expected_shadow{};
    for (std::size_t i = 0; i < 4; ++i) {
        expected_shadow[i * 2] = shadow_x[i + 4];
        expected_shadow[i * 2 + 1] = shadow_y[i + 4];
    }
    check(drive.state().bait_active68 == 0 &&
              drive.saved_bait_targets() == expected_shadow &&
              drive.state().shadow_x_ea46 == shadow_x &&
              drive.state().shadow_y_ea47 == shadow_y,
          "state 19 leaves the bait shadow untouched without active bait");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state19_and_state20(payload);
        test_distance_saturation(payload);
        test_no_bait_save(payload);
        std::cout << "native drive-entry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
