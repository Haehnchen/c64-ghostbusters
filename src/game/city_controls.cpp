#include "game/city_controls.hpp"

#include "assets/city_data.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::array<std::uint8_t, 5> kVerticalLow{0x1B, 0x37, 0x53, 0x6F, 0x8B};
constexpr std::array<std::uint8_t, 5> kVerticalHigh{0x1F, 0x3B, 0x57, 0x73, 0x8F};
constexpr std::array<std::uint8_t, 4> kHorizontalLow{0x40, 0x68, 0x90, 0xB8};
constexpr std::array<std::uint8_t, 4> kHorizontalHigh{0x44, 0x6C, 0x94, 0xBC};
constexpr std::array<std::uint8_t, 4> kBuildingXLow{0x1B, 0x37, 0x53, 0x6F};
constexpr std::array<std::uint8_t, 4> kBuildingXHigh{0x3B, 0x57, 0x73, 0x8F};
constexpr std::array<std::uint8_t, 5> kBuildingYLow{0x00, 0x40, 0x68, 0x90, 0xB8};
constexpr std::array<std::uint8_t, 5> kBuildingYHigh{0x44, 0x6C, 0x94, 0xBC, 0xFF};
constexpr std::array<std::uint8_t, 5> kRoadX{0x1D, 0x39, 0x55, 0x71, 0x8D};
constexpr std::array<std::uint8_t, 4> kRoadY{0x42, 0x6A, 0x92, 0xBA};
constexpr std::array<std::uint8_t, 4> kRequiredEquipment{0, 1, 4, 1};

bool in_wrapping_half_open(std::uint8_t value, std::uint8_t low, std::uint8_t high)
{
    if (low <= high) return value >= low && value < high;
    return value >= low || value < high;
}

int compare_balance(AccountBalanceBytes left, AccountBalanceBytes right)
{
    const std::array<std::uint8_t, 3> a{left.byte57, left.byte58, left.byte59};
    const std::array<std::uint8_t, 3> b{right.byte57, right.byte58, right.byte59};
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

std::uint8_t bcd_add(std::uint8_t left, std::uint8_t right, bool& carry)
{
    unsigned low = (left & 0x0F) + (right & 0x0F) + (carry ? 1U : 0U);
    if (low > 9) low += 6;
    unsigned value = (left & 0xF0) + (right & 0xF0) + low;
    if (value > 0x99) value += 0x60;
    carry = value > 0xFF;
    return static_cast<std::uint8_t>(value);
}

} // namespace

CityControls::CityControls(const assets::Payload& payload, const CityEntry& entry,
                           AccountBalanceBytes starting_balance)
    : payload_(payload)
{
    if (entry.stage() != CityEntry::Stage::ready) {
        throw std::invalid_argument("CityControls requires a ready CityEntry");
    }
    state_.characters = entry.characters();
    state_.sprites = entry.sprites();
    state_.starting_balance51 = starting_balance;
    state_.balance57 = entry.balance();
    state_.bait69 = entry.bait();
    state_.empty_traps6b = entry.traps6b();
    state_.owned_mask6d = entry.owned_mask();
    state_.fire_latch11 = entry.fire_latch11();
    state_.sprite_control_c0 = state_.sprites.pointers;
    state_.shadow_x_ea46 = state_.sprites.x;
    state_.shadow_y_ea47 = state_.sprites.y;
    state_.shadow_target_x_ea56 = state_.sprites.target_x;
    state_.shadow_target_y_ea57 = state_.sprites.target_y;
    // initialize_game_state $9041-$904B seeds three hauntings once, before
    // the initial dialog/shop. City re-entry uses the state-taking constructor
    // below and must preserve the evolved/captured statuses instead.
    state_.building_status_c8[7] = 0x1F;
    state_.building_status_c8[8] = 0x1C;
    state_.building_status_c8[15] = 0x14;
    const assets::CityData city_data(payload_);
    for (std::size_t i = 0; i < state_.map_types_ea28.size(); ++i)
        state_.map_types_ea28[i] = city_data.map_type(i);
    if (!entry.pending_notice().empty()) (void)state_.notices.queue(entry.pending_notice());

    scene_data_.assign(city_data.scene_graphics().begin(),
                       city_data.scene_graphics().end());
}

CityControls::CityControls(const assets::Payload& payload, CityControlsState state)
    : payload_(payload), state_(std::move(state))
{
    const assets::CityData city_data(payload_);
    scene_data_.assign(city_data.scene_graphics().begin(),
                       city_data.scene_graphics().end());
}

void CityControls::refresh_sprite_visual(std::size_t sprite)
{
    const auto pointer = state_.sprites.pointers[sprite];
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) throw std::out_of_range("city sprite pointer");
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                state_.sprites.bitmap_data[sprite].begin());
    const assets::CityData city_data(payload_);
    state_.sprites.colors[sprite] = city_data.sprite_color(pointer);
}

void CityControls::queue_notice(std::uint8_t index)
{
    const assets::CityData city_data(payload_);
    std::vector<std::uint8_t> translated;
    for (const auto raw_value : city_data.notice(index)) {
        auto value = raw_value;
        if (value == 0x20) value = 0;
        if (value >= 0x40) value = static_cast<std::uint8_t>(value - 0x40);
        translated.push_back(value);
    }
    (void)state_.notices.queue(translated);
}

void CityControls::add_pk_energy(std::uint8_t amount, std::uint8_t random)
{
    const auto old_thousands = static_cast<std::uint8_t>(state_.pk_high5b & 0xF0);
    bool carry = false;
    state_.pk_high5b = bcd_add(state_.pk_high5b, amount, carry);
    if (carry) {
        state_.pk_low5a = 0x99;
        state_.pk_high5b = 0x99;
        return;
    }
    const auto new_thousands = static_cast<std::uint8_t>(state_.pk_high5b & 0xF0);
    if (new_thousands == old_thousands || new_thousands < 0x50) return;

    std::uint8_t candidate = random;
    for (unsigned attempts = 0; attempts < 256; ++attempts, ++candidate) {
        auto index = static_cast<std::uint8_t>(candidate & 0x0F);
        if (index < 4) index = static_cast<std::uint8_t>(index | 4);
        if (index != 0x0A && state_.map_types_ea28[index] != 0) {
            state_.pending_alert80 = index;
            state_.building_status_c8[index] = 0xC8;
            return;
        }
    }
}

void CityControls::reset_arrived_sprite(std::size_t sprite)
{
    state_.active_roamer_count28 = static_cast<std::uint8_t>(std::count_if(
        state_.sprite_control_c0.begin() + 4, state_.sprite_control_c0.end(),
        [](std::uint8_t value) { return value != 0; }));
    const assets::CityData city_data(payload_);
    const auto position = city_data.arriving_sprite(sprite);
    state_.sprites.x[sprite] = state_.shadow_x_ea46[sprite] = position.x;
    state_.sprites.y[sprite] = state_.shadow_y_ea47[sprite] = position.y;
    state_.sprites.target_x[sprite] = state_.shadow_target_x_ea56[sprite] = position.target_x;
    state_.sprites.target_y[sprite] = state_.shadow_target_y_ea57[sprite] = position.target_y;
}

void CityControls::move_sprite(std::size_t sprite)
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        if (value < target) ++value;
        else if (value > target) --value;
    };
    approach(state_.sprites.x[sprite], state_.sprites.target_x[sprite]);
    approach(state_.sprites.y[sprite], state_.sprites.target_y[sprite]);
}

void CityControls::move_player(std::uint8_t joystick)
{
    for (int road = 4; road >= 0; --road) {
        const auto x = state_.sprites.x[0];
        if (x < kVerticalLow[road] || x > kVerticalHigh[road]) continue;
        if ((joystick & 1) == 0 && state_.sprites.y[0] >= 0x43) {
            --state_.sprites.y[0];
            state_.sprites.x[0] = static_cast<std::uint8_t>(kVerticalLow[road] + 2);
        } else if ((joystick & 2) == 0 && state_.sprites.y[0] < 0xBA) {
            ++state_.sprites.y[0];
            state_.sprites.x[0] = static_cast<std::uint8_t>(kVerticalLow[road] + 2);
        }
    }
    for (int road = 3; road >= 0; --road) {
        const auto y = state_.sprites.y[0];
        if (y < kHorizontalLow[road] || y > kHorizontalHigh[road]) continue;
        if ((joystick & 4) == 0 && state_.sprites.x[0] >= 0x1E) {
            --state_.sprites.x[0];
            state_.sprites.y[0] = static_cast<std::uint8_t>(kHorizontalLow[road] + 2);
        } else if ((joystick & 8) == 0 && state_.sprites.x[0] < 0x8D) {
            ++state_.sprites.x[0];
            state_.sprites.y[0] = static_cast<std::uint8_t>(kHorizontalLow[road] + 2);
        }
    }
}

void CityControls::update_buildings()
{
    for (int index = 19; index >= 0; --index) {
        const auto old = state_.building_status_c8[index];
        auto high = static_cast<std::uint8_t>(old & 0xF0);
        const auto column = static_cast<std::size_t>(index & 3);
        const auto row = static_cast<std::size_t>((index & 0x1C) >> 2);
        const bool inside = state_.sprites.x[0] >= kBuildingXLow[column] &&
                            state_.sprites.x[0] < kBuildingXHigh[column] &&
                            state_.sprites.y[0] >= kBuildingYLow[row] &&
                            state_.sprites.y[0] < kBuildingYHigh[row];
        std::uint8_t low;
        if (inside) {
            const auto base = static_cast<std::uint8_t>(old & 0x0C);
            auto phase = static_cast<std::uint8_t>(3);
            if ((old & 0x0F) != 0x0F) {
                phase = static_cast<std::uint8_t>((old & 0x0F) >> 2U);
                const auto requirement = kRequiredEquipment[phase];
                if (requirement != 0 && (requirement & state_.owned_mask6d) == 0) phase = 0;
            }
            low = static_cast<std::uint8_t>(base | phase);
            state_.current_building6e = static_cast<std::uint8_t>(index);
            if (low == 0x0F) {
                if ((old & 0x0F) == 0x0C) high = 0x10;
                low = 0x0F;
            }
        } else {
            low = static_cast<std::uint8_t>(old & 0x0F);
            if (low < 0x0C) low = static_cast<std::uint8_t>(low & 0x0C);
        }
        state_.building_status_c8[index] = static_cast<std::uint8_t>(high | low);
    }
}

void CityControls::detect_roamer_contacts()
{
    const auto px = state_.sprites.x[0];
    const auto py = state_.sprites.y[0];
    for (int sprite = 7; sprite >= 4; --sprite) {
        if (!in_wrapping_half_open(state_.sprites.x[sprite], static_cast<std::uint8_t>(px - 8),
                                   static_cast<std::uint8_t>(px + 9)) ||
            !in_wrapping_half_open(state_.sprites.y[sprite], static_cast<std::uint8_t>(py - 0x11),
                                   static_cast<std::uint8_t>(py + 0x0D))) continue;
        state_.sprites.target_x[sprite] = state_.sprites.x[sprite];
        state_.sprites.target_y[sprite] = state_.sprites.y[sprite];
        auto& counter = state_.roamer_counters6f[sprite - 4];
        if (counter == 0) counter = state_.route_length66 != 0 ? state_.route_length66 : 1;
    }
}

void CityControls::update_zuul_routes(std::uint8_t random)
{
    const assets::CityData city_data(payload_);
    const std::array<std::uint8_t, 2> choices{random, static_cast<std::uint8_t>(random >> 2U)};
    for (int sprite = 3; sprite >= 2; --sprite) {
        const auto xi = std::find(kRoadX.begin(), kRoadX.end(), state_.sprites.x[sprite]);
        const auto yi = std::find(kRoadY.begin(), kRoadY.end(), state_.sprites.y[sprite]);
        if (xi == kRoadX.end() || yi == kRoadY.end()) continue;
        const auto xposition = static_cast<std::uint8_t>(xi - kRoadX.begin() + 1);
        const auto yposition = static_cast<std::uint8_t>(yi - kRoadY.begin() + 1);
        const auto selector = static_cast<std::uint8_t>(choices[sprite - 2] & 6);
        const auto xlookup = static_cast<std::uint8_t>(
            xposition + city_data.zuul_route_x_offset(selector));
        const auto ylookup = static_cast<std::uint8_t>(
            yposition + city_data.zuul_route_y_offset(selector));
        state_.sprites.target_x[sprite] = state_.shadow_target_x_ea56[sprite] =
            city_data.zuul_target_x(xlookup);
        state_.sprites.target_y[sprite] = state_.shadow_target_y_ea57[sprite] =
            city_data.zuul_target_y(ylookup);
    }
}

bool CityControls::handle_fire(std::uint8_t joystick)
{
    const auto current = static_cast<std::uint8_t>(joystick & 0x10);
    const bool pressed = current != state_.fire_latch11 && current == 0;
    state_.fire_latch11 = current;
    if (!pressed) return false;
    if ((joystick & 2) == 0) state_.current_building6e = static_cast<std::uint8_t>(state_.current_building6e + 4);
    const auto building = state_.current_building6e;
    if (building != 0x11) {
        if (building == 0x0A && state_.finale_active81 == 0) return false;
        if (state_.empty_traps6b == 0) { queue_notice(0); return true; }
        if (state_.backup_men3d < 2) { queue_notice(1); return true; }
        if (state_.backpack_charge3e == 0) { queue_notice(3); return true; }
        if (building >= state_.map_types_ea28.size() || state_.map_types_ea28[building] == 0) return false;
    }
    state_.state3a = state_.route_length66 == 0 ? 0x16 : 0x13;
    return true;
}

bool CityControls::handle_alert()
{
    const auto alert = state_.pending_alert80;
    if (alert == 0 || alert >= state_.building_status_c8.size() ||
        (state_.building_status_c8[alert] & 0xFC) != 0xF8) return false;
    state_.building_status_c8[alert] = 0;
    const auto relative = static_cast<std::uint8_t>(alert - 4);
    auto x = static_cast<std::uint8_t>(kRoadX[relative & 3] + 8);
    state_.sprites.target_x[4] = state_.sprites.target_x[5] = x;
    x = static_cast<std::uint8_t>(x + 0x0C);
    state_.sprites.target_x[6] = state_.sprites.target_x[7] = x;
    auto y = static_cast<std::uint8_t>(kRoadY[relative >> 2U] - 0x18);
    state_.sprites.target_y[4] = state_.sprites.target_y[6] = y;
    y = static_cast<std::uint8_t>(y + 0x15);
    state_.sprites.target_y[5] = state_.sprites.target_y[7] = y;
    state_.bait_active68 = 0;
    queue_notice(4);
    state_.state3a = 0x24;
    return true;
}

void CityControls::update_trail_and_bait()
{
    const auto row = static_cast<std::uint8_t>(state_.sprites.y[0] - 0x40) >> 3U;
    const auto column = static_cast<std::uint8_t>(state_.sprites.x[0] - 0x1D) >> 2U;
    const assets::CityData city_data(payload_);
    const auto trail = city_data.trail_cell(row, column);
    const auto offset = trail.screen_offset;
    if (offset >= state_.characters.screen.size()) throw std::out_of_range("city trail address");
    if (state_.characters.screen[offset] == 0) {
        state_.characters.screen[offset] = 0x40;
        state_.characters.colors[offset] = 0;
    }
    const auto linear_low = trail.linear_low;
    if (linear_low != state_.last_map_cell1f) {
        state_.last_map_cell1f = linear_low;
        if (state_.route_length66 != 0xFF) ++state_.route_length66;
    }
    if (state_.key17 != 0x42) return;
    state_.key17 = 0;
    if (state_.bait69 == 0) { queue_notice(2); return; }
    --state_.bait69;
    if (state_.bait_active68 != 0) return;
    state_.bait_active68 = 1;
    state_.characters.screen[offset] = 0x41;
    state_.characters.screen[offset + 1] = 0x42;
    state_.characters.colors[offset] = state_.characters.colors[offset + 1] = 3;
    const auto px = state_.sprites.x[0];
    const auto py = state_.sprites.y[0];
    state_.sprites.target_x[4] = state_.sprites.target_x[5] = static_cast<std::uint8_t>(px - 6);
    state_.sprites.target_x[6] = state_.sprites.target_x[7] = static_cast<std::uint8_t>(px + 6);
    state_.sprites.target_y[4] = state_.sprites.target_y[6] = static_cast<std::uint8_t>(py - 10);
    state_.sprites.target_y[5] = state_.sprites.target_y[7] = static_cast<std::uint8_t>(py + 11);
}

void CityControls::tick(CityControlsInput input)
{
    if (state_.state3a != 0x12) return;
    state_.key17 = input.key17;

    for (int sprite = 7; sprite >= 4; --sprite) {
        state_.shadow_x_ea46[sprite] = state_.sprites.x[sprite];
        state_.shadow_y_ea47[sprite] = state_.sprites.y[sprite];
        if (state_.sprites.x[sprite] == state_.shadow_target_x_ea56[sprite] &&
            state_.sprites.y[sprite] == state_.shadow_target_y_ea57[sprite]) {
            add_pk_energy(1, input.random06);
            reset_arrived_sprite(sprite);
        }
    }
    for (int sprite = 7; sprite >= 4; --sprite) {
        if (state_.sprites.x[sprite] == state_.sprites.target_x[sprite] ||
            state_.sprite_control_c0[sprite] == 0) continue;
        const auto pointer = static_cast<std::uint8_t>(state_.sprites.x[sprite] < state_.sprites.target_x[sprite] ? 8 : 9);
        state_.sprites.pointers[sprite] = state_.sprite_control_c0[sprite] = pointer;
        refresh_sprite_visual(sprite);
    }

    const assets::CityData city_data(payload_);
    if (state_.pk_high5b >= 0x50) {
        for (int sprite = 3; sprite >= 2; --sprite) {
            state_.shadow_x_ea46[sprite] = state_.sprites.x[sprite];
            state_.shadow_y_ea47[sprite] = state_.sprites.y[sprite];
            const auto checkpoint = city_data.zuul_sprite(sprite);
            if (state_.pk_high5b >= 0x95 ||
                (state_.sprites.x[sprite] == checkpoint.check_x &&
                 state_.sprites.y[sprite] == checkpoint.check_y)) {
                state_.sprites.target_x[sprite] = state_.shadow_target_x_ea56[sprite] =
                    checkpoint.target_x;
                state_.sprites.target_y[sprite] = state_.shadow_target_y_ea57[sprite] =
                    checkpoint.target_y;
            }
        }
    }

    constexpr std::array<std::uint8_t, 4> rendezvous{0x61, 0x86, 0x65, 0x87};
    bool at_rendezvous = true;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto sprite = 2U + i / 2U;
        const auto value = i % 2U == 0 ? state_.sprites.x[sprite] : state_.sprites.y[sprite];
        if (value != rendezvous[i]) { at_rendezvous = false; break; }
        if (i % 2U == 0) state_.shadow_x_ea46[sprite] = value;
        else state_.shadow_y_ea47[sprite] = value;
    }
    if (at_rendezvous) {
        if (state_.finale_active81 == 0) {
            if (compare_balance(state_.balance57, state_.starting_balance51) < 0) {
                state_.state3a = 0x2D;
                state_.finale_active81 = 1;
                return;
            }
            state_.finale_active81 = 1;
            state_.countdown7c = 0xFF;
            queue_notice(9);
            state_.sprites.target_x[0] = state_.sprites.target_x[1] = 0x63;
            state_.sprites.target_y[0] = state_.sprites.target_y[1] = 0x92;
            state_.move_mask3c = 0;
            state_.building_status_c8.fill(0);
        } else {
            // A is still $81 at the branch into $7C7A.
            state_.building_status_c8.fill(state_.finale_active81);
        }
        state_.status_d2 = 0x0F;
        // $D2 is also byte 10 of the contiguous $C8-$DB status array.
        state_.building_status_c8[0x0A] = 0x0F;
    }

    if ((input.frame09 & state_.move_mask3c) == 0) {
        for (int sprite = 7; sprite >= 2; --sprite) {
            if (state_.sprite_control_c0[sprite] != 0) move_sprite(sprite);
        }
        if (state_.finale_active81 != 0) {
            for (int sprite = 1; sprite >= 0; --sprite) {
                if (state_.sprite_control_c0[sprite] != 0) move_sprite(sprite);
            }
        }
    }
    if (state_.finale_active81 == 0) move_player(input.joystick33);

    state_.sprites.x[1] = state_.sprites.x[0];
    state_.sprites.y[1] = state_.sprites.y[0];
    state_.shadow_x_ea46[0] = state_.shadow_target_x_ea56[0] = state_.sprites.x[0];
    state_.shadow_y_ea47[0] = state_.shadow_target_y_ea57[0] = state_.sprites.y[0];
    update_buildings();
    detect_roamer_contacts();
    update_zuul_routes(input.random06);

    if (state_.finale_active81 != 0) {
        if (state_.sprites.x[0] == 0x63 && state_.sprites.y[0] == 0x92 && state_.countdown7c == 0) {
            state_.current_building6e = 0x0A;
            state_.state3a = 0x16;
        }
        return;
    }
    if (handle_fire(input.joystick33)) return;
    if (handle_alert()) return;
    update_trail_and_bait();
}

} // namespace ghostbusters::game
