#include "game/name_entry.hpp"

#include <algorithm>
#include <stdexcept>

namespace ghostbusters::game {

NameEntry::NameEntry(std::size_t maximum)
    : maximum_(maximum)
{
    if (maximum > 19) {
        throw std::out_of_range("Name entry maximum exceeds its 20-byte buffer");
    }
}

NameEntry::NameEntry(const std::array<std::uint8_t, 20>& saved,
                     const std::size_t maximum)
    : bytes_(saved), maximum_(maximum), submitted_(true)
{
    if (maximum > 19) {
        throw std::out_of_range("Name entry maximum exceeds its 20-byte buffer");
    }
    const auto terminator = std::find(bytes_.begin(), bytes_.end(), 0);
    size_ = std::min<std::size_t>(static_cast<std::size_t>(terminator - bytes_.begin()),
                                  maximum_);
}

NameEntry::Result NameEntry::key(std::uint8_t translated)
{
    if (submitted_) return Result::ignored;

    if (translated == 0x0D) {
        bytes_[size_] = 0;
        submitted_ = true;
        return Result::submitted;
    }

    if (translated == 0x7F) {
        if (size_ == 0) return Result::ignored;
        --size_;
        // The original input path leaves a visible space in the old tail
        // slot until Return writes the terminator at bytes_[size_].
        bytes_[size_] = 0x20;
        return Result::erased;
    }

    if (translated < 0x20 || translated > 0x7E || size_ >= maximum_) {
        return Result::ignored;
    }

    bytes_[size_] = translated;
    ++size_;
    return Result::inserted;
}

const std::array<std::uint8_t, 20>& NameEntry::bytes() const noexcept
{
    return bytes_;
}

std::size_t NameEntry::size() const noexcept
{
    return size_;
}

bool NameEntry::submitted() const noexcept
{
    return submitted_;
}

} // namespace ghostbusters::game
