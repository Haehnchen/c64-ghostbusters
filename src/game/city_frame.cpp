#include "game/city_frame.hpp"
#include "assets/city_data.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostbusters::game {
namespace {

std::uint8_t bcd_add(std::uint8_t left, std::uint8_t right, bool& carry)
{
    unsigned low = (left & 0x0F) + (right & 0x0F) + (carry ? 1U : 0U);
    if (low > 9) low += 6;
    unsigned value = (left & 0xF0) + (right & 0xF0) + low;
    if (value > 0x99) value += 0x60;
    carry = value > 0xFF;
    return static_cast<std::uint8_t>(value);
}

void add_pk_high(CityControlsState& state, std::uint8_t amount, bool carry,
                 std::uint8_t random)
{
    const auto old_thousands = static_cast<std::uint8_t>(state.pk_high5b & 0xF0);
    state.pk_high5b = bcd_add(state.pk_high5b, amount, carry);
    if (carry) {
        state.pk_low5a = state.pk_high5b = 0x99;
        return;
    }
    const auto new_thousands = static_cast<std::uint8_t>(state.pk_high5b & 0xF0);
    if (new_thousands == old_thousands || new_thousands < 0x50) return;

    std::uint8_t candidate = random;
    for (unsigned attempts = 0; attempts < 256; ++attempts, ++candidate) {
        auto index = static_cast<std::uint8_t>(candidate & 0x0F);
        if (index < 4) index = static_cast<std::uint8_t>(index | 4);
        if (index != 0x0A && state.map_types_ea28[index] != 0) {
            state.pending_alert80 = index;
            state.building_status_c8[index] = 0xC8;
            return;
        }
    }
}

void update_building_colors(const assets::Payload& payload,
                            CityControlsState& state, std::uint8_t frame)
{
    const assets::CityData data(payload);
    auto building = static_cast<std::uint8_t>((frame & 3) | 0x10);
    while ((building & 0x80) == 0) {
        const auto base = data.building_color_base(building);
        const auto phase = static_cast<std::uint8_t>(state.building_status_c8[building] & 3);
        // Bit 3 selects multicolor character mode; the visible per-cell hue
        // comes from the low three bits, so values 0x0C/0x0A mean purple/red.
        const auto color = phase == 3 && (frame & 0x10) == 0
                               ? static_cast<std::uint8_t>(0x0D)
                               : data.building_phase_color(phase);
        std::uint8_t offset = building >= 4 && building < 0x10 ? 0x7C : 0x2C;
        while ((offset & 0x80) == 0) {
            for (unsigned column = 0; column < 5; ++column) {
                state.characters.colors.at(base + offset) = color;
                --offset;
            }
            offset = static_cast<std::uint8_t>(offset - 0x23);
        }
        building = static_cast<std::uint8_t>(building - 4);
    }

    for (std::size_t column = 0; column < 3; ++column) {
        state.characters.colors[0x1CC + column] = 1;
        state.characters.colors[0x32E + column] = 1;
    }
    state.characters.colors[0x1CF] = 1;
}

// $740C-$746C: keep a wave of hauntings in preparation. This runs every
// common gameplay frame, independently of the slower PK/status clocks.
void activate_hauntings(CityControlsState& state, std::uint8_t random)
{
    if (state.finale_active81 != 0) return;
    for (const auto status : state.building_status_c8) {
        if (status != 0 && (status & 0x0F) < 4) return;
    }

    auto mask = static_cast<std::uint8_t>((state.move_mask3c | 0x0F) << 2U);
    unsigned remaining = 0;
    bool carry;
    do {
        ++remaining;
        carry = (mask & 0x80) != 0;
        mask = static_cast<std::uint8_t>((mask << 1U) | 1U);
    } while (!carry);

    while (remaining-- != 0) {
        auto rank = static_cast<std::uint8_t>(random & 0x1F);
        std::uint8_t retries = 0x21;
        int selected = -1;
        do {
            --retries;
            for (int index = 19; index >= 0; --index) {
                if (index == 0x11 || index == 0x0A ||
                    state.map_types_ea28[index] == 0 || state.building_status_c8[index] != 0) continue;
                --rank;
                if ((rank & 0x80) != 0) { selected = index; break; }
            }
        } while (selected < 0 && (retries & 0x80) == 0);
        if (selected < 0) return;
        state.building_status_c8[selected] = 0x10;
        random = static_cast<std::uint8_t>((random << 1U) | (random >> 7U));
    }
}

std::uint8_t translated(std::uint8_t value)
{
    if (value == 0x20) return 0;
    if (value >= 0x40) return static_cast<std::uint8_t>(value - 0x40);
    return value;
}

void insert_resource_notice(const assets::Payload& payload, CityControlsState& state)
{
    auto notice = state.notices.snapshot();
    std::uint8_t y = notice.length4a;
    while (y < notice.position4b) {
        notice.buffer[y] = 0;
        ++y;
    }
    const assets::CityData data(payload);
    for (const auto value : data.resource_notice()) {
        notice.buffer[y] = translated(value);
        ++notice.length4a;
        ++y;
    }
    notice.buffer[y] = 0xFF;
    if (notice.position4b == 0) ++notice.position4b;
    notice.cached_status4c = static_cast<std::uint8_t>(notice.cached_status4c + 1);

    const auto base = static_cast<std::uint8_t>(y - 0x43);
    auto write_bcd = [&](std::size_t offset, std::uint8_t value, bool suppress_zero) {
        auto high = static_cast<std::uint8_t>((value >> 4U) + 0x30);
        const auto low = static_cast<std::uint8_t>((value & 0x0F) + 0x30);
        if (suppress_zero && high == 0x30) high = 0;
        notice.buffer[static_cast<std::uint8_t>(base + offset)] = high;
        notice.buffer[static_cast<std::uint8_t>(base + offset + 1)] = low;
    };
    write_bcd(0x12, state.backpack_charge3e, true);
    notice.buffer[static_cast<std::uint8_t>(base + 0x24)] =
        static_cast<std::uint8_t>((state.empty_traps6b & 0x0F) + 0x30);
    notice.buffer[static_cast<std::uint8_t>(base + 0x35)] =
        static_cast<std::uint8_t>((state.backup_men3d & 0x0F) + 0x30);
    state.notices = NoticeScroller(notice);
}

} // namespace

CityFramePreResult update_city_frame_before_notice(const assets::Payload& payload,
                                                   CityControlsState& state,
                                                   CityFrameInput input)
{
    CityFramePreResult result;
    result.delay14 = input.delay14;
    result.key17 = input.key17;
    state.key17 = input.key17;

    if ((input.flags47 & 0x80) != 0 || (input.gate02 & 0x80) == 0) return result;
    if (result.delay14 != 0) {
        --result.delay14;
        if (result.delay14 == 0) result.reset_requested = true;
        return result;
    }

    if (state.state3a == 0x12 || state.state3a == 0x24) {
        update_building_colors(payload, state, input.frame09);
    }
    if (state.state3a >= 0x12 && state.state3a < 0x2A &&
        state.notices.cached_status4c() == 0 && state.key17 == 0x20) {
        state.key17 = 0;
        insert_resource_notice(payload, state);
    }
    result.key17 = state.key17;
    result.continue_frame = true;
    return result;
}

void update_city_difficulty(const assets::Payload& payload, CityControlsState& state)
{
    state.move_mask3c = assets::CityData(payload).difficulty_mask(state.pk_high5b >> 4U);
}

void update_city_frame_after_notice(const assets::Payload& payload,
                                    CityControlsState& state,
                                    CityFrameInput input)
{
    if (state.state3a < 0x12) return;

    if (input.frame09 == 0) {
        for (int index = 19; index >= 0; --index) {
            const auto old = state.building_status_c8[index];
            if ((old & 0xF0) == 0) continue;
            auto next = static_cast<std::uint8_t>(old + 0x10);
            if (next < old) {
                if (next == 0x0F) {
                    add_pk_high(state, 3, false, input.random06);
                    next = 0;
                } else {
                    next = assets::CityData(payload).next_haunt_phase((next & 0x0C) >> 2U);
                }
            }
            state.building_status_c8[index] = next;
        }
    }

    if (state.finale_active81 == 0 &&
        ((static_cast<std::uint8_t>(state.move_mask3c | 0x0F)) & input.frame09) == 0) {
        bool carry = false;
        state.pk_low5a = bcd_add(state.pk_low5a, 1, carry);
        add_pk_high(state, 0, carry, input.random06);
    }
    activate_hauntings(state, input.random06);
}

} // namespace ghostbusters::game
