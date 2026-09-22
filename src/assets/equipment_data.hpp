#pragma once

#include "assets/payload.hpp"
#include "assets/notice_data.hpp"
#include "assets/capture_tables.hpp"
#include "assets/equipment_tables.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ghostbusters::assets {

// Bounded shop data indexed by category, vehicle, item and sprite.
class EquipmentData {
public:
    static constexpr std::size_t kVehicleCount = 4;
    static constexpr std::size_t kCategoryCount = 3;
    static constexpr std::size_t kItemsPerCategory = 5;
    static constexpr std::size_t kSpriteCount = 8;
    static constexpr std::size_t kItemCount = 8;
    static constexpr std::size_t kCarriedPositionCount = 96;

    struct SpritePosition {
        std::uint8_t x;
        std::uint8_t y;
    };

    explicit EquipmentData(const Payload& payload)
        : scripts_{{payload.asset("text/equipment_category_0"),
                    payload.asset("text/equipment_category_1"),
                    payload.asset("text/equipment_category_2")}},
          notices_(payload),
          scene_graphics_(payload.asset("graphics/shared_sprites"))
    {
        require_size(scripts_[0], kCategoryScriptSizes[0],
                     "text/equipment_category_0");
        require_size(scripts_[1], kCategoryScriptSizes[1],
                     "text/equipment_category_1");
        require_size(scripts_[2], kCategoryScriptSizes[2],
                     "text/equipment_category_2");
        for (const auto script : scripts_) {
            if (script.empty() || script.back() != 0xFF) {
                throw std::runtime_error("Equipment script has no terminator");
            }
        }
        require_size(scene_graphics_, kSceneGraphicsSize,
                     "graphics/shared_sprites");
    }

    [[nodiscard]] std::span<const std::uint8_t> scene_graphics() const noexcept
    {
        return scene_graphics_;
    }

    [[nodiscard]] std::span<const std::uint8_t> category_script(
        std::uint8_t category) const
    {
        require_category(category);
        return scripts_[category];
    }

    [[nodiscard]] std::uint8_t vehicle_multicolor2(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return equipment_tables::kVehicleMulticolor2[vehicle];
    }

    [[nodiscard]] std::uint8_t item_pointer(std::uint8_t category,
                                             std::size_t item) const
    {
        require_category(category);
        if (item >= kItemsPerCategory) {
            throw std::out_of_range("equipment category item index");
        }
        return equipment_tables::kCategoryItems[
            category * kCategoryStride + item];
    }

    [[nodiscard]] std::uint8_t ownership_mask(std::uint8_t pointer) const
    {
        return equipment_tables::kOwnershipMasks[item_index(pointer)];
    }

    [[nodiscard]] std::uint8_t price_middle_bcd(std::uint8_t pointer) const
    {
        return equipment_tables::kPrices[item_index(pointer)];
    }

    [[nodiscard]] SpritePosition initial_sprite_position(
        std::size_t sprite) const
    {
        if (sprite >= kSpriteCount) {
            throw std::out_of_range("equipment sprite index");
        }
        const auto& position = equipment_tables::kInitialPositions[sprite];
        return {position[0], position[1]};
    }

    [[nodiscard]] std::uint8_t carried_graphic(std::uint8_t category,
                                                std::size_t row) const
    {
        require_category(category);
        if (row >= kCarriedGraphicStride) {
            throw std::out_of_range("equipment carried-item row");
        }
        return equipment_tables::kCarriedGraphics[category][row];
    }

    [[nodiscard]] std::uint8_t category_state_limit(
        std::uint8_t category) const
    {
        require_category(category);
        return equipment_tables::kCategoryStateLimits[category];
    }

    [[nodiscard]] std::uint8_t vehicle_capacity(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return equipment_tables::kVehicleCapacities[vehicle];
    }

    [[nodiscard]] std::uint8_t vehicle_position_base(
        std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return equipment_tables::kVehiclePositionBases[vehicle];
    }

    [[nodiscard]] std::uint8_t carried_target_x(std::size_t index) const
    {
        require_carried_position(index);
        return equipment_tables::kCarriedTargetX[index];
    }

    [[nodiscard]] std::uint8_t carried_target_y(std::size_t index) const
    {
        require_carried_position(index);
        return equipment_tables::kCarriedTargetY[index];
    }

    [[nodiscard]] std::uint8_t forklift_x(std::uint8_t phase) const
    {
        if (phase >= 2) throw std::out_of_range("equipment forklift x phase");
        return equipment_tables::kForkliftX[phase];
    }

    [[nodiscard]] std::uint8_t sprite_color(std::uint8_t pointer) const
    {
        if (pointer > kMaximumSpritePointer) {
            throw std::out_of_range("equipment sprite pointer");
        }
        return capture_tables::kSpriteColors[pointer];
    }

    [[nodiscard]] std::span<const std::uint8_t, 10> balance_label() const
        noexcept
    {
        return equipment_tables::kBalanceLabel;
    }

    [[nodiscard]] std::span<const std::uint8_t> capacity_notice() const
    {
        return notices_.notice(kCapacityNotice);
    }

private:
    static constexpr std::size_t kSceneGraphicsSize = 0x0EC0;
    static constexpr std::array<std::size_t, kCategoryCount> kCategoryScriptSizes{
        283, 283, 289};
    static constexpr std::size_t kCategoryStride = 8;
    static constexpr std::size_t kCarriedGraphicStride = 4;
    static constexpr std::uint8_t kFirstItemPointer = 0x33;
    static constexpr std::uint8_t kMaximumSpritePointer = 0x3A;
    static constexpr std::uint8_t kCapacityNotice = 7;

    static void require_size(std::span<const std::uint8_t> bytes,
                             std::size_t expected, const char* asset)
    {
        if (bytes.size() != expected) {
            throw std::runtime_error(std::string(asset) +
                                     " asset has an unexpected size");
        }
    }

    static void require_vehicle(std::uint8_t vehicle)
    {
        if (vehicle >= kVehicleCount) throw std::out_of_range("equipment vehicle index");
    }

    static void require_category(std::uint8_t category)
    {
        if (category >= kCategoryCount) throw std::out_of_range("equipment category index");
    }

    static void require_carried_position(std::size_t index)
    {
        if (index >= kCarriedPositionCount) {
            throw std::out_of_range("equipment carried-position index");
        }
    }

    [[nodiscard]] static std::size_t item_index(std::uint8_t pointer)
    {
        if (pointer < kFirstItemPointer ||
            pointer >= kFirstItemPointer + kItemCount) {
            throw std::out_of_range("equipment item sprite pointer");
        }
        return pointer - kFirstItemPointer;
    }

    std::array<std::span<const std::uint8_t>, kCategoryCount> scripts_;
    NoticeData notices_;
    std::span<const std::uint8_t> scene_graphics_;
};

} // namespace ghostbusters::assets
