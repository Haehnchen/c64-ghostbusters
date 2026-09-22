#include "game/building_entry.hpp"

#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kClearedLength = 0x03C0;
constexpr std::size_t kStatusColorOffset = 0x0370;
constexpr std::size_t kStatusColorLength = 0x0027;
constexpr std::size_t kCharset5A00 = 0x0200;

constexpr std::array<std::uint8_t, 4> kReplacementColors{
    0x0A, 0x08, 0x09, 0x0D,
};

} // namespace

BuildingEntry::BuildingEntry(const assets::Payload& payload, CityControlsState city,
                             std::uint8_t vehicle, DriveControlsPersistent persistent,
                             BuildingEntryRegisters registers)
    : capture_data_(payload), registers_(registers)
{
    state_.city = std::move(city);
    state_.vehicle5c = vehicle;
    state_.distance67 = persistent.retained67;
    state_.vehicle_position63 = persistent.retained63;
    state_.direction64 = persistent.direction64;
    state_.animation65 = persistent.animation65;
    state_.scroll_position1a = persistent.retained1a;
    state_.speed1b = persistent.retained1b;
    state_.scroll_fraction1c = persistent.retained1c;
    state_.fire_latch13 = persistent.fire_latch13;
    state_.roamer_source73 = persistent.roamer_source73;
    state_.capture_timer75 = persistent.capture_timer75;
    if (state_.city.state3a != 0x16) {
        throw std::invalid_argument("BuildingEntry requires State $16");
    }
    if (vehicle >= 4) throw std::out_of_range("Vehicle index must be in 0..3");

    scene_data_.assign(capture_data_.scene_graphics().begin(),
                       capture_data_.scene_graphics().end());
}

BuildingEntry::BuildingEntry(const assets::Payload& payload, const DriveControls& drive,
                             BuildingEntryRegisters registers)
    : BuildingEntry(payload, drive.state(), registers)
{
}

BuildingEntry::BuildingEntry(const assets::Payload& payload, DriveControlsState drive,
                             BuildingEntryRegisters registers)
    : capture_data_(payload), state_(std::move(drive)), registers_(registers)
{
    if (state_.city.state3a != 0x16) {
        throw std::invalid_argument("BuildingEntry requires State $16");
    }
    if (state_.vehicle5c >= 4) {
        throw std::out_of_range("Vehicle index must be in 0..3");
    }

    scene_data_.assign(capture_data_.scene_graphics().begin(),
                       capture_data_.scene_graphics().end());
}

void BuildingEntry::tick()
{
    if (stage_ == Stage::clearing) {
        clear_scene();
    } else if (stage_ == Stage::preparing) {
        prepare_scene();
    }
}

void BuildingEntry::clear_scene()
{
    auto& city = state_.city;
    auto& frame = city.characters;

    // $95B9 clears four overlapping pages. State $16 also takes its status-row
    // branch, and the helper always leaves zero in scratch byte $37.
    std::fill_n(frame.screen.begin(), kClearedLength, 0);
    std::fill_n(frame.colors.begin(), kClearedLength, 0);
    std::fill_n(frame.colors.begin() + kStatusColorOffset, kStatusColorLength, 1);
    registers_.scratch37 = 0;

    state_.animation65 = 0;
    city.sprites.x.fill(0);
    city.sprites.y.fill(0);
    city.roamer_counters6f.fill(0);
    frame.background = 0x0C;
    frame.multicolor1 = 0;

    // The four stores based at $D800/$D900/$DA00/$DA70 cover exactly the
    // first 880 color cells, ending immediately before the status colors.
    std::fill_n(frame.colors.begin(), kStatusColorOffset, 9);
    city.route_length66 = 0;

    city.key17 = 0;
    city.state3a = static_cast<std::uint8_t>(city.state3a + 1U);
    stage_ = Stage::preparing;
}

void BuildingEntry::copy_building_background()
{
    auto& frame = state_.city.characters;
    const bool zuul = state_.city.current_building6e == 0x0A;

    // $9D96 copies six complete pages into $5A00-$5FFF.
    const auto charset = capture_data_.building_charset(zuul);
    std::copy_n(charset.begin(), charset.size(),
                frame.charset.begin() + kCharset5A00);

    // $9DA9 starts at Y=$47. After the first 72 bytes Y is $FF; each of the
    // remaining three decremented pages therefore copies all 256 bytes. The
    // combined destination and source ranges are contiguous 840-byte spans.
    const auto screen = capture_data_.building_screen(zuul);
    std::copy_n(screen.begin(), screen.size(),
                frame.screen.begin());

    const auto descriptor =
        capture_data_.building_descriptor(state_.city.current_building6e);
    const auto replacement = kReplacementColors[descriptor & 0x03U];
    for (std::size_t offset = 0; offset < 0x0348; ++offset) {
        // The inline loop branches back to its STA (rather than its LDA) at
        // page joins. Offsets $FF/$1FF/$2FF therefore duplicate the next
        // page's first source byte and skip the preceding page's $FF byte.
        const auto page_join = offset == 0x00FF || offset == 0x01FF ||
                               offset == 0x02FF;
        auto color = capture_data_.building_color(
            zuul, offset + (page_join ? 1U : 0U));
        if (color == 0x0A) color = replacement;
        // The comparison above uses the full source byte. Only the subsequent
        // store to physical Color RAM discards its high nibble.
        frame.colors[offset] = static_cast<std::uint8_t>(color & 0x0FU);
    }
}

void BuildingEntry::apply_low_overlay(std::uint8_t overlay)
{
    if (overlay == 0) return;
    auto& frame = state_.city.characters;
    for (std::size_t repeat = 0; repeat < 4; ++repeat) {
        for (std::size_t index = 0; index < 0x18; ++index) {
            const auto destination = static_cast<std::size_t>(0x30 + repeat * 6U +
                capture_data_.low_overlay_destination(index));
            frame.screen[destination] = capture_data_.low_overlay_tile(overlay, index);
        }
    }

    const auto charset = capture_data_.low_overlay_charset(overlay);
    std::copy_n(charset.begin(), charset.size(), frame.charset.begin() + 0x0330);
}

void BuildingEntry::apply_high_overlay(std::uint8_t overlay)
{
    if (overlay == 0) return;
    auto& frame = state_.city.characters;
    const auto screen = capture_data_.high_overlay_screen(overlay);
    for (std::size_t row = 0; row < 10; ++row) {
        for (std::size_t column = 0; column < 22; ++column) {
            const auto destination = 0x0120U + row * 40U + column;
            frame.screen[destination] = screen[row * 22U + column];
            frame.colors[destination] = 9;
        }
    }

    const auto charset = capture_data_.high_overlay_charset(overlay);
    std::copy_n(charset.begin(), charset.size(), frame.charset.begin() + 0x03D0);
}

void BuildingEntry::apply_descriptor_charset_patch(std::uint8_t patch)
{
    if (patch == 0) return;
    const auto charset = capture_data_.descriptor_charset_patch(patch);
    std::copy_n(charset.begin(), charset.size(),
                state_.city.characters.charset.begin() + 0x0268);
}

void BuildingEntry::restore_vehicle_graphics()
{
    auto& frame = state_.city.characters;
    const auto vehicle = state_.vehicle5c;
    const auto charset = capture_data_.vehicle_charset(vehicle);
    std::copy_n(charset.begin(), charset.size(), frame.charset.begin() + kCharset5A00);

    const auto color = static_cast<std::uint8_t>(
        capture_data_.vehicle_color(vehicle) | 0x08U);
    for (std::size_t column = 0; column < 4; ++column) {
        auto code = static_cast<std::uint8_t>(0x40 + column);
        for (std::size_t row = 0; row < 3; ++row) {
            const auto destination = 0x02F2U + row * 40U + column;
            frame.screen[destination] = code;
            frame.colors[destination] = color;
            code = static_cast<std::uint8_t>(code + 4U);
        }
    }
}

void BuildingEntry::refresh_sprite_visual(std::size_t sprite)
{
    const auto pointer = state_.city.sprites.pointers[sprite];
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("building-entry sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                state_.city.sprites.bitmap_data[sprite].begin());
}

void BuildingEntry::configure_sprites()
{
    auto& sprites = state_.city.sprites;
    sprites.priority_mask = 0;
    sprites.x_expand_mask = 0;
    sprites.y_expand_mask = 0;
    sprites.multicolor_mask = 0xEF;

    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        sprites.pointers[sprite] = capture_data_.initial_sprite_pointer(sprite);
        const auto position = capture_data_.initial_sprite_position(sprite);
        const auto target = capture_data_.initial_sprite_target(sprite);
        sprites.x[sprite] = position[0];
        sprites.y[sprite] = position[1];
        sprites.target_x[sprite] = target[0];
        sprites.target_y[sprite] = target[1];
        refresh_sprite_visual(sprite);
    }
}

void BuildingEntry::prepare_scene()
{
    auto& city = state_.city;
    auto& frame = city.characters;
    registers_.vic_control1 = static_cast<std::uint8_t>(registers_.vic_control1 & 0xEFU);

    copy_building_background();
    const auto descriptor = capture_data_.building_descriptor(city.current_building6e);
    apply_low_overlay(static_cast<std::uint8_t>((descriptor >> 4U) & 0x03U));
    apply_high_overlay(static_cast<std::uint8_t>((descriptor >> 6U) & 0x03U));

    if (city.current_building6e == 0x11) {
        for (std::size_t index = 0; index < 12; ++index) {
            frame.screen[0x014D + index] = static_cast<std::uint8_t>(
                capture_data_.headquarters_sign(index) & 0x3FU);
            frame.colors[0x014D + index] = 2;
        }
    }

    apply_descriptor_charset_patch(static_cast<std::uint8_t>((descriptor >> 2U) & 0x03U));
    restore_vehicle_graphics();

    city.key17 = 0;
    frame.multicolor2 = 0x0B;
    registers_.state1e = 1;
    configure_sprites();

    if (city.current_building6e == 0x11) {
        city.state3a = 0x22;
    } else if (city.current_building6e == 0x0A) {
        city.state3a = 0x28;
    } else {
        city.countdown7c = 0x7F;
        city.state3a = static_cast<std::uint8_t>(city.state3a + 1U);
    }
    stage_ = Stage::ready;
}

} // namespace ghostbusters::game
