#pragma once

#include "assets/equipment_data.hpp"
#include "assets/payload.hpp"
#include "game/account_codec.hpp"
#include "game/sound_event.hpp"
#include "game/text_script.hpp"
#include "video/character_frame.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

struct EquipmentSpriteState {
    std::array<std::uint8_t, 8> pointers{};
    std::array<std::uint8_t, 8> x{};
    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 8> target_x{};
    std::array<std::uint8_t, 8> target_y{};
    // Each pointer addresses one 64-byte block in VIC bank $4000. The data
    // here is resolved from the prepared shared sprite bank.
    std::array<std::array<std::uint8_t, 64>, 8> bitmap_data{};
    std::array<std::uint8_t, 8> colors{};
    std::uint8_t enabled_mask = 0xFF;
    std::uint8_t x_high_mask = 0;
    std::uint8_t y_expand_mask = 0;
    std::uint8_t x_expand_mask = 0;
    std::uint8_t priority_mask = 0;
    std::uint8_t multicolor_mask = 0xFF;
    std::uint8_t shared_multicolor_1 = 1;
    std::uint8_t shared_multicolor_2 = 0;

    [[nodiscard]] bool operator==(const EquipmentSpriteState&) const = default;
};

// $33 is the active-low joystick byte read by state 15. `key` is the
// edge-triggered translated byte that the keyboard scanner places in $17.
struct EquipmentInput {
    std::uint8_t joystick = 0xFF;
    std::uint8_t key = 0;
};

class EquipmentSelection {
public:
    enum class Stage { initial, ready, finished };

    EquipmentSelection(const assets::Payload& payload,
                       const video::CharacterFrame& previous,
                       std::uint8_t vehicle,
                       AccountBalanceBytes balance,
                       std::uint8_t owned_mask,
                       std::uint8_t category);

    void tick(std::uint8_t irq_counter, EquipmentInput input = {});
    // State15 handler alone, for the original handler-level comparison. Live
    // frames call tick(), which first executes the shared text/balance phase.
    void tick_controls(EquipmentInput input = {});

    // True only when this frame executes an original store clearing $17.
    [[nodiscard]] bool clears_input() const noexcept { return clears_input_; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] std::uint8_t original_state() const noexcept { return stage_ == Stage::finished ? 16 : 15; }
    [[nodiscard]] bool script_active() const noexcept { return script_.active(); }
    [[nodiscard]] std::uint8_t column() const noexcept { return script_.column(); }
    [[nodiscard]] std::uint8_t row() const noexcept { return script_.row(); }
    [[nodiscard]] const std::vector<std::uint8_t>& rendered_bytes() const noexcept { return script_.rendered_bytes(); }
    [[nodiscard]] const video::CharacterFrame& characters() const noexcept { return characters_; }
    [[nodiscard]] const EquipmentSpriteState& sprites() const noexcept { return sprites_; }
    [[nodiscard]] AccountBalanceBytes balance() const noexcept { return balance_; }
    [[nodiscard]] std::uint8_t owned_mask() const noexcept { return owned_mask_; }
    [[nodiscard]] std::uint8_t vehicle() const noexcept { return vehicle_; }
    [[nodiscard]] std::uint8_t category() const noexcept { return category_; }
    [[nodiscard]] std::uint8_t state5f() const noexcept { return state5f_; }
    [[nodiscard]] std::uint8_t carried_slot60() const noexcept { return carried_slot60_; }
    [[nodiscard]] std::uint8_t carried_graphic61() const noexcept { return carried_graphic61_; }
    [[nodiscard]] std::uint8_t purchase_count62() const noexcept { return purchase_count62_; }
    [[nodiscard]] std::uint8_t bait69() const noexcept { return bait69_; }
    [[nodiscard]] std::uint8_t traps6a() const noexcept { return traps6a_; }
    [[nodiscard]] std::uint8_t traps6b() const noexcept { return traps6b_; }
    [[nodiscard]] std::uint8_t fire_latch11() const noexcept { return fire_latch11_; }
    [[nodiscard]] const std::vector<std::uint8_t>& pending_notice() const noexcept
    {
        return pending_notice_;
    }
    [[nodiscard]] std::vector<std::uint8_t> take_pending_notice();
    [[nodiscard]] SoundEvents take_sound_events();

private:
    void begin_category(bool first_entry);
    void begin_balance_script();
    void update_shop(EquipmentInput input);
    void update_targets();
    void move_sprites();
    [[nodiscard]] bool sprites_at_targets() const noexcept;
    [[nodiscard]] bool fire_pressed(std::uint8_t joystick) noexcept;
    void purchase_carried();
    void queue_capacity_notice();
    void overlay_carried_on_vehicle();
    void resolve_all_sprite_visuals();

    const assets::Payload& payload_;
    assets::EquipmentData data_;
    video::CharacterFrame characters_;
    EquipmentSpriteState sprites_;
    AccountBalanceBytes balance_;
    TextScript script_;
    bool clears_input_ = false;
    Stage stage_ = Stage::initial;
    std::uint8_t owned_mask_ = 0;
    std::uint8_t vehicle_ = 0;
    std::uint8_t category_ = 0;
    std::uint8_t state5f_ = 0;
    std::uint8_t carried_slot60_ = 0;
    std::uint8_t carried_graphic61_ = 0;
    std::uint8_t purchase_count62_ = 0;
    std::uint8_t bait69_ = 0;
    std::uint8_t traps6a_ = 0;
    std::uint8_t traps6b_ = 0;
    std::uint8_t fire_latch11_ = 0;
    std::vector<std::uint8_t> scene_data_;
    std::vector<std::uint8_t> pending_notice_;
    SoundEvents sound_events_;
};

} // namespace ghostbusters::game
