#pragma once

#include "video/character_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostbusters::game {

// The bounded state copied from the original scrolling-message RAM.  The
// buffer is the 256-byte window at $E900; its final byte is the $FF sentinel
// written by $99CC after the translated message bytes.
struct NoticeScrollerSnapshot {
    std::uint8_t phase49 = 0;
    std::uint8_t length4a = 0;
    std::uint8_t position4b = 0;
    std::uint8_t cached_status4c = 0;
    std::array<std::uint8_t, 256> buffer{};
    std::array<std::uint8_t, 40> row{};
};

class NoticeScroller {
public:
    using Buffer = std::array<std::uint8_t, 256>;
    using Row = std::array<std::uint8_t, 40>;
    using Snapshot = NoticeScrollerSnapshot;

    static constexpr std::size_t max_payload_size = 255;

    NoticeScroller();
    // Restore an original runtime snapshot, including bytes after the terminator.
    explicit NoticeScroller(const Snapshot& snapshot);

    // Queue bytes already converted to C64 screen codes by the producer
    // (EquipmentSelection::pending_notice()).  The input is deliberately
    // sentinel-free: queue writes the sole $FF terminator itself.  Like
    // $99CC, a nonzero $4B makes this a no-op and returns false.
    [[nodiscard]] bool queue(std::span<const std::uint8_t> translated);

    // Advance the bounded $71F6-$726C update.  States below $12 return
    // immediately, which keeps a notice queued in shop state 15 invisible.
    // Only screen row 22 is written; colors, audio, and status-line fields
    // owned by the surrounding IRQ are outside this module.
    void tick(std::uint8_t state, video::CharacterFrame& frame);

    [[nodiscard]] std::uint8_t phase49() const noexcept { return phase49_; }
    [[nodiscard]] std::uint8_t length4a() const noexcept { return length4a_; }
    [[nodiscard]] std::uint8_t position4b() const noexcept { return position4b_; }
    [[nodiscard]] std::uint8_t cached_status4c() const noexcept
    {
        return cached_status4c_;
    }

    [[nodiscard]] const Buffer& buffer() const noexcept { return buffer_; }
    [[nodiscard]] const Row& row() const noexcept { return row_; }
    [[nodiscard]] Snapshot snapshot() const noexcept;

    // This is useful when a probe starts at a captured value of $49.  Queue
    // itself never changes the phase, matching $99CC.
    void set_phase49(std::uint8_t value) noexcept { phase49_ = value; }

private:
    void write_row(video::CharacterFrame& frame);

    std::uint8_t phase49_ = 0;
    std::uint8_t length4a_ = 0;
    std::uint8_t position4b_ = 0;
    std::uint8_t cached_status4c_ = 0;
    Buffer buffer_{};
    Row row_{};
};

} // namespace ghostbusters::game
