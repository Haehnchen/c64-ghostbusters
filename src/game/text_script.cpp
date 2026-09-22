#include "game/text_script.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kScreenColumns = 40;
constexpr std::size_t kScreenRows = 25;

void validateCursor(std::uint8_t column, std::uint8_t row)
{
    if (column >= kScreenColumns || row >= kScreenRows) {
        throw std::out_of_range("Text script cursor is outside the 40x25 screen");
    }
}

} // namespace

void TextScript::start(std::span<const std::uint8_t> bytes, std::uint8_t column,
                       std::uint8_t row, std::uint8_t color)
{
    validateCursor(column, row);
    const auto terminator = std::find(bytes.begin(), bytes.end(), std::uint8_t{0xFF});
    if (terminator == bytes.end()) {
        throw std::runtime_error("Runtime text script has no FF terminator");
    }

    runtime_bytes_.assign(bytes.begin(), std::next(terminator));
    runtime_index_ = 0;
    column_ = column;
    row_ = row;
    color_ = color;
    active_ = true;
}

bool TextScript::active() const noexcept
{
    return active_;
}

SoundEvents TextScript::take_sound_events()
{
    auto events = std::move(sound_events_);
    sound_events_.clear();
    return events;
}

void TextScript::tick(video::CharacterFrame& frame,
                      std::uint8_t frame_counter, std::uint8_t cadence_mask)
{
    rendered_bytes_.clear();
    if (!active_ || (frame_counter & cadence_mask) != 0) return;
    const unsigned count = cadence_mask == 0 ? 40 : 1;
    for (unsigned i = 0; i < count && active_; ++i) advance_byte(frame, cadence_mask);
}

void TextScript::advance_byte(video::CharacterFrame& frame,
                              std::uint8_t cadence_mask)
{
    const auto value = next_source_byte();

    // Cursor controls also trigger a tone; FF terminates silently.
    if (cadence_mask >= 3 && value >= 0x21 && value != 0xFF) {
        sound_events_.push_back(SoundEvent::text_tone);
    }

    if (value == 0xFF) {
        active_ = false;
        return;
    }
    rendered_bytes_.push_back(value);
    // Zero consumes time but does not write a glyph.
    if (value == 0x00) return;
    if ((value & 0x80) != 0) {
        column_ = static_cast<std::uint8_t>(value & 0x3F);
        return;
    }
    if (value == 0x0D) {
        if (static_cast<std::size_t>(row_) + 1 >= kScreenRows) {
            throw std::out_of_range("Text script newline exceeds the screen");
        }
        column_ = 1;
        ++row_;
        return;
    }

    const std::size_t screenIndex = static_cast<std::size_t>(row_) * kScreenColumns + column_;
    if (screenIndex >= kScreenColumns * kScreenRows) {
        throw std::out_of_range("Text script write exceeds the screen");
    }
    const auto code = value == 0x20
                          ? std::uint8_t{0}
                          : value >= 0x40 ? static_cast<std::uint8_t>(value - 0x40)
                                          : value;
    frame.screen[screenIndex] = code;
    frame.colors[screenIndex] = color_;
    ++column_;

    // Only slow, color-zero text wraps at spaces past column 30.
    if (value == 0x20 && cadence_mask != 0 && color_ == 0 && column_ >= 0x20) {
        if (static_cast<std::size_t>(row_) + 1 >= kScreenRows) {
            throw std::out_of_range("Text script autowrap exceeds the screen");
        }
        column_ = 1;
        ++row_;
    }
}

std::uint8_t TextScript::next_source_byte()
{
    if (runtime_index_ >= runtime_bytes_.size()) {
        throw std::runtime_error("Text script ended without FF terminator");
    }
    return runtime_bytes_[runtime_index_++];
}

std::uint8_t TextScript::column() const noexcept
{
    return column_;
}

std::uint8_t TextScript::row() const noexcept
{
    return row_;
}

void TextScript::set_cursor(std::uint8_t column, std::uint8_t row)
{
    validateCursor(column, row);
    column_ = column;
    row_ = row;
}

} // namespace ghostbusters::game
