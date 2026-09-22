#include "game/equipment_selection.hpp"

#include "game/bcd_money.hpp"
#include "game/money_text.hpp"
#include "game/title_screen.hpp"
#include "game/vehicle_graphics.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

void resolve_sprite_visual(std::span<const std::uint8_t> scene_data,
                           const assets::EquipmentData& data,
                           EquipmentSpriteState& sprites, std::size_t sprite)
{
    const auto source = scene_data.begin() + sprites.pointers[sprite] * 64U;
    std::copy_n(source, 64, sprites.bitmap_data[sprite].begin());
    sprites.colors[sprite] = data.sprite_color(sprites.pointers[sprite]);
}

std::uint8_t expanded_multicolor_mask(std::uint8_t value)
{
    // $6074-$6084 retains the original bits as well as their adjacent copy.
    // Each nonzero bit therefore protects/clears its complete two-bit pixel.
    return static_cast<std::uint8_t>(value | ((value & 0x55U) << 1U) |
                                     ((value & 0xAAU) >> 1U));
}

} // namespace

EquipmentSelection::EquipmentSelection(const assets::Payload& payload,
                                       const video::CharacterFrame& previous,
                                       std::uint8_t vehicle,
                                       AccountBalanceBytes balance,
                                       std::uint8_t owned_mask,
                                       std::uint8_t category)
    : payload_(payload), data_(payload), characters_(previous), balance_(balance),
      owned_mask_(owned_mask), vehicle_(vehicle), category_(category)
{
    if (vehicle_ >= 4) throw std::out_of_range("Vehicle index must be in 0..3");
    if (category_ >= 3) throw std::out_of_range("Equipment category must be in 0..2");

    // States 12 and 13 prepare the purchased vehicle immediately before the
    // state-14 entry. Keeping that bridge here makes the native handoff
    // independent of whether VehicleSelection previously showed a preview.
    prepare_vehicle_graphics(payload_, characters_, vehicle_);
    // The verified vehicle-purchase path clears all pointers before state 14.
    sprites_.pointers.fill(0);

    scene_data_.assign(data_.scene_graphics().begin(),
                       data_.scene_graphics().end());

    // $7733-$773F. X expansion remains zero at the $72F0 script checkpoint;
    // its $06 setup belongs to the later state-15 update.
    sprites_.y_expand_mask = 0;
    sprites_.x_expand_mask = 0;
    sprites_.priority_mask = 0;
    sprites_.multicolor_mask = 0xFF;
    // The game IRQ writes these shadow fields to $D025/$D026 and enables all
    // eight sprites at $8F71-$8FB3 while the UI state is below 18.
    sprites_.enabled_mask = 0xFF;
    sprites_.x_high_mask = 0;
    sprites_.shared_multicolor_1 = 1; // $7739-$773B writes $1D.
    sprites_.shared_multicolor_2 = 0; // $772F-$7731 writes $1E.

    begin_category(true);
}

void EquipmentSelection::begin_category(bool first_entry)
{
    characters_.multicolor1 = 1;
    characters_.multicolor2 = data_.vehicle_multicolor2(vehicle_);

    sprites_.pointers[0] = 1;
    if (first_entry) {
        sprites_.pointers[1] = 0;
        sprites_.pointers[2] = 0;
    }
    for (std::size_t item = 0; item < 5; ++item) {
        auto pointer = data_.item_pointer(category_, item);
        if (pointer != 0) {
            const auto mask = data_.ownership_mask(pointer);
            if ((owned_mask_ & mask) != 0) pointer = 0;
        }
        sprites_.pointers[item + 3] = pointer;
    }

    for (std::size_t sprite = 0; sprite < 3; ++sprite) {
        const auto position = data_.initial_sprite_position(sprite);
        sprites_.x[sprite] = sprites_.target_x[sprite] = position.x;
        sprites_.y[sprite] = sprites_.target_y[sprite] = position.y;
    }
    for (std::size_t item = 0; item < 5; ++item) {
        const auto sprite = item + 3;
        const auto position = data_.initial_sprite_position(sprite);
        sprites_.x[sprite] = sprites_.target_x[sprite] = position.x;
        sprites_.y[sprite] = sprites_.target_y[sprite] = position.y;
    }
    if (first_entry) sprites_.x_expand_mask = 0;
    resolve_all_sprite_visuals();

    // $7658 leaves the script cursor at (1,1), both on first entry and after
    // selecting another category through states 13/14.
    script_.start(data_.category_script(category_), 1, 1, 0);
    stage_ = Stage::initial;
}

void EquipmentSelection::tick(const std::uint8_t irq_counter, EquipmentInput input)
{
    clears_input_ = false;
    if (stage_ == Stage::finished) return;
    if (script_.active()) {
        script_.tick(characters_, irq_counter, 0);
        auto events = script_.take_sound_events();
        sound_events_.insert(sound_events_.end(), events.begin(), events.end());
        if (script_.active()) return;
        stage_ = Stage::ready;
    }
    // $7399-$73BA runs after FF, then dispatches State15 in this same frame.
    begin_balance_script();
    tick_controls(input);
}

void EquipmentSelection::tick_controls(EquipmentInput input)
{
    clears_input_ = false;
    if (stage_ == Stage::ready) update_shop(input);
}

void EquipmentSelection::begin_balance_script()
{
    const auto label = data_.balance_label();
    for (unsigned i = 0; i < label.size(); ++i) {
        characters_.screen[0x1A + i] = label[i];
        characters_.screen[0x6A + i] = 0x2D;
    }
    script_.start(format_money_field(balance_), 0x1C, 1, 1);
}

void EquipmentSelection::move_sprites()
{
    const auto approach = [](std::uint8_t& current, std::uint8_t target) {
        if (current < target) {
            current = static_cast<std::uint8_t>(current + std::min<unsigned>(2, target - current));
        } else if (current > target) {
            current = static_cast<std::uint8_t>(current - std::min<unsigned>(2, current - target));
        }
    };
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        approach(sprites_.x[sprite], sprites_.target_x[sprite]);
        approach(sprites_.y[sprite], sprites_.target_y[sprite]);
    }
}

bool EquipmentSelection::sprites_at_targets() const noexcept
{
    return sprites_.x == sprites_.target_x && sprites_.y == sprites_.target_y;
}

bool EquipmentSelection::fire_pressed(std::uint8_t joystick) noexcept
{
    const auto fire = static_cast<std::uint8_t>(joystick & 0x10U);
    const bool changed = fire != fire_latch11_;
    fire_latch11_ = fire;
    return changed && fire == 0;
}

void EquipmentSelection::update_targets()
{
    if (carried_slot60_ == 0) {
        state5f_ &= 0x1CU;
        sprites_.pointers[0] = 1;
        sprites_.pointers[1] = 3;
        sprites_.pointers[2] = 3;
        sprites_.target_y[0] = data_.carried_target_y(state5f_ * 2U);
        sprites_.target_x[0] = data_.forklift_x(state5f_ & 1U);
        const auto fork_x = static_cast<std::uint8_t>(sprites_.x[0] - 0x18U);
        for (std::size_t sprite = 1; sprite <= 2; ++sprite) {
            sprites_.x[sprite] = sprites_.target_x[sprite] = fork_x;
            sprites_.y[sprite] = sprites_.target_y[sprite] = sprites_.y[0];
        }
        return;
    }

    const auto carried = static_cast<std::size_t>(carried_slot60_);
    auto index = static_cast<std::uint8_t>(data_.vehicle_position_base(vehicle_) +
                                           ((state5f_ & 0x0CU) << 1U));
    if ((state5f_ & 3U) != 0) index = static_cast<std::uint8_t>(index + carried_graphic61_);
    sprites_.target_x[carried] = data_.carried_target_x(index);
    sprites_.target_x[0] = static_cast<std::uint8_t>(sprites_.target_x[carried] - 0x10U);

    std::uint8_t trap_offset = 0;
    if (carried_graphic61_ == 5 && (state5f_ & 3U) != 0) {
        trap_offset = static_cast<std::uint8_t>(traps6a_ * 3U);
    }
    const auto target_y = static_cast<std::uint8_t>(
        data_.carried_target_y(index) - trap_offset);
    sprites_.target_y[carried] = target_y;
    sprites_.target_y[0] = target_y;
    sprites_.target_y[1] = target_y;
    sprites_.target_y[2] = target_y;

    sprites_.pointers[0] = 2;
    sprites_.pointers[1] = 3;
    sprites_.pointers[2] = 3;
    sprites_.target_x[2] = static_cast<std::uint8_t>(sprites_.x[carried] - 8U);
    const auto left = static_cast<std::uint8_t>(sprites_.target_x[2] - 11U);
    sprites_.target_x[0] = left < 0x4C ? left : 0x4C;
    sprites_.target_x[1] = static_cast<std::uint8_t>(sprites_.target_x[0] + 12U);
}

void EquipmentSelection::update_shop(EquipmentInput input)
{
    move_sprites();
    sprites_.x_expand_mask = 0x06;
    update_targets();

    const auto row = static_cast<std::uint8_t>(state5f_ >> 2U);
    if ((state5f_ & 3U) == 0 && fire_pressed(input.joystick)) {
        if (carried_slot60_ == 0) {
            const auto slot = static_cast<std::uint8_t>(row + 3U);
            if (sprites_.pointers[slot] != 0) {
                carried_slot60_ = slot;
                carried_graphic61_ = data_.carried_graphic(category_, row);
            }
        } else if (carried_slot60_ == static_cast<std::uint8_t>(row + 3U)) {
            sprites_.target_x[carried_slot60_] = 0x14;
            carried_slot60_ = 0;
            carried_graphic61_ = 0;
        }
    }

    const bool settled = sprites_at_targets();
    sound_events_.push_back(settled ? SoundEvent::equipment_motor_stop
                                    : SoundEvent::equipment_motor_start);
    if (settled) {
        if ((input.joystick & 0x08U) == 0 && (state5f_ & 3U) == 0) {
            ++state5f_;
        } else if ((input.joystick & 0x04U) == 0 && (state5f_ & 3U) != 0) {
            --state5f_;
        } else if ((state5f_ & 3U) == 0 && carried_slot60_ == 0) {
            if ((input.joystick & 0x02U) == 0 && state5f_ < 0x10) {
                state5f_ = static_cast<std::uint8_t>(state5f_ + 4U);
            }
            if ((input.joystick & 0x01U) == 0 && (state5f_ & 0x1CU) != 0) {
                state5f_ = static_cast<std::uint8_t>(state5f_ - 4U);
            }
        }
    }

    if ((state5f_ & 3U) == 0) {
        const auto limit = data_.category_state_limit(category_);
        if (state5f_ >= limit) state5f_ = static_cast<std::uint8_t>(limit - 1U);
    }

    if (fire_pressed(input.joystick)) {
        if (purchase_count62_ >= data_.vehicle_capacity(vehicle_)) {
            queue_capacity_notice();
        } else if ((state5f_ & 3U) != 0 && sprites_at_targets()) {
            purchase_carried();
        }
    }

    if (input.key == 0x45) {
        clears_input_ = true; // $7A65, even if no trap permits exit.
        if (traps6a_ != 0) stage_ = Stage::finished;
    } else if (input.key >= 0x31 && input.key < 0x34) {
        const auto next = static_cast<std::uint8_t>((input.key & 3U) - 1U);
        if (next != category_) {
            clears_input_ = true; // $7A86; selecting the same category retains $17.
            category_ = next;
            state5f_ = 0;
            carried_slot60_ = 0;
            carried_graphic61_ = 0;
            begin_category(false);
        }
    }
    resolve_all_sprite_visuals();
}

void EquipmentSelection::queue_capacity_notice()
{
    // L99CC does nothing while the shared scrolling-message buffer is busy.
    if (!pending_notice_.empty()) return;
    for (auto value : data_.capacity_notice()) {
        if (value == 0x20) value = 0;
        else if (value >= 0x40) value = static_cast<std::uint8_t>(value - 0x40U);
        pending_notice_.push_back(value);
    }
}

void EquipmentSelection::purchase_carried()
{
    const auto slot = static_cast<std::size_t>(carried_slot60_);
    const auto pointer = sprites_.pointers[slot];
    const AccountBalanceBytes price{0, data_.price_middle_bcd(pointer), 0};
    if (!canAfford(balance_, price)) return;

    balance_ = subtractMoney(balance_, price);
    // Traps deliberately have an ownership mask of zero. They remain visible
    // and may be bought repeatedly, while still consuming vehicle capacity.
    owned_mask_ |= data_.ownership_mask(pointer);
    ++purchase_count62_;
    if (pointer == 0x38) {
        ++traps6a_;
        ++traps6b_;
    }
    if (pointer == 0x36) bait69_ = static_cast<std::uint8_t>(bait69_ + 5U);

    overlay_carried_on_vehicle();

    const auto home_index = static_cast<std::uint8_t>((carried_slot60_ - 3U) * 8U);
    const auto home_y = data_.carried_target_y(home_index);
    sprites_.y[slot] = sprites_.target_y[slot] = home_y;
    sprites_.x[slot] = 0;
    sprites_.target_x[slot] = 0x14;
    carried_slot60_ = 0;
    if (pointer != 0x38) sprites_.pointers[slot] = 0;
}

void EquipmentSelection::overlay_carried_on_vehicle()
{
    const auto slot = static_cast<std::size_t>(carried_slot60_);
    const auto source = static_cast<std::size_t>(sprites_.pointers[slot]) * 64U;
    const auto phase = static_cast<std::uint8_t>((sprites_.x[slot] - 0x57U) & 3U);
    std::array<std::uint8_t, 92> shifted{};
    for (std::size_t group = 0; group < 23; ++group) {
        std::array<std::uint8_t, 4> bytes{
            scene_data_[source + group * 3], scene_data_[source + group * 3 + 1],
            scene_data_[source + group * 3 + 2], 0};
        for (std::uint8_t count = 0; count < phase; ++count) {
            for (unsigned twice = 0; twice < 2; ++twice) {
                std::uint8_t carry = 0;
                for (auto& byte : bytes) {
                    const auto next = static_cast<std::uint8_t>(byte & 1U);
                    byte = static_cast<std::uint8_t>((byte >> 1U) | (carry << 7U));
                    carry = next;
                }
            }
        }
        std::copy(bytes.begin(), bytes.end(), shifted.begin() + group * 4);
    }

    const auto dx = static_cast<std::uint8_t>(sprites_.x[slot] - 0x57U);
    const auto dy = static_cast<std::uint8_t>(sprites_.y[slot] - 0x52U);
    const unsigned low_sum = static_cast<unsigned>(dy) + ((dx & 4U) != 0 ? 0x80U : 0U);
    auto high = static_cast<std::uint8_t>(sprites_.y[slot] >= 0x52 ? 0 : 0xFF);
    high = static_cast<std::uint8_t>(high + (dx >> 3U) + (low_sum > 0xFFU ? 1U : 0U));
    high = static_cast<std::uint8_t>(high + 0x5AU);
    std::uint16_t address = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint8_t>(low_sum));

    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 21; ++row) {
            const auto value = shifted[column + row * 4];
            const auto mask = expanded_multicolor_mask(value);
            const auto charset_index = static_cast<std::size_t>(address - 0x5800U);
            characters_.charset[charset_index] = static_cast<std::uint8_t>(
                (characters_.charset[charset_index] & static_cast<std::uint8_t>(~mask)) |
                (value & mask));
            ++address;
        }
        address = static_cast<std::uint16_t>(address + 107U);
    }
}

void EquipmentSelection::resolve_all_sprite_visuals()
{
    for (std::size_t sprite = 0; sprite < sprites_.pointers.size(); ++sprite) {
        resolve_sprite_visual(scene_data_, data_, sprites_, sprite);
    }
    sprites_.x_high_mask = 0;
    for (std::size_t sprite = 0; sprite < sprites_.x.size(); ++sprite) {
        if ((sprites_.x[sprite] & 0x80U) != 0) sprites_.x_high_mask |= (1U << sprite);
    }
}

std::vector<std::uint8_t> EquipmentSelection::take_pending_notice()
{
    auto notice = std::move(pending_notice_);
    pending_notice_.clear();
    return notice;
}

SoundEvents EquipmentSelection::take_sound_events()
{
    auto events = std::move(sound_events_);
    sound_events_.clear();
    return events;
}

} // namespace ghostbusters::game
