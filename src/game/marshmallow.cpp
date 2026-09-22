#include "game/marshmallow.hpp"

#include "game/bcd_money.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::array<std::uint8_t, 4> kArrivalPointers{0x2B, 0x2C, 0x2D, 0x2E};
constexpr std::array<std::uint8_t, 4> kAttackPointers5{0x2C, 0x2C, 0x31, 0x31};
constexpr std::array<std::uint8_t, 4> kAttackPointers7{0x2E, 0x32, 0x32, 0x2E};
constexpr std::array<std::uint8_t, 4> kAttackYOffset{0x00, 0x02, 0x04, 0x03};

[[nodiscard]] std::uint8_t decimal_add(std::uint8_t left, std::uint8_t right,
                                       bool& carry)
{
    unsigned value = static_cast<unsigned>(left) + right + (carry ? 1U : 0U);
    if (static_cast<unsigned>(left & 0x0FU) + (right & 0x0FU) +
            (carry ? 1U : 0U) > 9U) {
        value += 0x06U;
    }
    if (value > 0x99U) value += 0x60U;
    carry = value > 0xFFU;
    return static_cast<std::uint8_t>(value);
}

} // namespace

Marshmallow::Marshmallow(const assets::Payload& payload, CityControlsState state,
                         MarshmallowRegisters registers)
    : data_(payload), state_(std::move(state)), registers_(registers)
{
    if (state_.state3a < 0x24 || state_.state3a > 0x27) {
        throw std::invalid_argument("Marshmallow requires State $24-$27");
    }
    scene_data_.assign(data_.scene_graphics().begin(),
                       data_.scene_graphics().end());
}

void Marshmallow::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.sprites;
    sprites.pointers[sprite] = pointer;
    sprites.colors[sprite] = static_cast<std::uint8_t>(
        data_.sprite_color(pointer) & 0x0FU);
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("marshmallow sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void Marshmallow::move_roamer(std::size_t sprite)
{
    auto approach = [this](std::uint8_t& value, std::uint8_t target) {
        if (value == target) return;
        registers_.scratch23 = 1;
        if (value < target) ++value;
        else --value;
    };
    registers_.scratch24 = 2;
    approach(state_.sprites.x[sprite], state_.sprites.target_x[sprite]);
    approach(state_.sprites.y[sprite], state_.sprites.target_y[sprite]);
}

void Marshmallow::move_roamers()
{
    // $8925/$8A03 use coordinate offsets $0E,$0C,$0A,$08 only.
    for (std::size_t sprite = 8; sprite-- > 4;) move_roamer(sprite);
}

bool Marshmallow::roamer_at_target(std::size_t sprite) const noexcept
{
    return state_.sprites.x[sprite] == state_.sprites.target_x[sprite] &&
           state_.sprites.y[sprite] == state_.sprites.target_y[sprite];
}

bool Marshmallow::all_roamers_at_targets() const noexcept
{
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        if (!roamer_at_target(sprite)) return false;
    }
    return true;
}

void Marshmallow::queue_notice(std::uint8_t index)
{
    if (state_.notices.position4b() != 0) return;
    std::vector<std::uint8_t> translated;
    for (auto value : data_.notice(index)) {
        if (value == 0x20) value = 0;
        if (value >= 0x40) value = static_cast<std::uint8_t>(value - 0x40);
        translated.push_back(value);
    }
    (void)state_.notices.queue(translated);
}

void Marshmallow::update_trail_and_bait()
{
    // Exact State-18 continuation at $7F29. State $24 reaches it while the
    // marshmallow sprites are still approaching, so B can retarget them.
    const auto row = static_cast<std::uint8_t>(state_.sprites.y[0] - 0x40U) >> 3U;
    const auto column = static_cast<std::uint8_t>(state_.sprites.x[0] - 0x1DU) >> 2U;
    const auto cell = data_.trail_cell(row, column);
    const auto offset = cell.screen_offset;
    if (state_.characters.screen[offset] == 0) {
        state_.characters.screen[offset] = 0x40;
        trail_color_row_offset_ = offset - column;
        state_.characters.colors[offset] = 0;
    }

    const auto linear_low = cell.linear_low;
    if (linear_low != state_.last_map_cell1f) {
        state_.last_map_cell1f = linear_low;
        if (state_.route_length66 != 0xFF) ++state_.route_length66;
    }
    if (state_.key17 != 0x42) return;
    state_.key17 = 0;
    if (state_.bait69 == 0) {
        queue_notice(2);
        return;
    }
    // Every accepted B press consumes bait, even when one is already active;
    // only the first press retargets the roamers and paints the map marker.
    --state_.bait69;
    if (state_.bait_active68 != 0) return;

    state_.bait_active68 = 1;
    state_.characters.screen[offset] = 0x41;
    state_.characters.screen[offset + 1] = 0x42;
    state_.characters.colors[offset] = state_.characters.colors[offset + 1] = 3;
    const auto player_x = state_.sprites.x[0];
    const auto player_y = state_.sprites.y[0];
    state_.sprites.target_x[4] = state_.sprites.target_x[5] =
        static_cast<std::uint8_t>(player_x - 6U);
    state_.sprites.target_x[6] = state_.sprites.target_x[7] =
        static_cast<std::uint8_t>(player_x + 6U);
    state_.sprites.target_y[4] = state_.sprites.target_y[6] =
        static_cast<std::uint8_t>(player_y - 0x0AU);
    state_.sprites.target_y[5] = state_.sprites.target_y[7] =
        static_cast<std::uint8_t>(player_y + 0x0BU);
}

void Marshmallow::draw_city_map_quadrant(std::uint8_t building,
                                         std::uint8_t quadrant)
{
    if (building >= state_.map_types_ea28.size() || quadrant >= 4) {
        throw std::out_of_range("marshmallow city-map quadrant");
    }
    registers_.scratch23 = building;
    registers_.scratch24 = quadrant;
    const auto selector = state_.map_types_ea28[building];
    const auto map = data_.city_map_quadrant(building, selector, quadrant);
    registers_.scratch25 = 0xFF; // $95EB decrements its five-byte counter past zero.
    for (std::size_t column = 0; column < map.cells.size(); ++column) {
        const auto destination = map.screen_offset +
                                 static_cast<std::ptrdiff_t>(column);
        if (destination >= 0 &&
            static_cast<std::size_t>(destination) <
                state_.characters.screen.size()) {
            state_.characters.screen[static_cast<std::size_t>(destination)] =
                map.cells[column];
        }
    }
}

void Marshmallow::restore_city_roamers()
{
    // $983D restores the positions saved before the alert as immediate
    // targets, and the normal city sprite pointers from $C4-$C7.
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        state_.sprites.target_x[sprite] = state_.shadow_x_ea46[sprite];
        state_.sprites.target_y[sprite] = state_.shadow_y_ea47[sprite];
        set_pointer(sprite, state_.sprite_control_c0[sprite]);
    }
}

void Marshmallow::add_reward()
{
    // $9664 adds $002000 from low byte $59 upward. On final carry it clamps
    // only $57/$58 to $99 and retains the already computed low byte $59.
    bool carry = false;
    state_.balance57.byte59 = decimal_add(state_.balance57.byte59, 0, carry);
    state_.balance57.byte58 = decimal_add(state_.balance57.byte58, 0x20, carry);
    state_.balance57.byte57 = decimal_add(state_.balance57.byte57, 0, carry);
    if (carry) {
        state_.balance57.byte57 = 0x99;
        state_.balance57.byte58 = 0x99;
    }
}

void Marshmallow::tick()
{
    trail_color_row_offset_.reset();
    auto& sprites = state_.sprites;
    switch (state_.state3a) {
    case 0x24: {
        move_roamers();
        registers_.scratch23 = 4;
        for (std::size_t sprite = 8; sprite-- > 4;) {
            if (!roamer_at_target(sprite)) continue;
            set_pointer(sprite, kArrivalPointers[sprite - 4]);
            --registers_.scratch23;
        }
        if (registers_.scratch23 != 0) {
            update_trail_and_bait();
            return;
        }
        state_.countdown7c = 0xFF;
        if (state_.bait_active68 == 0) {
            state_.key17 = 0;
            state_.state3a = 0x25;
        } else {
            state_.bait_active68 = 0;
            state_.state3a = 0x26;
        }
        return;
    }

    case 0x25: {
        if (state_.countdown7c == 0) {
            queue_notice(5);
            registers_.scratch23 = 0;
            registers_.scratch24 = 0x40;
            registers_.scratch25 = 0;
            state_.balance57 = subtractMoney(state_.balance57, {0, 0x40, 0});
            restore_city_roamers();
            state_.state3a = 0x27;
            return;
        }
        const auto phase = static_cast<std::uint8_t>(state_.countdown7c >> 3U);
        registers_.scratch23 = phase;
        const auto animation = static_cast<std::size_t>(phase & 3U);
        set_pointer(5, kAttackPointers5[animation]);
        set_pointer(7, kAttackPointers7[animation]);
        registers_.scratch23 = static_cast<std::uint8_t>(phase ^ 0x1FU);
        auto y = static_cast<std::uint8_t>(
            sprites.target_y[4] + registers_.scratch23 - kAttackYOffset[animation]);
        sprites.y[4] = sprites.y[6] = y;
        y = static_cast<std::uint8_t>(y + 0x15U);
        sprites.y[5] = sprites.y[7] = y;
        if ((state_.countdown7c & 0x3FU) == 0x20U) {
            const auto quadrant = static_cast<std::uint8_t>(
                (state_.countdown7c >> 6U) ^ 3U);
            const auto building = state_.pending_alert80;
            if (building >= state_.map_types_ea28.size()) {
                throw std::out_of_range("marshmallow alert building");
            }
            state_.map_types_ea28[building] = 0;
            draw_city_map_quadrant(building, quadrant);
        }
        return;
    }

    case 0x26:
        set_pointer(4, 0x2F);
        set_pointer(6, 0x30);
        if (state_.countdown7c != 0) return;
        queue_notice(6);
        registers_.scratch23 = 0;
        registers_.scratch24 = 0x20;
        registers_.scratch25 = 0;
        add_reward();
        restore_city_roamers();
        state_.key17 = 0;
        state_.state3a = 0x27;
        return;

    case 0x27:
        move_roamers();
        if (!all_roamers_at_targets()) return;
        for (std::size_t sprite = 4; sprite < 8; ++sprite) {
            state_.sprites.target_x[sprite] = state_.shadow_target_x_ea56[sprite];
            state_.sprites.target_y[sprite] = state_.shadow_target_y_ea57[sprite];
        }
        state_.state3a = 0x12;
        return;
    }
}

} // namespace ghostbusters::game
