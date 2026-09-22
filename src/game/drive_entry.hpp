#pragma once

#include "assets/payload.hpp"
#include "game/city_controls.hpp"
#include "video/character_frame.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Native boundary for the original State 19 and State 20 handlers. The
// active driving handler (State 21) starts after Stage::ready and is outside
// this class.
class DriveEntry {
public:
    enum class Stage { entering, preparing, ready };
    using SavedVehicleCharset = std::array<std::uint8_t, 1536>;

    DriveEntry(const assets::Payload& payload, CityControlsState state,
               const SavedVehicleCharset& saved_vehicle_charset,
               std::uint8_t vehicle);

    // One call executes exactly one handler: $7FC9-$8035, then $8036-$8072.
    void tick();

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] CityControlsState& state() noexcept { return state_; }
    [[nodiscard]] const CityControlsState& state() const noexcept { return state_; }
    [[nodiscard]] video::CharacterFrame& characters() noexcept { return state_.characters; }
    [[nodiscard]] const video::CharacterFrame& characters() const noexcept
    {
        return state_.characters;
    }
    [[nodiscard]] EquipmentSpriteState& sprites() noexcept { return state_.sprites; }
    [[nodiscard]] const EquipmentSpriteState& sprites() const noexcept { return state_.sprites; }

    [[nodiscard]] const SavedVehicleCharset& saved_vehicle_charset() const noexcept
    {
        return saved_vehicle_charset_;
    }
    [[nodiscard]] std::uint8_t vehicle() const noexcept { return vehicle_; }
    [[nodiscard]] std::uint8_t distance67() const noexcept { return distance67_; }
    [[nodiscard]] std::uint8_t vehicle_position63() const noexcept { return vehicle_position63_; }
    [[nodiscard]] std::uint8_t state1a() const noexcept { return state1a_; }
    [[nodiscard]] std::uint8_t state1b() const noexcept { return state1b_; }
    [[nodiscard]] std::uint8_t state1c() const noexcept { return state1c_; }
    [[nodiscard]] std::uint8_t state17() const noexcept { return state_.key17; }
    [[nodiscard]] const std::array<std::uint8_t, 8>& saved_bait_targets() const noexcept
    {
        return saved_bait_targets_ea4e_;
    }

private:
    void begin_drive();
    void prepare_drive();
    void clear_screen_and_color();
    void draw_vehicle_grid();
    void resolve_sprite_visuals();

    const assets::Payload& payload_;
    CityControlsState state_;
    SavedVehicleCharset saved_vehicle_charset_{};
    std::array<std::uint8_t, 8> saved_bait_targets_ea4e_{};
    std::vector<std::uint8_t> scene_data_;
    std::uint8_t vehicle_ = 0;
    std::uint8_t distance67_ = 0;
    std::uint8_t vehicle_position63_ = 0;
    std::uint8_t state1a_ = 0;
    std::uint8_t state1b_ = 0;
    std::uint8_t state1c_ = 0;
    Stage stage_ = Stage::entering;
};

} // namespace ghostbusters::game
