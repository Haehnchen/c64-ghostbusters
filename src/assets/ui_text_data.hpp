#pragma once

#include "assets/payload.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

enum class UiScript : std::uint8_t {
    franchise_intro = 1,
    account_question = 2,
    new_account = 3,
    account_number = 4,
    invalid_account = 5,
    vehicle_menu = 6,
    balance_prefix = 7,
    vehicle_instructions = 8,
    poor_ending = 13,
    failure_prefix = 14,
    failure_suffix = 15,
    credit_prefix = 16,
    account_prefix = 17,
    restart_prompt = 18,
    success_prefix = 24,
    success_suffix = 25,
    starting_balance = 26,
    ending_balance = 27,
};

// Named text streams include their terminator; fixed-width captions do not.
class UiTextData {
public:
    static constexpr std::size_t kVehicleCount = 4;
    static constexpr std::size_t kVehicleHintRows = 2;
    static constexpr std::size_t kVehicleHintWidth = 36;
    struct VehiclePrice { std::uint8_t high_bcd; std::uint8_t middle_bcd; };

    explicit UiTextData(const Payload& payload)
    {
        for (std::size_t i = 0; i < kScripts.size(); ++i)
            if (!kScripts[i].empty()) scripts_[i] = validated(payload.asset(kScripts[i]));
        for (std::size_t i = 0; i < previews_.size(); ++i)
            previews_[i] = validated(payload.asset(kPreviews[i]));
        for (std::size_t i = 0; i < hints_.size(); ++i) {
            hints_[i] = payload.asset(kHints[i]);
            if (hints_[i].size() != kVehicleHintWidth)
                throw std::runtime_error("Unexpected vehicle caption width");
        }
    }

    [[nodiscard]] std::span<const std::uint8_t> script(UiScript id) const
    {
        const auto index = static_cast<std::size_t>(id);
        if (index >= scripts_.size() || scripts_[index].empty())
            throw std::out_of_range("unsupported UI script id");
        return scripts_[index];
    }

    [[nodiscard]] std::span<const std::uint8_t> vehicle_preview(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return previews_[vehicle];
    }

    [[nodiscard]] std::span<const std::uint8_t, kVehicleHintWidth>
    vehicle_hint_row(std::size_t row) const
    {
        if (row >= hints_.size()) throw std::out_of_range("vehicle hint row index");
        return std::span<const std::uint8_t, kVehicleHintWidth>(hints_[row].data(), kVehicleHintWidth);
    }

    [[nodiscard]] VehiclePrice vehicle_price(std::uint8_t vehicle) const
    {
        require_vehicle(vehicle);
        return kPrices[vehicle];
    }

private:
    static constexpr std::array<std::string_view, 28> kScripts{
        "",
        "text/franchise_intro",
        "text/account_question",
        "text/new_account",
        "text/account_number",
        "text/invalid_account",
        "text/vehicle_menu",
        "text/balance_prefix",
        "text/vehicle_instructions",
        "",
        "",
        "",
        "",
        "text/poor_ending",
        "text/failure_prefix",
        "text/failure_suffix",
        "text/credit_prefix",
        "text/account_prefix",
        "text/restart_prompt",
        "",
        "",
        "",
        "",
        "",
        "text/success_prefix",
        "text/success_suffix",
        "text/starting_balance",
        "text/ending_balance",
    };
    static constexpr std::array<std::string_view, 4> kPreviews{
        "text/vehicle_preview_0", "text/vehicle_preview_1", "text/vehicle_preview_2", "text/vehicle_preview_3"};
    static constexpr std::array<std::string_view, 2> kHints{"text/vehicle_hint_0", "text/vehicle_hint_1"};
    static constexpr std::array<VehiclePrice, 4> kPrices{{{0, 0x20}, {0, 0x48}, {0, 0x60}, {1, 0x50}}};

    static void require_vehicle(std::uint8_t vehicle)
    {
        if (vehicle >= kVehicleCount) throw std::out_of_range("vehicle UI index");
    }
    static std::span<const std::uint8_t> validated(std::span<const std::uint8_t> bytes)
    {
        if (bytes.empty() || bytes.back() != 0xFF ||
            std::find(bytes.begin(), bytes.end() - 1, std::uint8_t{0xFF}) != bytes.end() - 1)
            throw std::runtime_error("Text script has an invalid terminator");
        return bytes;
    }

    std::array<std::span<const std::uint8_t>, 28> scripts_{};
    std::array<std::span<const std::uint8_t>, 4> previews_{};
    std::array<std::span<const std::uint8_t>, 2> hints_{};
};

} // namespace ghostbusters::assets
