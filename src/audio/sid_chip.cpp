#include "audio/sid_chip.hpp"

#include <residfp/residfp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::audio {
namespace {

constexpr double kPalClockFrequency = 985248.0;
constexpr double kOutputFrequency = 48000.0;
constexpr std::size_t kClockChunkCycles = 8192;
// One SID clock can produce at most one sample. This bound is deliberately
// independent of the resampler's approximate output formula.
constexpr std::size_t kOutputChunkSamples = kClockChunkCycles;
constexpr float kPcmScale = 1.0F / 32768.0F;

} // namespace

struct SidChip::Impl {
    reSIDfp::residfp chip;
};

SidChip::SidChip()
    : impl_(std::make_unique<Impl>())
{
    if (!impl_->chip.setChipModel(reSIDfp::MOS6581)) {
        throw std::runtime_error("reSIDfp rejected the MOS6581 chip model");
    }
    if (!impl_->chip.setSamplingParameters(kPalClockFrequency, reSIDfp::RESAMPLE,
                                           kOutputFrequency)) {
        throw std::runtime_error("reSIDfp rejected PAL/48 kHz sampling parameters");
    }
    impl_->chip.reset();
}

SidChip::~SidChip() = default;

SidChip::SidChip(SidChip&& other) noexcept = default;

SidChip& SidChip::operator=(SidChip&& other) noexcept = default;

void SidChip::reset()
{
    if (!impl_) throw std::runtime_error("Cannot reset an empty SidChip");
    impl_->chip.reset();
}

void SidChip::write(std::uint8_t reg, std::uint8_t value)
{
    if (reg >= 0x20) {
        throw std::out_of_range("SID register must be in the range 0x00..0x1f");
    }
    if (!impl_) throw std::runtime_error("Cannot write an empty SidChip");
    impl_->chip.write(reg, value);
}

std::vector<float> SidChip::clock(std::uint32_t cycles)
{
    if (cycles == 0) return {};
    if (!impl_) throw std::runtime_error("Cannot clock an empty SidChip");

    std::vector<float> output;
    const auto estimated = (static_cast<std::uint64_t>(cycles) * 48000U + 985247U) /
                           985248U;
    if (estimated > output.max_size()) {
        throw std::length_error("Requested SID clock range is too large for a sample vector");
    }
    output.reserve(static_cast<std::size_t>(estimated));
    std::array<short, kOutputChunkSamples> pcm{};
    std::uint32_t remaining = cycles;
    while (remaining != 0) {
        const auto chunk = std::min<std::uint32_t>(remaining,
                                                    static_cast<std::uint32_t>(kClockChunkCycles));
        const auto produced = impl_->chip.clock(static_cast<unsigned int>(chunk), pcm.data());
        if (produced < 0 || static_cast<std::size_t>(produced) > pcm.size()) {
            throw std::runtime_error("reSIDfp produced an invalid PCM sample count");
        }
        for (int index = 0; index < produced; ++index) {
            output.push_back(static_cast<float>(pcm[static_cast<std::size_t>(index)]) * kPcmScale);
        }
        remaining -= chunk;
    }
    return output;
}

} // namespace ghostbusters::audio
