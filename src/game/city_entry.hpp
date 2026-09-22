#pragma once

#include "assets/city_data.hpp"
#include "game/equipment_selection.hpp"
#include "video/character_frame.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

struct CityControlsState;

// Native boundary for original states 16 and 17. Each tick executes one
// complete state handler and stops before the next state's update begins.
class CityEntry {
public:
    enum class Stage { leaving_shop, preparing_city, ready };

    CityEntry(const assets::Payload& payload, const EquipmentSelection& shop);

    void tick();

    // State 17 only: restore dynamic map and saved sprite coordinates without
    // repeating the shop exit or requesting a music reset. Caller clears the
    // external registers $1D/$1E/$37/$77 written by this handler.
    static void redraw(const assets::Payload& payload, CityControlsState& state);

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] const video::CharacterFrame& characters() const noexcept { return characters_; }
    [[nodiscard]] const EquipmentSpriteState& sprites() const noexcept { return sprites_; }
    [[nodiscard]] const std::array<std::uint8_t, 1536>& saved_vehicle_charset() const noexcept
    {
        return saved_vehicle_charset_;
    }
    [[nodiscard]] AccountBalanceBytes balance() const noexcept { return balance_; }
    [[nodiscard]] std::uint8_t vehicle() const noexcept { return vehicle_; }
    [[nodiscard]] std::uint8_t category() const noexcept { return category_; }
    [[nodiscard]] std::uint8_t state5e() const noexcept { return state5e_; }
    [[nodiscard]] std::uint8_t state5f() const noexcept { return state5f_; }
    [[nodiscard]] std::uint8_t carried_slot60() const noexcept { return carried_slot60_; }
    [[nodiscard]] std::uint8_t carried_graphic61() const noexcept { return carried_graphic61_; }
    [[nodiscard]] std::uint8_t purchase_count() const noexcept { return purchase_count_; }
    [[nodiscard]] std::uint8_t bait() const noexcept { return bait_; }
    [[nodiscard]] std::uint8_t traps6a() const noexcept { return traps6a_; }
    [[nodiscard]] std::uint8_t traps6b() const noexcept { return traps6b_; }
    [[nodiscard]] std::uint8_t owned_mask() const noexcept { return owned_mask_; }
    [[nodiscard]] std::uint8_t fire_latch11() const noexcept { return fire_latch11_; }
    [[nodiscard]] const std::vector<std::uint8_t>& pending_notice() const noexcept
    {
        return pending_notice_;
    }
    [[nodiscard]] bool take_music_reset_request() noexcept;

private:
    CityEntry(const assets::Payload& payload, const CityControlsState& state);
    void leave_shop();
    void prepare_city(const CityControlsState* restored = nullptr);
    void build_map_tiles(const CityControlsState* restored);
    void resolve_city_sprite_visuals();

    assets::CityData city_data_;
    video::CharacterFrame characters_;
    EquipmentSpriteState sprites_;
    std::array<std::uint8_t, 1536> saved_vehicle_charset_{};
    AccountBalanceBytes balance_{};
    Stage stage_ = Stage::leaving_shop;
    std::uint8_t vehicle_ = 0;
    std::uint8_t category_ = 0;
    std::uint8_t state5e_ = 0;
    std::uint8_t state5f_ = 0;
    std::uint8_t carried_slot60_ = 0;
    std::uint8_t carried_graphic61_ = 0;
    std::uint8_t purchase_count_ = 0;
    std::uint8_t bait_ = 0;
    std::uint8_t traps6a_ = 0;
    std::uint8_t traps6b_ = 0;
    std::uint8_t owned_mask_ = 0;
    std::uint8_t fire_latch11_ = 0;
    std::vector<std::uint8_t> pending_notice_;
    std::vector<std::uint8_t> scene_data_;
    bool music_reset_requested_ = false;
};

} // namespace ghostbusters::game
