#include "audio/sid_chip.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        ghostbusters::audio::SidChip chip;
        // Voice-3 setup used by the original text writer at $7306-$733A.
        chip.write(0x12, 0x08);
        chip.write(0x0E, 0x12); chip.write(0x0F, 0x34);
        chip.write(0x10, 0x80); chip.write(0x11, 0x0F);
        chip.write(0x13, 0x03); chip.write(0x14, 0x00);
        chip.write(0x18, 0x0F); chip.write(0x12, 0x41);
        const auto samples = chip.clock(985248);
        // reSIDfp uses fixed-point phase increments in two resampling stages;
        // the pinned build produces 48011 samples for 985248 cycles. Allow
        // sub-0.1% quantization error rather than assuming an exact ratio.
        if (samples.size() < 47952 || samples.size() > 48048) throw std::runtime_error("SID sample rate mismatch: " + std::to_string(samples.size()));
        if (!std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 1; })) {
            throw std::runtime_error("Invalid SID PCM values");
        }
        const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
        if (*high - *low < 0.001F) throw std::runtime_error("SID text-tone setup produced no varying signal");
        std::cout << "SID PAL/48kHz PCM smoke passed: " << samples.size() << " samples\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
