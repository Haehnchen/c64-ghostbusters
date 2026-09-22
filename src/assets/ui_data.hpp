#pragma once

#include "assets/payload.hpp"
#include <array>
#include <stdexcept>

namespace ghostbusters::assets {

class UiData {
public:
    explicit UiData(const Payload& data)
        : keyboard_(data.asset("ui/keyboard")), scroller_(data.asset("ui/scroller")),
          font_patch_(data.asset("ui/font_patch")), footer_colors_(data.asset("ui/footer_colors")),
          charset_(data.asset("title/charset")), header_(data.asset("title/header")),
          logo_(data.asset("title/logo"))
    {
        if (keyboard_.size() != 64 || scroller_.size() != 128 || font_patch_.size() != 128 ||
            footer_colors_.size() != 8 || charset_.size() != 1536 || header_.size() != 90 ||
            logo_.size() != 216) throw std::runtime_error("Unexpected UI resource dimensions");
    }

    // Partial reset copies the first twelve entries; full reset copies all eighteen.
    [[nodiscard]] auto reset_initial_state() const { return std::span<const std::uint8_t>(kResetState); }
    [[nodiscard]] auto keyboard() const { return keyboard_; }
    [[nodiscard]] auto scroller_text() const { return scroller_; }
    [[nodiscard]] auto font_patch() const { return font_patch_; }
    [[nodiscard]] auto footer_colors() const { return footer_colors_; }
    [[nodiscard]] auto title_charset() const { return charset_; }
    [[nodiscard]] auto header_row(std::size_t row) const
    {
        if (row >= 3) throw std::out_of_range("title header row");
        return header_.subspan(row * 30, 30);
    }
    [[nodiscard]] auto logo_row(std::size_t row) const
    {
        if (row >= 12) throw std::out_of_range("title logo row");
        return logo_.subspan(row * 18, 18);
    }

private:
    static constexpr std::array<std::uint8_t, 18> kResetState{
        31, 31, 15, 0, 15, 8, 1, 0, 8, 63, 3, 153, 0, 0, 0, 0, 0, 0};
    std::span<const std::uint8_t> keyboard_, scroller_, font_patch_, footer_colors_;
    std::span<const std::uint8_t> charset_, header_, logo_;
};

} // namespace ghostbusters::assets
