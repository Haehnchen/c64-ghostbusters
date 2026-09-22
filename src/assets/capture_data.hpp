#pragma once

#include "assets/capture_tables.hpp"
#include "assets/payload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ghostbusters::assets {

// Bounded semantic assets used by building entry and capture scenes.
class CaptureData {
public:
    explicit CaptureData(const Payload& payload)
        : building_charsets_{{payload.asset("buildings/normal/charset"),
                              payload.asset("buildings/zuul/charset")}},
          building_screens_{{payload.asset("buildings/normal/screen"),
                             payload.asset("buildings/zuul/screen")}},
          building_colors_{{payload.asset("buildings/normal/colors"),
                            payload.asset("buildings/zuul/colors")}},
          low_overlay_tiles_{{{},
              payload.asset("buildings/overlays/low/1/tiles"),
              payload.asset("buildings/overlays/low/2/tiles"),
              payload.asset("buildings/overlays/low/3/tiles")}},
          low_overlay_charsets_{{{},
              payload.asset("buildings/overlays/low/1/charset"),
              payload.asset("buildings/overlays/low/2/charset"),
              payload.asset("buildings/overlays/low/3/charset")}},
          high_overlay_screens_{{{},
              payload.asset("buildings/overlays/high/1/screen"),
              payload.asset("buildings/overlays/high/2/screen"),
              payload.asset("buildings/overlays/high/3/screen")}},
          high_overlay_charsets_{{{},
              payload.asset("buildings/overlays/high/1/charset"),
              payload.asset("buildings/overlays/high/2/charset"),
              payload.asset("buildings/overlays/high/3/charset")}},
          descriptor_patches_{{{},
              payload.asset("buildings/descriptors/1/charset_patch"),
              payload.asset("buildings/descriptors/2/charset_patch"),
              payload.asset("buildings/descriptors/3/charset_patch")}},
          headquarters_sign_(payload.asset("buildings/headquarters/sign")),
          vehicle_charsets_{{payload.asset("buildings/vehicles/compact/charset"),
              payload.asset("buildings/vehicles/hearse/charset"),
              payload.asset("buildings/vehicles/wagon/charset"),
              payload.asset("buildings/vehicles/performance/charset")}},
          scene_graphics_(payload.asset("graphics/shared_sprites"))
    {
        for (const auto bytes : building_charsets_)
            require_size(bytes, 0x600, "building charset");
        for (const auto bytes : building_screens_)
            require_size(bytes, 0x348, "building screen");
        for (const auto bytes : building_colors_)
            require_size(bytes, 0x348, "building colors");
        for (std::size_t variant = 1; variant < 4; ++variant) {
            require_size(low_overlay_tiles_[variant], 0x18, "low overlay tiles");
            require_size(low_overlay_charsets_[variant], 0x70,
                         "low overlay charset");
            require_size(high_overlay_screens_[variant], 0xDC,
                         "high overlay screen");
            require_size(high_overlay_charsets_[variant], 0x200,
                         "high overlay charset");
            require_size(descriptor_patches_[variant], 0x10,
                         "descriptor charset patch");
        }
        require_size(headquarters_sign_, 12, "headquarters sign");
        for (const auto bytes : vehicle_charsets_)
            require_size(bytes, 0x60, "parked vehicle charset");
        require_size(scene_graphics_, 0x0EC0, "graphics/shared_sprites");
    }

    [[nodiscard]] std::span<const std::uint8_t> scene_graphics() const noexcept
    { return scene_graphics_; }

    [[nodiscard]] std::span<const std::uint8_t> building_charset(bool zuul) const
    { return building_charsets_[zuul ? 1U : 0U]; }

    [[nodiscard]] std::span<const std::uint8_t> building_screen(bool zuul) const
    { return building_screens_[zuul ? 1U : 0U]; }

    [[nodiscard]] std::uint8_t building_color(bool zuul, std::size_t offset) const
    {
        if (offset >= 0x348) throw std::out_of_range("building color offset");
        return building_colors_[zuul ? 1U : 0U][offset];
    }

    [[nodiscard]] std::uint8_t building_descriptor(std::size_t building) const
    {
        if (building >= capture_tables::kBuildingDescriptors.size())
            throw std::out_of_range("building descriptor index");
        return capture_tables::kBuildingDescriptors[building];
    }

    [[nodiscard]] std::uint8_t low_overlay_tile(std::uint8_t overlay,
                                                std::size_t index) const
    {
        require_overlay(overlay);
        if (index >= 0x18) throw std::out_of_range("low building overlay tile");
        return low_overlay_tiles_[overlay][index];
    }

    [[nodiscard]] std::uint8_t low_overlay_destination(std::size_t index) const
    {
        if (index >= capture_tables::kLowOverlayDestinations.size())
            throw std::out_of_range("low overlay destination");
        return capture_tables::kLowOverlayDestinations[index];
    }

    [[nodiscard]] std::span<const std::uint8_t>
    low_overlay_charset(std::uint8_t overlay) const
    { require_overlay(overlay); return low_overlay_charsets_[overlay]; }

    [[nodiscard]] std::span<const std::uint8_t>
    high_overlay_screen(std::uint8_t overlay) const
    { require_overlay(overlay); return high_overlay_screens_[overlay]; }

    [[nodiscard]] std::span<const std::uint8_t>
    high_overlay_charset(std::uint8_t overlay) const
    { require_overlay(overlay); return high_overlay_charsets_[overlay]; }

    [[nodiscard]] std::span<const std::uint8_t>
    descriptor_charset_patch(std::uint8_t patch) const
    { require_overlay(patch); return descriptor_patches_[patch]; }

    [[nodiscard]] std::uint8_t headquarters_sign(std::size_t index) const
    {
        if (index >= headquarters_sign_.size())
            throw std::out_of_range("headquarters sign index");
        return headquarters_sign_[index];
    }

    [[nodiscard]] std::span<const std::uint8_t>
    vehicle_charset(std::uint8_t vehicle) const
    {
        if (vehicle >= vehicle_charsets_.size())
            throw std::out_of_range("vehicle index");
        return vehicle_charsets_[vehicle];
    }

    [[nodiscard]] std::uint8_t vehicle_color(std::uint8_t vehicle) const
    {
        if (vehicle >= capture_tables::kVehicleColors.size())
            throw std::out_of_range("vehicle color index");
        return capture_tables::kVehicleColors[vehicle];
    }

    [[nodiscard]] std::uint8_t initial_sprite_pointer(std::size_t sprite) const
    {
        if (sprite >= capture_tables::kInitialSpritePointers.size())
            throw std::out_of_range("initial sprite pointer");
        return capture_tables::kInitialSpritePointers[sprite];
    }

    [[nodiscard]] std::array<std::uint8_t, 2>
    initial_sprite_position(std::size_t sprite) const
    {
        if (sprite >= capture_tables::kInitialSpritePositions.size())
            throw std::out_of_range("initial sprite position");
        const auto point = capture_tables::kInitialSpritePositions[sprite];
        return {point.x, point.y};
    }

    [[nodiscard]] std::array<std::uint8_t, 2>
    initial_sprite_target(std::size_t sprite) const
    {
        if (sprite >= capture_tables::kInitialSpriteTargets.size())
            throw std::out_of_range("initial sprite target");
        const auto point = capture_tables::kInitialSpriteTargets[sprite];
        return {point.x, point.y};
    }

    [[nodiscard]] std::array<std::uint8_t, 2> second_buster_target() const
    {
        return {capture_tables::kSecondBusterTarget.x,
                capture_tables::kSecondBusterTarget.y};
    }

    [[nodiscard]] std::array<std::uint8_t, 2>
    ghost_delta(std::uint8_t direction) const
    {
        if (direction >= capture_tables::kGhostDeltas.size())
            throw std::out_of_range("ghost direction");
        const auto delta = capture_tables::kGhostDeltas[direction];
        return {delta.x, delta.y};
    }

    [[nodiscard]] std::uint8_t sprite_color(std::uint8_t pointer) const
    {
        if (pointer >= capture_tables::kSpriteColors.size())
            throw std::out_of_range("sprite color pointer");
        return capture_tables::kSpriteColors[pointer];
    }

private:
    static void require_overlay(std::uint8_t overlay)
    {
        if (overlay == 0 || overlay >= 4)
            throw std::out_of_range("building overlay variant");
    }

    static void require_size(std::span<const std::uint8_t> data,
                             std::size_t expected, const char* name)
    {
        if (data.size() != expected)
            throw std::runtime_error(std::string(name) +
                                     " asset has an unexpected size");
    }

    std::array<std::span<const std::uint8_t>, 2> building_charsets_;
    std::array<std::span<const std::uint8_t>, 2> building_screens_;
    std::array<std::span<const std::uint8_t>, 2> building_colors_;
    std::array<std::span<const std::uint8_t>, 4> low_overlay_tiles_;
    std::array<std::span<const std::uint8_t>, 4> low_overlay_charsets_;
    std::array<std::span<const std::uint8_t>, 4> high_overlay_screens_;
    std::array<std::span<const std::uint8_t>, 4> high_overlay_charsets_;
    std::array<std::span<const std::uint8_t>, 4> descriptor_patches_;
    std::span<const std::uint8_t> headquarters_sign_;
    std::array<std::span<const std::uint8_t>, 4> vehicle_charsets_;
    std::span<const std::uint8_t> scene_graphics_;
};

} // namespace ghostbusters::assets
