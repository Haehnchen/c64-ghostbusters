#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostbusters::assets::vehicle_tables {

struct DriveSprite {
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t pointer;

    constexpr bool operator==(const DriveSprite&) const = default;
};

// Native drive-scene constants formerly embedded in unrelated aggregate data.
inline constexpr std::array<DriveSprite, 8> kInitialDriveSprites{{
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x04}, {0x00, 0x00, 0x05},
    {0x36, 0x00, 0x0C}, {0x6D, 0x00, 0x0C},
    {0x6D, 0x00, 0x0C}, {0x36, 0x00, 0x0C},
}};
inline constexpr std::array<std::uint8_t, 4> kVacuumTargetY{
    0x78, 0x70, 0x6C, 0x76};
inline constexpr std::array<std::uint8_t, 4> kMulticolor2{
    0x03, 0x06, 0x06, 0x04};
inline constexpr std::array<std::uint8_t, 4> kDriveSpriteY{
    0x86, 0x87, 0x84, 0x94};
inline constexpr std::array<std::uint8_t, 4> kSirenColors{
    0x06, 0x0E, 0x03, 0x01};
inline constexpr std::array<std::uint8_t, 4> kSpeedLimits{
    0x60, 0x70, 0x80, 0xA0};
inline constexpr std::array<std::uint8_t, 12> kGridFirstCodes{
    0x40, 0x50, 0x60, 0x70, 0x80, 0x90,
    0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};

} // namespace ghostbusters::assets::vehicle_tables
