#include "game/drive_entry.hpp"
#include "assets/vehicle_data.hpp"
#include "assets/capture_data.hpp"

#include "game/title_screen.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kVehicleCharsetOffset = 0x0200;
constexpr std::size_t kClearedScreenLength = 0x03C0;
constexpr std::size_t kStatusColorOffset = 0x0370;
constexpr std::size_t kStatusColorLength = 0x0027;
constexpr std::size_t kVehicleGridRow = 4;
constexpr std::size_t kVehicleGridColumn = 25;
constexpr std::size_t kVehicleGridColumns = 12;
constexpr std::size_t kVehicleGridRows = 16;

} // namespace

DriveEntry::DriveEntry(const assets::Payload& payload, CityControlsState state,
                       const SavedVehicleCharset& saved_vehicle_charset,
                       std::uint8_t vehicle)
    : payload_(payload), state_(std::move(state)),
      saved_vehicle_charset_(saved_vehicle_charset), vehicle_(vehicle)
{
    if (vehicle_ >= 4) throw std::out_of_range("Vehicle index must be in 0..3");

    scene_data_ = shared_sprite_data(payload_);
    for (std::size_t i = 0; i < 4; ++i) {
        saved_bait_targets_ea4e_[i * 2] = state_.shadow_x_ea46[i + 4];
        saved_bait_targets_ea4e_[i * 2 + 1] = state_.shadow_y_ea47[i + 4];
    }
}

void DriveEntry::tick()
{
    if (stage_ == Stage::entering) {
        begin_drive();
    } else if (stage_ == Stage::preparing) {
        prepare_drive();
    }
}

void DriveEntry::clear_screen_and_color()
{
    // The clear covers 960 cells; the final 64 screen/color bytes survive.
    std::fill_n(state_.characters.screen.begin(), kClearedScreenLength, 0);
    std::fill_n(state_.characters.colors.begin(), kClearedScreenLength, 0);
    // Active scene transitions retain a white status row.
    if (state_.state3a >= 0x11) {
        std::fill_n(state_.characters.colors.begin() + kStatusColorOffset,
                    kStatusColorLength, 1);
    }
}

void DriveEntry::begin_drive()
{
    // The active-drive handler still needs the separate control-pointer shadow.
    state_.sprites.pointers.fill(0);
    resolve_sprite_visuals();
    clear_screen_and_color();

    state_.characters.background = 0x0C;
    state_.characters.multicolor1 = 1;
    state_.characters.multicolor2 = assets::VehicleData(payload_).multicolor2(vehicle_);

    if (state_.bait_active68 != 0) {
        state_.bait_active68 = 0;
        // Bait targets for sprites 4..7 survive the city-to-drive transition.
        for (std::size_t i = 0; i < 4; ++i) {
            const auto x = state_.sprites.target_x[i + 4];
            const auto y = state_.sprites.target_y[i + 4];
            saved_bait_targets_ea4e_[i * 2] = x;
            saved_bait_targets_ea4e_[i * 2 + 1] = y;
            state_.shadow_x_ea46[i + 4] = x;
            state_.shadow_y_ea47[i + 4] = y;
        }
    }

    const auto sum = static_cast<unsigned>(state_.route_length66) + 5U;
    distance67_ = sum > 0xFFU ? 0xFF : static_cast<std::uint8_t>(sum);
    state_.route_length66 = 0;

    // Restore the modified vehicle glyphs, including the purchased equipment.
    std::copy(saved_vehicle_charset_.begin(), saved_vehicle_charset_.end(),
              state_.characters.charset.begin() + kVehicleCharsetOffset);

    state1a_ = 0;
    state1b_ = 0;
    state1c_ = 0;
    state_.key17 = 0;

    state_.state3a = static_cast<std::uint8_t>(state_.state3a + 1U);
    stage_ = Stage::preparing;
}

void DriveEntry::draw_vehicle_grid()
{
    // Each column uses a consecutive range of 16 glyphs.
    for (std::size_t column = 0; column < kVehicleGridColumns; ++column) {
        const auto first = assets::VehicleData(payload_).grid_first_codes()[column];
        for (std::size_t row = 0; row < kVehicleGridRows; ++row) {
            const auto offset = (kVehicleGridRow + row) * 40 +
                                kVehicleGridColumn + column;
            state_.characters.screen[offset] = static_cast<std::uint8_t>(first + row);
            state_.characters.colors[offset] = 8;
        }
    }
}

void DriveEntry::prepare_drive()
{
    vehicle_position63_ = 0x64;
    draw_vehicle_grid();

    // Keep the white status row below the first 880 color cells.
    std::fill_n(state_.characters.colors.begin(), 0x0370, 8);
    state_.sprites.y_expand_mask = 0xF0;

    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const auto initial = assets::VehicleData(payload_).initial_drive_sprite(sprite);
        state_.sprites.x[sprite] = initial.x;
        state_.sprites.y[sprite] = initial.y;
        state_.sprites.pointers[sprite] = initial.pointer;
    }
    state_.sprites.priority_mask = 0xF0;
    resolve_sprite_visuals();

    state_.key17 = 0;
    state_.state3a = static_cast<std::uint8_t>(state_.state3a + 1U);
    stage_ = Stage::ready;
}

void DriveEntry::resolve_sprite_visuals()
{
    for (std::size_t sprite = 0; sprite < state_.sprites.pointers.size(); ++sprite) {
        const auto pointer = state_.sprites.pointers[sprite];
        const auto offset = static_cast<std::size_t>(pointer) * 64U;
        if (offset + state_.sprites.bitmap_data[sprite].size() > scene_data_.size()) {
            throw std::out_of_range("drive sprite pointer");
        }
        std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset),
                    state_.sprites.bitmap_data[sprite].size(),
                    state_.sprites.bitmap_data[sprite].begin());
        state_.sprites.colors[sprite] = assets::CaptureData(payload_).sprite_color(pointer);
    }
}

} // namespace ghostbusters::game
