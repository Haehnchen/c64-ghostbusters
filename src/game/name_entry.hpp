#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghostbusters::game {

class NameEntry {
public:
    enum class Result { ignored, inserted, erased, submitted };

    explicit NameEntry(std::size_t maximum = 18);
    explicit NameEntry(const std::array<std::uint8_t, 20>& saved,
                       std::size_t maximum = 18);

    // Accepts one translated key from the keyboard layer. Function-key
    // sentinels are filtered by that layer; printable translated bytes are
    // accepted here, including $2F.
    [[nodiscard]] Result key(std::uint8_t translated);

    [[nodiscard]] const std::array<std::uint8_t, 20>& bytes() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool submitted() const noexcept;

private:
    std::array<std::uint8_t, 20> bytes_{};
    std::size_t size_ = 0;
    std::size_t maximum_ = 18;
    bool submitted_ = false;
};

} // namespace ghostbusters::game
