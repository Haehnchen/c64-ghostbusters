#include "game/catch_sequence.hpp"

#include "assets/sprite_mirror.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

std::uint8_t bcd_add(std::uint8_t left, std::uint8_t right, bool& carry)
{
    unsigned low = (left & 0x0FU) + (right & 0x0FU) + (carry ? 1U : 0U);
    if (low > 9U) low += 6U;
    unsigned value = (left & 0xF0U) + (right & 0xF0U) + low;
    if (value > 0x99U) value += 0x60U;
    carry = value > 0xFFU;
    return static_cast<std::uint8_t>(value);
}

} // namespace

CatchSequence::CatchSequence(const assets::Payload& payload, DriveControlsState state,
                             BuildingControlsRegisters building_registers,
                             BeamControlsRegisters beam_registers,
                             CatchSequenceRegisters registers)
    : capture_data_(payload), state_(std::move(state)),
      building_registers_(building_registers), beam_registers_(beam_registers),
      registers_(registers)
{
    if (state_.city.state3a < 0x1C || state_.city.state3a > 0x1F) {
        throw std::invalid_argument("CatchSequence requires State $1C-$1F");
    }
    const auto decoded = capture_data_.scene_graphics();
    scene_data_.assign(0x1100, 0);
    std::copy(decoded.begin(), decoded.end(), scene_data_.begin());
    assets::mirror_sprite_sheets(scene_data_);
}

void CatchSequence::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    sprites.colors[sprite] = static_cast<std::uint8_t>(
        capture_data_.sprite_color(pointer) & 0x0FU);
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("catch-sequence sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void CatchSequence::move_all_sprites()
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        if (value < target) ++value;
        else if (value > target) --value;
    };
    auto& sprites = state_.city.sprites;
    for (std::size_t sprite = 8; sprite-- > 0;) {
        approach(sprites.x[sprite], sprites.target_x[sprite]);
        approach(sprites.y[sprite], sprites.target_y[sprite]);
    }
}

void CatchSequence::update_ghost(CatchSequenceInput input)
{
    auto& sprites = state_.city.sprites;
    sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask & 0xEFU);
    if ((state_.city.owned_mask6d & 0x02U) == 0) {
        sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask | 0x10U);
    }
    const auto direction = building_registers_.direction7d;
    const auto delta = capture_data_.ghost_delta(direction);
    sprites.x[4] = static_cast<std::uint8_t>(sprites.x[4] +
        delta[0]);
    sprites.y[4] = static_cast<std::uint8_t>(sprites.y[4] +
        delta[1]);
    std::uint8_t turn = 0;
    if (sprites.y[4] >= 0x70U) { sprites.y[4] = 0x6E; turn = 4; }
    if (sprites.y[4] < 0x30U) { sprites.y[4] = 0x32; turn = 4; }
    sprites.target_y[4] = sprites.y[4];
    if (sprites.x[4] >= 0x90U) { sprites.x[4] = 0x8E; turn = 4; }
    if (sprites.x[4] < 0x48U) { sprites.x[4] = 0x4A; turn = 4; }
    sprites.target_x[4] = sprites.x[4];
    building_registers_.direction7d = static_cast<std::uint8_t>(
        (building_registers_.direction7d + turn) & 7U);
    if ((input.frame09 & 3U) == 0) {
        const auto bit = static_cast<std::uint8_t>(input.random06 & 1U);
        building_registers_.direction7d = static_cast<std::uint8_t>(
            (building_registers_.direction7d + bit) & 7U);
        set_pointer(4, static_cast<std::uint8_t>(0x0AU + bit));
    }
}

void CatchSequence::update_deformation()
{
    auto& sprites = state_.city.sprites;
    beam_registers_.beam_phase79 = static_cast<std::uint8_t>(
        beam_registers_.beam_phase79 + 1U);
    if (beam_registers_.beam_phase79 >= 6U) beam_registers_.beam_phase79 = 0;
    constexpr std::array<std::uint8_t, 4> bases{0x25, 0x1F, 0x1F, 0x1F};
    for (std::size_t sprite = 0; sprite < 4; ++sprite) {
        set_pointer(sprite, static_cast<std::uint8_t>(
            bases[sprite] + beam_registers_.beam_phase79));
    }

    auto amount = beam_registers_.beam_scratch7a;
    if (amount >= 0x49U) amount = static_cast<std::uint8_t>(0x9CU - amount);
    const auto top = static_cast<std::uint8_t>(sprites.y[7] - 0x39U);
    registers_.scratch23 = top;
    const auto lower = static_cast<std::uint8_t>(sprites.y[7] - 0x15U);
    const auto compressed = static_cast<std::uint8_t>(lower - amount);
    sprites.y[3] = lower;
    sprites.y[1] = compressed;
    sprites.y[2] = compressed >= top ? compressed : top;
    sprites.y[0] = static_cast<std::uint8_t>(compressed - 0x14U);
    for (std::size_t sprite = 0; sprite < 4; ++sprite) sprites.x[sprite] = sprites.x[7];
}

void CatchSequence::update_pointer4(std::uint8_t frame)
{
    const auto pointer = state_.city.sprites.pointers[4];
    // $9A0C first shifts the pointer right, then rotates it left after three
    // frame shifts. The two pointer shifts cancel except for the new low bit.
    set_pointer(4, static_cast<std::uint8_t>(
        (pointer & 0xFEU) | ((frame >> 2U) & 1U)));
}

void CatchSequence::add_pk_energy(std::uint8_t amount, std::uint8_t random)
{
    auto& city = state_.city;
    const auto old_thousands = static_cast<std::uint8_t>(city.pk_high5b & 0xF0U);
    bool carry = false;
    city.pk_high5b = bcd_add(city.pk_high5b, amount, carry);
    if (carry) {
        city.pk_low5a = 0x99;
        city.pk_high5b = 0x99;
        return;
    }
    const auto new_thousands = static_cast<std::uint8_t>(city.pk_high5b & 0xF0U);
    if (new_thousands == old_thousands || new_thousands < 0x50U) return;
    std::uint8_t candidate = random;
    for (unsigned attempts = 0; attempts < 256; ++attempts, ++candidate) {
        auto index = static_cast<std::uint8_t>(candidate & 0x0FU);
        if (index < 4U) index = static_cast<std::uint8_t>(index | 4U);
        if (index != 0x0AU && city.map_types_ea28[index] != 0) {
            city.pending_alert80 = index;
            city.building_status_c8[index] = 0xC8;
            return;
        }
    }
}

void CatchSequence::add_capture_award(std::uint8_t amount)
{
    auto& balance = state_.city.balance57;
    bool carry = false;
    balance.byte59 = bcd_add(balance.byte59, 0, carry);
    balance.byte58 = bcd_add(balance.byte58, amount, carry);
    balance.byte57 = bcd_add(balance.byte57, 0, carry);
    if (carry) {
        balance.byte58 = 0x99;
        balance.byte57 = 0x99;
    }
}

void CatchSequence::advance_state()
{
    state_.city.key17 = 0;
    state_.city.state3a = static_cast<std::uint8_t>(state_.city.state3a + 1U);
}

CatchSequenceTickResult CatchSequence::request_speech(std::uint8_t command)
{
    blocked_speech_ = command;
    return {{{CatchAudioAction::speech_start, command}}, true};
}

void CatchSequence::finish_return_targets()
{
    auto& sprites = state_.city.sprites;
    sprites.target_x[5] = sprites.x[7];
    sprites.target_x[6] = static_cast<std::uint8_t>(sprites.x[7] + 0x12U);
    sprites.target_y[5] = sprites.y[7];
    sprites.target_y[6] = sprites.y[7];
    advance_state();
}

void CatchSequence::tick_state28(CatchSequenceInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    update_ghost(input);
    move_all_sprites();
    sprites.y_expand_mask = 0x0E;
    sprites.priority_mask = 0;
    update_deformation();
    if (beam_registers_.beam_scratch7a >= 0x9CU) {
        for (std::size_t sprite = 0; sprite < 4; ++sprite) set_pointer(sprite, 0);
        const auto source = static_cast<std::size_t>(5U + ((input.random06 & 2U) >> 1U));
        sprites.target_x[4] = sprites.x[source];
        sprites.target_y[4] = sprites.y[source];
        beam_registers_.post_failure_ea7a = 1;
        advance_state();
        return;
    }
    beam_registers_.beam_scratch7a = static_cast<std::uint8_t>(
        beam_registers_.beam_scratch7a + 3U);
    const bool y_inside = sprites.y[4] >= static_cast<std::uint8_t>(sprites.y[0] - 0x0CU) &&
                          sprites.y[4] < static_cast<std::uint8_t>(sprites.y[0] + 0x04U);
    registers_.scratch23 = static_cast<std::uint8_t>(sprites.y[0] - 0x0CU);
    if (!y_inside) return;
    const bool x_inside = sprites.x[4] >= static_cast<std::uint8_t>(sprites.x[0] - 0x08U) &&
                          sprites.x[4] < static_cast<std::uint8_t>(sprites.x[0] + 0x08U);
    registers_.scratch23 = static_cast<std::uint8_t>(sprites.x[0] - 0x08U);
    if (x_inside) city.state3a = 0x1E;
}

CatchSequenceTickResult CatchSequence::tick_state29(CatchSequenceInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    move_all_sprites();
    for (int buster = 6; buster >= 5; --buster) {
        if (sprites.x[4] != sprites.target_x[buster] ||
            sprites.y[4] != sprites.target_y[buster]) continue;
        set_pointer(static_cast<std::size_t>(buster), 0x18);
        --city.backup_men3d;
        sprites.target_x[4] = buster == 6 ? 0xA8 : 0x00;
        sprites.target_y[4] = 0;
        // $8727 forces X=0; the shared $8729 tail then exits the loop.
        break;
    }
    if (sprites.x[4] != 0xA8 && sprites.x[4] != 0x00) return {};
    city.building_status_c8[city.current_building6e] = 0;
    add_pk_energy(3, input.random06);
    // A normal escape marks this flag and requests failure speech. Crossed
    // streams clear it earlier, so the same cleanup returns silently.
    if (beam_registers_.post_failure_ea7a != 0) return request_speech(2);
    city.state3a = 0x11;
    return {};
}

void CatchSequence::tick_state30(CatchSequenceInput input)
{
    auto& sprites = state_.city.sprites;
    update_deformation();
    if (beam_registers_.beam_scratch7a >= 0x9CU) {
        registers_.deformation7b = 0x7F;
        for (std::size_t sprite = 1; sprite < 4; ++sprite) set_pointer(sprite, 0);
        sprites.target_x[0] = sprites.x[7];
        sprites.target_y[0] = sprites.y[7];
        sprites.target_x[4] = sprites.x[7];
        sprites.target_y[4] = sprites.y[7];
        registers_.catch_flag_ea88 = 0;
        advance_state();
        return;
    }
    ++beam_registers_.beam_scratch7a;
    update_pointer4(input.frame09);
    sprites.x[4] = sprites.x[0];
    sprites.y[4] = sprites.y[0];
}

CatchSequenceTickResult CatchSequence::tick_state31(CatchSequenceInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    move_all_sprites();
    beam_registers_.beam_phase79 = static_cast<std::uint8_t>(
        beam_registers_.beam_phase79 + 1U);
    if (beam_registers_.beam_phase79 >= 6U) beam_registers_.beam_phase79 = 0;
    if (sprites.pointers[0] != 0) {
        set_pointer(0, static_cast<std::uint8_t>(0x25U + beam_registers_.beam_phase79));
    }
    if (sprites.x[0] == sprites.target_x[0] && sprites.y[0] == sprites.target_y[0] &&
        sprites.pointers[0] != 0) {
        set_pointer(7, 0x38);
        set_pointer(0, 0);
        set_pointer(4, 0);
        registers_.catch_flag_ea88 = 1;
        if ((city.owned_mask6d & 0x40U) != 0 && registers_.full_traps6c < 0x0AU) {
            ++registers_.full_traps6c;
        } else {
            --city.empty_traps6b;
        }
        const auto status = city.building_status_c8[city.current_building6e];
        std::uint8_t award_index = 0x0F;
        if ((status & 0x0CU) == 0x0CU) {
            award_index = static_cast<std::uint8_t>(status >> 4U);
            city.building_status_c8[city.current_building6e] = 0;
        }
        constexpr std::array<std::uint8_t, 16> awards{
            0x10,0x10,0x09,0x09,0x08,0x08,0x07,0x07,
            0x06,0x06,0x05,0x05,0x04,0x04,0x03,0x03};
        add_capture_award(awards[award_index]);
    }

    --registers_.deformation7b;
    if (registers_.deformation7b == 0) {
        ++registers_.deformation7b;
        if (sprites.pointers[0] == 0) {
            sprites.enabled_mask = 0xE0;
            // $8818 writes hardware registers D000-D007 directly. The
            // authoritative zero-page coordinates $A0-$A7 remain unchanged.
            return request_speech(1);
        }
    }

    const auto phase = static_cast<std::uint8_t>((registers_.deformation7b >> 2U) & 3U);
    registers_.scratch23 = phase;
    constexpr std::array<std::uint8_t, 4> y_delta{0, 1, 0xFF, 0};
    std::uint8_t pointer_bits = phase;
    for (int buster = 6; buster >= 5; --buster) {
        sprites.y[buster] = static_cast<std::uint8_t>(sprites.y[buster] + y_delta[phase]);
        pointer_bits = static_cast<std::uint8_t>(pointer_bits << 1U);
        set_pointer(static_cast<std::size_t>(buster), static_cast<std::uint8_t>(
            0x0EU + ((sprites.pointers[buster] & 1U) | pointer_bits)));
        pointer_bits = static_cast<std::uint8_t>(pointer_bits >> 1U);
    }
    if (sprites.pointers[0] != 0) update_pointer4(input.frame09);
    return {};
}

CatchSequenceTickResult CatchSequence::tick(CatchSequenceInput input)
{
    if (blocked_speech_ != 0) return {{}, true};
    switch (state_.city.state3a) {
    case 0x1C: tick_state28(input); return {};
    case 0x1D: return tick_state29(input);
    case 0x1E: tick_state30(input); return {};
    case 0x1F: return tick_state31(input);
    default: return {};
    }
}

CatchSequenceTickResult CatchSequence::resume_speech()
{
    if (blocked_speech_ == 0) return {};
    const auto command = std::exchange(blocked_speech_, static_cast<std::uint8_t>(0));
    if (command == 2) state_.city.state3a = 0x11;
    else if (command == 1) finish_return_targets();
    return {};
}

} // namespace ghostbusters::game
