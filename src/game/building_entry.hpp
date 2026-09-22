#pragma once

#include "assets/capture_data.hpp"
#include "game/city_controls.hpp"
#include "game/drive_controls.hpp"
#include "game/sound_event.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Persistent bytes used by States $16/$17 which are outside the existing
// city/drive state types. There are deliberately no field defaults: callers
// importing an original snapshot must supply every retained byte.
struct BuildingEntryRegisters {
    BuildingEntryRegisters(std::uint8_t state1e_value,
                           std::uint8_t scratch37_value,
                           std::uint8_t vic_control1_value)
        : state1e(state1e_value), scratch37(scratch37_value),
          vic_control1(vic_control1_value)
    {
    }

    std::uint8_t state1e;
    std::uint8_t scratch37;
    std::uint8_t vic_control1;
};

enum class BuildingEntryTransition : std::uint8_t {
    normal = 0x18,
    ghostbusters_headquarters = 0x22,
    zuul = 0x28,
};

// Native boundary for the two consecutive building-entry handlers:
// State $16 at $8279-$82AE and State $17 at $82AF-$84F1.
class BuildingEntry {
public:
    enum class Stage { clearing, preparing, ready };

    BuildingEntry(const assets::Payload& payload, CityControlsState city,
                  std::uint8_t vehicle, DriveControlsPersistent persistent,
                  BuildingEntryRegisters registers);
    BuildingEntry(const assets::Payload& payload, const DriveControls& drive,
                  BuildingEntryRegisters registers);
    BuildingEntry(const assets::Payload& payload, DriveControlsState drive,
                  BuildingEntryRegisters registers);

    // One call executes exactly one original state handler.
    void tick();

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] video::CharacterFrame& characters() noexcept
    {
        return state_.city.characters;
    }
    [[nodiscard]] const video::CharacterFrame& characters() const noexcept
    {
        return state_.city.characters;
    }
    [[nodiscard]] EquipmentSpriteState& sprites() noexcept
    {
        return state_.city.sprites;
    }
    [[nodiscard]] const EquipmentSpriteState& sprites() const noexcept
    {
        return state_.city.sprites;
    }
    [[nodiscard]] BuildingEntryTransition transition() const noexcept
    {
        return static_cast<BuildingEntryTransition>(state_.city.state3a);
    }
    [[nodiscard]] std::uint8_t state1e() const noexcept { return registers_.state1e; }
    [[nodiscard]] std::uint8_t scratch37() const noexcept { return registers_.scratch37; }
    [[nodiscard]] std::uint8_t vic_control1() const noexcept
    {
        return registers_.vic_control1;
    }
    [[nodiscard]] bool display_enabled() const noexcept
    {
        return (registers_.vic_control1 & 0x10U) != 0;
    }
    [[nodiscard]] const SoundEvents& audio_events() const noexcept
    {
        return audio_events_;
    }

private:
    void clear_scene();
    void prepare_scene();
    void copy_building_background();
    void apply_low_overlay(std::uint8_t overlay);
    void apply_high_overlay(std::uint8_t overlay);
    void apply_descriptor_charset_patch(std::uint8_t patch);
    void restore_vehicle_graphics();
    void configure_sprites();
    void refresh_sprite_visual(std::size_t sprite);

    assets::CaptureData capture_data_;
    DriveControlsState state_;
    BuildingEntryRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
    SoundEvents audio_events_;
    Stage stage_ = Stage::clearing;
};

} // namespace ghostbusters::game
