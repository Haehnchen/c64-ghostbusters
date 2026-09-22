#pragma once

#include "assets/capture_data.hpp"
#include "assets/city_tables.hpp"
#include "assets/notice_data.hpp"
#include "assets/payload.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Named view over the data consumed by the Marshmallow handlers (States
// $24-$27). The native state machine sees bounded spans and screen-relative
// offsets rather than source pointers.
class MarshmallowData {
public:
    struct CityMapQuadrant {
        std::ptrdiff_t screen_offset;
        std::span<const std::uint8_t> cells;
    };

    struct TrailCell {
        std::size_t screen_offset;
        std::uint8_t linear_low;
    };

    explicit MarshmallowData(const Payload& payload)
        : capture_(payload),
          map_tiles_(payload.asset("city/tiles")),
          notices_(payload)
    {
        if (map_tiles_.size() != kMapTilesSize) {
            throw std::runtime_error(
                "city/tiles asset has an unexpected size");
        }
    }

    [[nodiscard]] std::span<const std::uint8_t> scene_graphics() const noexcept
    {
        return capture_.scene_graphics();
    }

    [[nodiscard]] std::uint8_t sprite_color(std::uint8_t pointer) const
    {
        return capture_.sprite_color(pointer);
    }

    [[nodiscard]] std::span<const std::uint8_t> notice(std::uint8_t index) const
    {
        return notices_.notice(index);
    }

    [[nodiscard]] std::ptrdiff_t map_destination(std::size_t building) const
    {
        if (building >= city_tables::kMapDestinations.size()) {
            throw std::out_of_range("marshmallow city-map building");
        }
        return city_tables::kMapDestinations[building];
    }

    [[nodiscard]] std::span<const std::uint8_t>
    map_row(std::uint8_t selector, std::uint8_t row) const
    {
        if (selector >= kMapTypeCount || row >= kMapQuadrantCount) {
            throw std::out_of_range("marshmallow city-map row");
        }
        const auto offset = selector * kMapTypeStride +
                            row * kMapQuadrantWidth;
        return map_tiles_.subspan(offset, kMapQuadrantWidth);
    }

    // Resolve the map cell used by the State-36 trail update.
    [[nodiscard]] TrailCell trail_cell(std::uint8_t row,
                                       std::uint8_t column) const
    {
        if (row >= kTrailRowCount) {
            throw std::out_of_range("marshmallow city trail row");
        }
        const auto row_offset = kTrailFirstCell +
                                static_cast<std::size_t>(row) * kScreenWidth;
        const auto screen_offset = row_offset + column;
        if (screen_offset >= kScreenSize) {
            throw std::out_of_range("marshmallow city trail column");
        }
        return {screen_offset, static_cast<std::uint8_t>(screen_offset)};
    }

    // Resolve one five-cell row and its signed native-screen destination.
    [[nodiscard]] CityMapQuadrant city_map_quadrant(
        std::uint8_t building, std::uint8_t selector,
        std::uint8_t quadrant) const
    {
        if (quadrant >= kMapQuadrantCount) {
            throw std::out_of_range("marshmallow city-map quadrant");
        }

        return {map_destination(building) +
                    static_cast<std::ptrdiff_t>(quadrant * kMapQuadrantStride),
                map_row(selector, quadrant)};
    }

private:
    static constexpr std::size_t kMapTilesSize = 0x00A0;

    static constexpr std::size_t kMapQuadrantCount = 4;
    static constexpr std::size_t kMapTypeCount = 8;
    static constexpr std::size_t kMapQuadrantWidth = 5;
    static constexpr std::size_t kMapQuadrantStride = 40;
    static constexpr std::size_t kMapTypeStride = 20;
    static constexpr std::size_t kScreenWidth = 40;
    static constexpr std::size_t kScreenSize = 0x0400;
    static constexpr std::size_t kTrailFirstCell = 124;
    static constexpr std::size_t kTrailRowCount = 26;
    // Negative bases become visible after adding the row stride; clip only
    // the final cell position, never the base itself.

    CaptureData capture_;
    std::span<const std::uint8_t> map_tiles_;
    NoticeData notices_;
};

} // namespace ghostbusters::assets
