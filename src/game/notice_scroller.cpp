#include "game/notice_scroller.hpp"

#include <algorithm>

namespace ghostbusters::game {
namespace {

constexpr std::uint8_t kFirstVisibleState = 0x12;
constexpr std::uint8_t kScrollWindowPrefix = 0x28;
constexpr std::uint8_t kCompactionPosition = 0x78;
constexpr std::uint8_t kCompactionPositionAfter = 0x50;
constexpr std::size_t kRowStart = 22U * 40U;

} // namespace

NoticeScroller::NoticeScroller()
{
    buffer_.fill(0xFF);
}

NoticeScroller::NoticeScroller(const Snapshot& snapshot)
    : phase49_(snapshot.phase49), length4a_(snapshot.length4a),
      position4b_(snapshot.position4b), cached_status4c_(snapshot.cached_status4c),
      buffer_(snapshot.buffer), row_(snapshot.row)
{
}

bool NoticeScroller::queue(std::span<const std::uint8_t> translated)
{
    // $99CC first tests $4B.  Do that before validating the input so a busy
    // scroller remains a strict no-op for every producer request.
    if (position4b_ != 0 || translated.empty() ||
        translated.size() > max_payload_size ||
        std::find(translated.begin(), translated.end(), std::uint8_t{0xFF}) !=
            translated.end()) {
        return false;
    }

    // The normal idle path resets $4A; $99CC itself increments the existing
    // byte for each output character. It writes only the payload
    // and its terminator; bytes after that terminator retain their old RAM
    // values, even though the bounded renderer cannot make them visible.
    std::copy(translated.begin(), translated.end(), buffer_.begin());
    buffer_[translated.size()] = 0xFF;
    length4a_ = static_cast<std::uint8_t>(length4a_ + translated.size());
    position4b_ = 1;
    return true;
}

void NoticeScroller::tick(std::uint8_t state, video::CharacterFrame& frame)
{
    // Keep the probe-visible row synchronized even on an early return.  The
    // original routine leaves screen RAM untouched in this case.
    std::copy_n(frame.screen.begin() + static_cast<std::ptrdiff_t>(kRowStart),
                row_.size(), row_.begin());

    if (state < kFirstVisibleState || position4b_ == 0) return;

    // $7206/$7208: two ordinary byte decrements, with 8-bit wraparound.
    phase49_ = static_cast<std::uint8_t>(phase49_ - 2U);
    if ((phase49_ & 0x06U) == 0x06U) {
        ++position4b_;

        // At $78 the original copies the still-unseen tail beginning 40
        // bytes into the E900 image, then resumes at $50.
        if (position4b_ >= kCompactionPosition) {
            std::size_t source = kScrollWindowPrefix;
            std::size_t destination = 0;
            while (source < buffer_.size()) {
                const auto value = buffer_[source++];
                buffer_[destination++] = value;
                if (value == 0xFF) break;
            }
            position4b_ = kCompactionPositionAfter;
            length4a_ = static_cast<std::uint8_t>(length4a_ - kScrollWindowPrefix);
        }
    }

    // $7234 detects the terminator before entering the 40-byte copy loop.
    // The reset jumps to $726D, so the final frame remains the one written on
    // the preceding tick.
    const auto position = position4b_;
    if (position >= kScrollWindowPrefix &&
        buffer_[static_cast<std::uint8_t>(position - kScrollWindowPrefix)] == 0xFF) {
        position4b_ = 0;
        length4a_ = 0;
        cached_status4c_ = 0;
        return;
    }

    write_row(frame);
}

void NoticeScroller::write_row(video::CharacterFrame& frame)
{
    // $23 starts at $FF and is cleared once the E900 terminator is reached.
    // The indexed E8D8 base means x=$28 corresponds to buffer[0]. Both x
    // and the 6502 index are eight-bit values, hence the explicit wrapping.
    std::uint8_t visible = 0xFF;
    std::uint8_t x = position4b_;
    for (std::size_t column = 0; column < row_.size(); ++column, ++x) {
        std::uint8_t value = 0;
        if (x >= kScrollWindowPrefix) {
            const auto buffer_index = static_cast<std::uint8_t>(x - kScrollWindowPrefix);
            value = buffer_[buffer_index];
            if (value == 0xFF) {
                visible = 0;
                value = 0;
            }
            value = static_cast<std::uint8_t>(value & visible);
        }
        row_[column] = value;
        frame.screen[kRowStart + column] = value;
    }
}

NoticeScroller::Snapshot NoticeScroller::snapshot() const noexcept
{
    return Snapshot{phase49_, length4a_, position4b_, cached_status4c_, buffer_, row_};
}

} // namespace ghostbusters::game
