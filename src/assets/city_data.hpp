#pragma once

#include "assets/marshmallow_data.hpp"
#include "assets/city_tables.hpp"
#include "assets/payload.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Named, bounded view over the tables consumed by the city state-18 handler.
// The city logic deals only in sprite records, table indices and native screen
// offsets. MarshmallowData owns the shared map layout.
class CityData {
public:
    struct ArrivingSprite {
        std::uint8_t x;
        std::uint8_t y;
        std::uint8_t target_x;
        std::uint8_t target_y;
    };

    struct ZuulSprite {
        std::uint8_t check_x;
        std::uint8_t check_y;
        std::uint8_t target_x;
        std::uint8_t target_y;
    };

    struct EntrySprite {
        std::uint8_t pointer;
        std::uint8_t x;
        std::uint8_t y;
        std::uint8_t target_x;
        std::uint8_t target_y;
    };

    explicit CityData(const Payload& payload)
        : marshmallow_(payload),
          charset_(payload.asset("city/charset")),
          status_row_(payload.asset("city/status_row")),
          account_label_(payload.asset("text/account_label")),
          resource_notice_(payload.asset("text/resource_notice"))
    {
        if (charset_.size() != 0x0600 || status_row_.size() != 40 ||
            account_label_.size() != 3) {
            throw std::runtime_error("unexpected native city graphics size");
        }
        if (resource_notice_.empty() || resource_notice_.back() != 0xFF) {
            throw std::runtime_error("resource notice has no terminator");
        }
    }

    [[nodiscard]] std::size_t building_color_base(std::size_t building) const
    {
        if (building >= city_tables::kBuildingColorBases.size()) {
            throw std::out_of_range("city building color index");
        }
        return city_tables::kBuildingColorBases[building];
    }

    [[nodiscard]] std::uint8_t building_phase_color(std::uint8_t phase) const
    {
        if (phase >= city_tables::kBuildingPhaseColors.size()) {
            throw std::out_of_range("city building color phase");
        }
        return city_tables::kBuildingPhaseColors[phase];
    }

    [[nodiscard]] std::uint8_t next_haunt_phase(std::uint8_t phase) const
    {
        if (phase >= city_tables::kNextHauntPhase.size()) {
            throw std::out_of_range("city haunting phase");
        }
        return city_tables::kNextHauntPhase[phase];
    }

    [[nodiscard]] std::uint8_t difficulty_mask(std::uint8_t thousands) const
    {
        if (thousands >= city_tables::kDifficultyMasks.size()) {
            throw std::out_of_range("city PK thousands digit");
        }
        return city_tables::kDifficultyMasks[thousands];
    }

    [[nodiscard]] std::span<const std::uint8_t> resource_notice() const
    {
        for (std::size_t size = 0; size < resource_notice_.size(); ++size) {
            if (resource_notice_[size] == 0xFF) {
                return resource_notice_.first(size);
            }
        }
        throw std::runtime_error("unterminated resource notice");
    }

    [[nodiscard]] std::span<const std::uint8_t> scene_graphics() const noexcept
    {
        return marshmallow_.scene_graphics();
    }

    [[nodiscard]] std::uint8_t sprite_color(std::uint8_t pointer) const
    {
        return marshmallow_.sprite_color(pointer);
    }

    [[nodiscard]] std::uint8_t map_type(std::size_t index) const
    {
        if (index >= city_tables::kMapTypes.size()) {
            throw std::out_of_range("city map-type index");
        }
        return city_tables::kMapTypes[index];
    }

    [[nodiscard]] std::span<const std::uint8_t> city_charset() const
    {
        return charset_;
    }

    [[nodiscard]] std::span<const std::uint8_t>
    vertical_road(std::size_t block) const
    {
        if (block >= 4) throw std::out_of_range("city vertical-road block");
        return std::span<const std::uint8_t, 11>(
            city_tables::kVerticalRoad.data() + block * 11, 11);
    }

    [[nodiscard]] std::span<const std::uint8_t> status_row() const
    {
        return status_row_;
    }

    [[nodiscard]] std::span<const std::uint8_t> pk_label() const
    {
        return city_tables::kPkLabel;
    }

    [[nodiscard]] std::span<const std::uint8_t> account_label() const
    {
        return account_label_;
    }

    [[nodiscard]] std::uint8_t pk_separator() const
    {
        return city_tables::kPkSeparator;
    }

    [[nodiscard]] EntrySprite entry_sprite(std::size_t sprite) const
    {
        if (sprite >= city_tables::kEntrySprites.size()) {
            throw std::out_of_range("city entry-sprite index");
        }
        const auto value = city_tables::kEntrySprites[sprite];
        return {value.pointer, value.x, value.y, value.target_x, value.target_y};
    }

    [[nodiscard]] std::ptrdiff_t map_destination(std::size_t column) const
    {
        return marshmallow_.map_destination(column);
    }

    [[nodiscard]] std::span<const std::uint8_t>
    map_row(std::uint8_t selector, std::uint8_t row) const
    {
        return marshmallow_.map_row(selector, row);
    }

    [[nodiscard]] std::uint8_t map_tile(std::uint8_t selector,
                                        std::uint8_t row,
                                        std::uint8_t column) const
    {
        if (column >= 5) {
            throw std::out_of_range("city map tile index");
        }
        return map_row(selector, row)[column];
    }

    [[nodiscard]] std::span<const std::uint8_t> notice(std::uint8_t index) const
    {
        return marshmallow_.notice(index);
    }

    [[nodiscard]] ArrivingSprite arriving_sprite(std::size_t sprite) const
    {
        if (sprite < 4 || sprite >= 8) {
            throw std::out_of_range("city arriving-sprite index");
        }
        const auto value = city_tables::kArrivingSprites[sprite - 4U];
        return {value.x, value.y, value.target_x, value.target_y};
    }

    [[nodiscard]] std::uint8_t zuul_route_x_offset(
        std::uint8_t selector) const
    {
        if (selector > 6 || (selector & 1U) != 0) {
            throw std::out_of_range("city Zuul route selector");
        }
        return city_tables::kZuulRouteXOffsets[selector / 2U];
    }

    [[nodiscard]] std::uint8_t zuul_route_y_offset(
        std::uint8_t selector) const
    {
        if (selector > 6 || (selector & 1U) != 0) {
            throw std::out_of_range("city Zuul route selector");
        }
        return city_tables::kZuulRouteYOffsets[selector / 2U];
    }

    [[nodiscard]] std::uint8_t zuul_target_x(std::uint8_t lookup) const
    {
        if (lookup > 6) {
            throw std::out_of_range("city Zuul target-x lookup");
        }
        return city_tables::kZuulTargetX[lookup];
    }

    [[nodiscard]] std::uint8_t zuul_target_y(std::uint8_t lookup) const
    {
        if (lookup > 5) {
            throw std::out_of_range("city Zuul target-y lookup");
        }
        return city_tables::kZuulTargetY[lookup];
    }

    [[nodiscard]] ZuulSprite zuul_sprite(std::size_t sprite) const
    {
        if (sprite < 2 || sprite >= 4) {
            throw std::out_of_range("city Zuul-sprite index");
        }
        const auto value = city_tables::kZuulSprites[sprite - 2U];
        return {value.check_x, value.check_y, value.target_x, value.target_y};
    }

    [[nodiscard]] MarshmallowData::TrailCell trail_cell(
        std::uint8_t row, std::uint8_t column) const
    {
        return marshmallow_.trail_cell(row, column);
    }

private:

    MarshmallowData marshmallow_;
    std::span<const std::uint8_t> charset_;
    std::span<const std::uint8_t> status_row_;
    std::span<const std::uint8_t> account_label_;
    std::span<const std::uint8_t> resource_notice_;
};

} // namespace ghostbusters::assets
