#include "game/title_sequence.hpp"

#include <stdexcept>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kTextSize = 0x4AD;
constexpr std::size_t kTimelineSize = 0x70;
constexpr std::size_t kFirstColumn = 7;
constexpr std::size_t kEndColumn = 40;
constexpr std::size_t kTitleRow = 0x320;
constexpr std::size_t kShiftSource = 0x348;
constexpr std::size_t kFinalRow = 0x370;

[[nodiscard]] std::uint8_t screen_code(const std::uint8_t value) noexcept
{
    if (value == 0x20) return 0;
    if (value >= 0x40) return static_cast<std::uint8_t>(value - 0x40U);
    return value;
}

} // namespace

TitleSequence::TitleSequence(const assets::Payload& payload)
    : text_(payload.asset("title/lyrics")),
      timeline_(payload.asset("title/timeline"))
{
    if (text_.size() != kTextSize || text_.back() != 0xFF)
        throw std::runtime_error("Title lyric stream has an unexpected layout");
    if (timeline_.size() != kTimelineSize)
        throw std::runtime_error("Title timeline has an unexpected size");
}

TitleSequenceEvent TitleSequence::tick(video::CharacterFrame& frame,
                                       const bool space_down,
                                       const bool title_speech_active)
{
    advance_timeline();

    if (title_speech_active) return TitleSequenceEvent::none;

    if (space_down && !space_was_down_) pending_space_ = true;
    space_was_down_ = space_down;

    // PAL foreground performs its correction immediately after the first
    // timeline call. Speech's blocking wrapper does not take this path.
    if ((counter_ & 0x07U) == 1U) advance_timeline();

    if (gate_ != 0) {
        --gate_;
        if ((gate_ & 0x0FU) == 0) {
            write_next_line(frame);
        } else if ((gate_ & 0x07U) == 0x07U) {
            shift_title_rows(frame);
        }
    }

    if (pending_space_ && gate_ == 0) {
        pending_space_ = false;
        return TitleSequenceEvent::start_speech_1;
    }
    return TitleSequenceEvent::none;
}

void TitleSequence::reset_timeline() noexcept
{
    counter_ = 0;
    timeline_index_ = 0;
}

void TitleSequence::advance_timeline()
{
    ++counter_;
    if (timeline_index_ == timeline_.size()) return;

    const auto marker = timeline_[timeline_index_];
    if (marker != counter_) return;

    if (marker != 0)
        gate_ = static_cast<std::uint8_t>(gate_ + 0x10U);
    ++timeline_index_;
}

void TitleSequence::shift_title_rows(video::CharacterFrame& frame) const
{
    // Keep the original three loops explicit. The first clear is overwritten
    // by the forward copy, but is part of the observed title operation.
    for (std::size_t index = 0; index < 40; ++index)
        frame.screen[kTitleRow + index] = 0;
    for (std::size_t index = 0; index < 80; ++index)
        frame.screen[kTitleRow + index] = frame.screen[kShiftSource + index];
    for (std::size_t index = 0; index < 40; ++index)
        frame.screen[kFinalRow + index] = 0;
}

void TitleSequence::write_next_line(video::CharacterFrame& frame)
{
    std::size_t column = kFirstColumn;
    std::size_t consumed = 0;

    for (;;) {
        if (text_cursor_ + consumed >= text_.size())
            throw std::runtime_error("Title lyric cursor left its bounded stream");

        const auto value = text_[text_cursor_ + consumed];
        if (value == 0xFF) {
            text_cursor_ = 0;
            return;
        }
        if (value == 0x0D) break;

        frame.screen[kFinalRow + column] = screen_code(value);
        ++consumed;
        ++column;
        if (column == kEndColumn) break;
    }

    // The helper consumes the CR after a natural line. At the 33-character
    // width limit it likewise skips one following byte, even if that byte is
    // not a CR, before filling from the current column.
    text_cursor_ += consumed + 1;
    do {
        frame.screen[kFinalRow + column] = 0;
        ++column;
    } while (column < kEndColumn);
}

} // namespace ghostbusters::game
