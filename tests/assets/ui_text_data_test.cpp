#include "assets/payload.hpp"
#include "assets/ui_text_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::assets::UiScript;
using ghostbusters::assets::UiTextData;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
void expect_out_of_range(Function&& function, const char* message)
{
    try {
        function();
        check(false, message);
    } catch (const std::out_of_range&) {
    } catch (...) {
        check(false, message);
    }
}

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes)
{
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) hash = (hash ^ byte) * 16777619U;
    return hash;
}

void test_scripts(const UiTextData& data)
{
    struct Expected {
        UiScript id;
        std::size_t length;
        std::uint32_t hash;
    };
    constexpr std::array expected{
        Expected{UiScript::franchise_intro, 205, 0xEFC3B03CU},
        Expected{UiScript::account_question, 26, 0x2BD493ACU},
        Expected{UiScript::new_account, 153, 0x4E2A9BFEU},
        Expected{UiScript::account_number, 30, 0x06479ADDU},
        Expected{UiScript::invalid_account, 24, 0x76663C07U},
        Expected{UiScript::vehicle_menu, 133, 0xC9EF5F9AU},
        Expected{UiScript::balance_prefix, 12, 0xABC89A31U},
        Expected{UiScript::vehicle_instructions, 120, 0x3807E2ABU},
        Expected{UiScript::poor_ending, 144, 0xAC6621FFU},
        Expected{UiScript::failure_prefix, 10, 0xDB14D6D4U},
        Expected{UiScript::failure_suffix, 92, 0xC9C7AF07U},
        Expected{UiScript::credit_prefix, 90, 0x06D9085DU},
        Expected{UiScript::account_prefix, 31, 0x54217AC7U},
        Expected{UiScript::restart_prompt, 42, 0xC5E35466U},
        Expected{UiScript::success_prefix, 34, 0x82F709D4U},
        Expected{UiScript::success_suffix, 95, 0x9F3D64BCU},
        Expected{UiScript::starting_balance, 21, 0x607FCF5EU},
        Expected{UiScript::ending_balance, 21, 0x8559413FU},
    };

    for (const auto& item : expected) {
        const auto script = data.script(item.id);
        check(script.size() == item.length,
              "each named UI script has its pinned inclusive length");
        check(!script.empty() && script.back() == 0xFF,
              "each named UI script includes its FF boundary");
        check(fnv1a(script) == item.hash,
              "each named UI script has its independent pinned hash");
    }

    expect_out_of_range(
        [&] { (void)data.script(static_cast<UiScript>(0)); },
        "script id zero is rejected");
    expect_out_of_range(
        [&] { (void)data.script(static_cast<UiScript>(9)); },
        "an unused in-range script id is rejected");
    expect_out_of_range(
        [&] { (void)data.script(static_cast<UiScript>(28)); },
        "a script id beyond the table is rejected");
}

void test_vehicle_data(const UiTextData& data)
{
    constexpr std::array<std::size_t, 4> preview_lengths{68, 72, 76, 78};
    constexpr std::array<std::uint32_t, 4> preview_hashes{
        0xE4963ABCU, 0x99D14BEEU, 0x65966142U, 0xD91AEE5AU};
    constexpr std::array<UiTextData::VehiclePrice, 4> prices{{
        {0x00, 0x20}, {0x00, 0x48}, {0x00, 0x60}, {0x01, 0x50},
    }};

    for (std::uint8_t vehicle = 0; vehicle < 4; ++vehicle) {
        const auto preview = data.vehicle_preview(vehicle);
        check(preview.size() == preview_lengths[vehicle] &&
                  preview.back() == 0xFF,
              "each vehicle preview has its pinned FF-inclusive boundary");
        check(fnv1a(preview) == preview_hashes[vehicle],
              "each vehicle preview has its independent pinned hash");
        const auto price = data.vehicle_price(vehicle);
        check(price.high_bcd == prices[vehicle].high_bcd &&
                  price.middle_bcd == prices[vehicle].middle_bcd,
              "each vehicle price preserves both pinned BCD bytes");
    }

    constexpr std::array<std::uint32_t, 2> hint_hashes{
        0x4602DC79U, 0x8512AF19U};
    for (std::size_t row = 0; row < 2; ++row) {
        const auto hint = data.vehicle_hint_row(row);
        check(hint.size() == 36 && fnv1a(hint) == hint_hashes[row],
              "each vehicle hint row has its fixed width and pinned hash");
    }

    expect_out_of_range([&] { (void)data.vehicle_preview(4); },
                        "vehicle preview rejects index four");
    expect_out_of_range([&] { (void)data.vehicle_price(4); },
                        "vehicle price rejects index four");
    expect_out_of_range([&] { (void)data.vehicle_hint_row(2); },
                        "vehicle hint rejects row two");
}

} // namespace

int main()
{
    try {
        const Payload payload = Payload::embedded();
        const UiTextData data(payload);
        test_scripts(data);
        test_vehicle_data(data);
        if (failures != 0) return 1;
        std::cout << "UI text data tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
