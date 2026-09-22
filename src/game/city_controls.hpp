#pragma once

#include "assets/payload.hpp"
#include "game/account_codec.hpp"
#include "game/city_entry.hpp"
#include "game/notice_scroller.hpp"
#include "game/sound_event.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Values supplied by the surrounding runtime on every invocation of the
// original $7BAB state-18 handler.  CityControls does not synthesize clocks or
// random bytes internally.
struct CityControlsInput {
    std::uint8_t random06 = 0;
    std::uint8_t frame09 = 0;
    std::uint8_t joystick33 = 0xFF;
    std::uint8_t key17 = 0;
};

enum class CityControlsTransition : std::uint8_t {
    city = 0x12,
    drive = 0x13,
    building = 0x16,
    marshmallow = 0x24,
    insufficient_balance = 0x2D,
};

// Native representation of every persistent byte read or written by state
// 18.  The hexadecimal suffixes retain the original zero-page names so raw
// capture fixtures can be mapped without hidden policy.
struct CityControlsState {
    video::CharacterFrame characters{};
    EquipmentSpriteState sprites{};
    NoticeScroller notices{};
    AccountBalanceBytes starting_balance51{};
    AccountBalanceBytes balance57{};
    std::array<std::uint8_t, 8> sprite_control_c0{};
    std::array<std::uint8_t, 4> roamer_counters6f{};
    std::array<std::uint8_t, 20> building_status_c8{};
    std::array<std::uint8_t, 30> map_types_ea28{};
    std::array<std::uint8_t, 8> shadow_x_ea46{};
    std::array<std::uint8_t, 8> shadow_y_ea47{};
    std::array<std::uint8_t, 8> shadow_target_x_ea56{};
    std::array<std::uint8_t, 8> shadow_target_y_ea57{};
    std::uint8_t state3a = 0x12;
    std::uint8_t fire_latch11 = 0;
    std::uint8_t key17 = 0;
    std::uint8_t last_map_cell1f = 0;
    std::uint8_t active_roamer_count28 = 0;
    std::uint8_t move_mask3c = 0x3F;
    std::uint8_t backup_men3d = 3;
    std::uint8_t backpack_charge3e = 0x99;
    std::uint8_t pk_low5a = 0;
    std::uint8_t pk_high5b = 0;
    std::uint8_t route_length66 = 0;
    std::uint8_t bait_active68 = 0;
    std::uint8_t bait69 = 0;
    std::uint8_t empty_traps6b = 0;
    std::uint8_t owned_mask6d = 0;
    std::uint8_t current_building6e = 0;
    std::uint8_t countdown7c = 0;
    std::uint8_t pending_alert80 = 0;
    std::uint8_t finale_active81 = 0;
    std::uint8_t status_d2 = 0;
};

// Native boundary for the complete original state-18 handler $7BAB-$7FC8.
class CityControls {
public:
    CityControls(const assets::Payload& payload, const CityEntry& entry,
                 AccountBalanceBytes starting_balance);
    CityControls(const assets::Payload& payload, CityControlsState state);

    void tick(CityControlsInput input);

    [[nodiscard]] CityControlsState& state() noexcept { return state_; }
    [[nodiscard]] const CityControlsState& state() const noexcept { return state_; }
    [[nodiscard]] CityControlsTransition transition() const noexcept
    {
        return static_cast<CityControlsTransition>(state_.state3a);
    }
    // State 18 and all of its immediate exits make no effect, speech, or music
    // call.  The explicit empty list prevents callers from inferring sounds
    // from notices or actions.
    [[nodiscard]] const SoundEvents& audio_events() const noexcept { return audio_events_; }

private:
    void refresh_sprite_visual(std::size_t sprite);
    void queue_notice(std::uint8_t index);
    void add_pk_energy(std::uint8_t amount, std::uint8_t random);
    void reset_arrived_sprite(std::size_t sprite);
    void move_sprite(std::size_t sprite);
    void move_player(std::uint8_t joystick);
    void update_buildings();
    void detect_roamer_contacts();
    void update_zuul_routes(std::uint8_t random);
    [[nodiscard]] bool handle_fire(std::uint8_t joystick);
    [[nodiscard]] bool handle_alert();
    void update_trail_and_bait();

    const assets::Payload& payload_;
    CityControlsState state_;
    std::vector<std::uint8_t> scene_data_;
    SoundEvents audio_events_;
};

} // namespace ghostbusters::game
