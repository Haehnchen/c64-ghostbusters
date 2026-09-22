#pragma once

#include "assets/marshmallow_data.hpp"
#include "game/city_controls.hpp"
#include "game/sound_event.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ghostbusters::game {

// Helper bytes retained outside CityControlsState by States $24-$27. There
// are deliberately no field defaults so capture fixtures supply all values.
struct MarshmallowRegisters {
    MarshmallowRegisters(std::uint8_t scratch23_value,
                         std::uint8_t scratch24_value,
                         std::uint8_t scratch25_value)
        : scratch23(scratch23_value), scratch24(scratch24_value),
          scratch25(scratch25_value)
    {
    }

    std::uint8_t scratch23;
    std::uint8_t scratch24;
    std::uint8_t scratch25;
};

enum class MarshmallowTransition : std::uint8_t {
    city = 0x12,
    approach = 0x24,
    attack = 0x25,
    baited = 0x26,
    returning = 0x27,
};

// Native boundary for States $24-$27 at $8925-$8A2B. The surrounding common
// frame owns countdown decrement, keyboard translation, notices and PK ticks;
// tick() executes exactly one dispatched state handler.
class Marshmallow {
public:
    Marshmallow(const assets::Payload& payload, CityControlsState state,
                MarshmallowRegisters registers);

    void tick();

    [[nodiscard]] CityControlsState& state() noexcept { return state_; }
    [[nodiscard]] const CityControlsState& state() const noexcept { return state_; }
    [[nodiscard]] MarshmallowRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const MarshmallowRegisters& registers() const noexcept
    {
        return registers_;
    }
    [[nodiscard]] MarshmallowTransition transition() const noexcept
    {
        return static_cast<MarshmallowTransition>(state_.state3a);
    }
    [[nodiscard]] const SoundEvents& audio_events() const noexcept
    {
        return audio_events_;
    }
    // Present only when this tick painted a new trail cell; relative to colors.
    [[nodiscard]] std::optional<std::size_t> trail_color_row_offset() const noexcept
    {
        return trail_color_row_offset_;
    }

private:
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void move_roamers();
    void move_roamer(std::size_t sprite);
    [[nodiscard]] bool roamer_at_target(std::size_t sprite) const noexcept;
    [[nodiscard]] bool all_roamers_at_targets() const noexcept;
    void queue_notice(std::uint8_t index);
    void update_trail_and_bait();
    void draw_city_map_quadrant(std::uint8_t building, std::uint8_t quadrant);
    void restore_city_roamers();
    void add_reward();

    assets::MarshmallowData data_;
    CityControlsState state_;
    MarshmallowRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
    SoundEvents audio_events_;
    std::optional<std::size_t> trail_color_row_offset_;
};

} // namespace ghostbusters::game
