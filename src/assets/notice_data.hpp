#pragma once

#include "assets/payload.hpp"
#include <array>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

class NoticeData {
public:
    explicit NoticeData(const Payload& payload)
    {
        constexpr std::array<std::string_view, 10> names{
            "text/notice_0", "text/notice_1", "text/notice_2", "text/notice_3",
            "text/notice_4", "text/notice_5", "text/notice_6", "text/notice_7",
            "text/notice_8", "text/notice_9"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto bytes = payload.asset(names[i]);
            if (bytes.empty() || bytes.back() != 0xFF)
                throw std::runtime_error("Notice has no terminator");
            // The scroller supplies its own terminator.
            notices_[i] = bytes.first(bytes.size() - 1);
        }
    }
    [[nodiscard]] std::span<const std::uint8_t> notice(std::uint8_t index) const
    {
        if (index >= notices_.size()) throw std::out_of_range("notice index");
        return notices_[index];
    }
private:
    std::array<std::span<const std::uint8_t>, 10> notices_{};
};

} // namespace ghostbusters::assets
