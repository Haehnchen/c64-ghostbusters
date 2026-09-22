#pragma once

#include "video/character_frame.hpp"
#include <span>

namespace ghostbusters::video {

// Composite the title character layer with the moving lyric window and
// bottom row's fine scroll/clipping. The live view adds ball and footer.
[[nodiscard]] IndexedImage render_title(const CharacterFrame& frame, std::uint8_t fine_scroll,
                                       std::uint8_t text_phase = 0);

// Shared credits strip. The supplied frame contains the current scroller row;
// the rainbow is the separately colored graphic behind its white lettering.
void composite_footer(IndexedImage& image, const CharacterFrame& frame,
                      std::uint16_t phase, std::span<const std::uint8_t> rainbow,
                      std::span<const std::uint8_t> rainbow_colors,
                      bool visible = true);

} // namespace ghostbusters::video
