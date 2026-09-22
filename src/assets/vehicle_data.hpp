#pragma once

#include "assets/payload.hpp"
#include "assets/vehicle_tables.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Vehicle glyph sheets and bounded drive-scene tables.
class VehicleData {
public:
    static constexpr std::size_t kVehicleCount = 4;
    static constexpr std::size_t kCharsetBytes = 768;
    static constexpr std::size_t kGridColumns = 12;
    static constexpr std::size_t kSirenPhases = 4;

    using DriveSprite = vehicle_tables::DriveSprite;

    explicit VehicleData(const Payload& payload)
        : charsets_{{payload.asset("vehicles/compact"),
                     payload.asset("vehicles/hearse"),
                     payload.asset("vehicles/wagon"),
                     payload.asset("vehicles/performance")}}
    {
        for (const auto charset : charsets_) {
            if (charset.size() != kCharsetBytes) {
                throw std::runtime_error(
                    "native vehicle charset asset has an unexpected size");
            }
        }
    }

    [[nodiscard]] DriveSprite initial_drive_sprite(std::size_t sprite) const
    {
        if (sprite >= 8) throw std::out_of_range("drive sprite index");
        return vehicle_tables::kInitialDriveSprites[sprite];
    }

    [[nodiscard]] std::uint8_t vacuum_target_y(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return vehicle_tables::kVacuumTargetY[vehicle];
    }

    [[nodiscard]] std::array<std::uint8_t, kCharsetBytes>
    vehicle_charset(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        std::array<std::uint8_t, kCharsetBytes> result;
        std::copy(charsets_[vehicle].begin(), charsets_[vehicle].end(),
                  result.begin());
        return result;
    }

    [[nodiscard]] std::span<const std::uint8_t, kGridColumns>
    grid_first_codes() const noexcept
    {
        return vehicle_tables::kGridFirstCodes;
    }

    [[nodiscard]] std::uint8_t multicolor2(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return vehicle_tables::kMulticolor2[vehicle];
    }

    [[nodiscard]] std::uint8_t drive_sprite_y(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return vehicle_tables::kDriveSpriteY[vehicle];
    }

    [[nodiscard]] std::uint8_t speed_limit(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return vehicle_tables::kSpeedLimits[vehicle];
    }

    [[nodiscard]] std::uint8_t siren_color(std::uint8_t phase) const
    {
        if (phase >= kSirenPhases) {
            throw std::out_of_range("vehicle siren phase");
        }
        return vehicle_tables::kSirenColors[phase];
    }

private:
    static void require_vehicle(std::uint8_t vehicle)
    {
        if (vehicle >= kVehicleCount) {
            throw std::out_of_range("vehicle index");
        }
    }

    std::array<std::span<const std::uint8_t>, kVehicleCount> charsets_;
};

} // namespace ghostbusters::assets
