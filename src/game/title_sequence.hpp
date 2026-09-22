#pragma once

#include "assets/payload.hpp"
#include "video/character_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostbusters::game {

enum class TitleSequenceEvent {
    none,
    start_speech_1,
};

// Functional title timeline, lyric-line and Space-repeat state. One tick is
// one native title foreground update; CPU, raster and audio timing remain at
// the platform boundary.
class TitleSequence {
public:
    explicit TitleSequence(const assets::Payload& payload);

    // title_speech_active models the blocking title speech wrapper: its busy
    // loop advances the timeline, but does not poll input or run the ordinary
    // foreground gate, screen shifts, lyric helper or repeat request.
    [[nodiscard]] TitleSequenceEvent tick(video::CharacterFrame& frame,
                                          bool space_down,
                                          bool title_speech_active = false);

    // The two startup streams reset the counter and timeline index only.
    // Gate and lyric position are
    // deliberately retained, matching the title startup boundary.
    void reset_timeline() noexcept;

    [[nodiscard]] std::uint8_t counter() const noexcept { return counter_; }
    [[nodiscard]] std::size_t timeline_index() const noexcept { return timeline_index_; }
    [[nodiscard]] std::uint8_t gate() const noexcept { return gate_; }
    [[nodiscard]] std::size_t text_cursor() const noexcept { return text_cursor_; }
    [[nodiscard]] bool pending_space() const noexcept { return pending_space_; }

private:
    void advance_timeline();
    void shift_title_rows(video::CharacterFrame& frame) const;
    void write_next_line(video::CharacterFrame& frame);

    std::span<const std::uint8_t> text_;
    std::span<const std::uint8_t> timeline_;
    std::uint8_t counter_ = 0;
    std::size_t timeline_index_ = 0;
    std::uint8_t gate_ = 0;
    std::size_t text_cursor_ = 0;
    bool pending_space_ = false;
    bool space_was_down_ = false;
};

} // namespace ghostbusters::game
